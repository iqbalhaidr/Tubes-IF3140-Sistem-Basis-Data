#include "concurrency_control.h"
#include "failure_recovery.h"
#include "types.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using mdbms::Action;
using mdbms::ExecutionResult;
using mdbms::Row;
using mdbms::Rows;
using mdbms::ccm::ConcurrencyControlManager;
using mdbms::fr::FailureRecoveryManager;

Row make_row(int id, const std::string& owner) {
    Row row;
    row.table_name = "MultithreadTest";
    row.row_id = id;
    row.columns["owner"] = owner;
    row.columns["payload"] = std::string("row-") + std::to_string(id);
    return row;
}

void ensure_data_directories() {
    std::vector<fs::path> paths = {"data", "../data"};
    for (const auto& path : paths) {
        std::error_code ec;
        fs::create_directories(path, ec);
    }
}

void run_worker(int worker_id,
                int ops_per_worker,
                ConcurrencyControlManager& ccm,
                FailureRecoveryManager& frm,
                std::atomic<int>& committed_count) {
    for (int i = 0; i < ops_per_worker; ++i) {
        const int txn_id = ccm.begin_transaction();

        ExecutionResult begin_log;
        begin_log.transaction_id = txn_id;
        begin_log.query = "BEGIN";
        begin_log.success = true;
        frm.write_log(begin_log);

        Row target = make_row(worker_id * 100 + i, "worker-" + std::to_string(worker_id));
        ccm.log_object(target, txn_id);
        mdbms::Response resp = ccm.validate_object(target, txn_id, Action::WRITE);

        ExecutionResult op_log;
        op_log.transaction_id = txn_id;
        op_log.success = true;
        op_log.data = Rows<Row>(std::vector<Row>{target});
        op_log.query = resp.allowed
            ? "INSERT INTO MultithreadTest VALUES (...)"
            : "ABORT DURING WRITE";
        frm.write_log(op_log);

        ExecutionResult end_log;
        end_log.transaction_id = txn_id;
        end_log.success = true;
        if (resp.allowed) {
            end_log.query = "COMMIT";
            committed_count.fetch_add(1, std::memory_order_relaxed);
        } else {
            end_log.query = "ABORT";
        }
        frm.write_log(end_log);

        ccm.end_transaction(txn_id);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

int main() {
    ensure_data_directories();

    auto& ccm = ConcurrencyControlManager::get_instance("twophaselocking");
    auto& frm = FailureRecoveryManager::get_instance();

    frm.reset_state_for_testing();
    frm.flush_logs_for_testing();
    const std::size_t initial_logs = frm.read_all_logs_public("../data/wal.bin").size();

    const int worker_count = 4;
    const int ops_per_worker = 3;
    std::atomic<int> committed{0};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for (int w = 0; w < worker_count; ++w) {
        workers.emplace_back(run_worker, w, ops_per_worker, std::ref(ccm), std::ref(frm),
                             std::ref(committed));
    }
    for (auto& t : workers) {
        t.join();
    }

    frm.flush_logs_for_testing();
    const std::size_t final_logs = frm.read_all_logs_public("../data/wal.bin").size();

    std::cout << "\n=== Multithreaded CCM + FR Summary ===\n";
    std::cout << "Transactions committed: " << committed.load() << " / "
              << (worker_count * ops_per_worker) << '\n';
    std::cout << "New log entries flushed: "
              << (final_logs >= initial_logs ? final_logs - initial_logs : 0) << '\n';
    std::cout << "Log file: ../data/wal.bin\n";

    return 0;
}

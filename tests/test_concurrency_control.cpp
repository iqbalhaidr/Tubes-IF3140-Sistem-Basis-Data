#include <any>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "types.h"

#include "concurrency_control.h"

using namespace mdbms;
using namespace mdbms::ccm;

Row create_row(int id, std::string val_content) {
    Row r;
    r.row_id = id;
    r.table_name = "TestTable";
    r.columns["data_col"] = val_content;
    return r;
}

void print_response(const std::string& prefix, const Response& resp) {
    std::cout << prefix << "Allowed: " << (resp.allowed ? "True" : "False")
              << ", TID: " << resp.transaction_id << std::endl;
}

/* TES TIMESTAMP */
void print_result(const std::string& msg, bool ok) {
    const std::string GREEN = "\033[32m";
    const std::string RED = "\033[31m";
    const std::string RESET = "\033[0m";

    if (ok)
        std::cout << GREEN << "[PASS] " << msg << RESET << std::endl;
    else
        std::cerr << RED << "[FAIL] " << msg << RESET << std::endl;
}

void test_singleton() {
    std::cout << "\n--- TEST 1: Singleton Pattern ---" << std::endl;

    auto& c1 = ConcurrencyControlManager::get_instance();
    auto& c2 = ConcurrencyControlManager::get_instance();

    bool ok = (&c1 == &c2);
    print_result("Singleton instance konsisten", ok);
    std::cout << "\n--- TEST 1.1: Singleton Switch Algorithm ---" << std::endl;

    c1.switch_algorithm("twophaselocking");
    bool ok2 = (&c1 == &c2);
    print_result("Singleton Switch konsisten", ok2);

    c1.switch_algorithm("timestamp");
}

void test_singleton_thread_safety() {
    std::cout << "\n--- TEST 2: Thread Safety Singleton ---" << std::endl;

    const int N = 10;
    std::vector<std::thread> threads;
    std::vector<ConcurrencyControlManager*> refs(N);

    for (int i = 0; i < N; i++) {
        threads.emplace_back(
            [&](int idx) { refs[idx] = &ConcurrencyControlManager::get_instance(); }, i);
    }
    for (auto& t : threads)
        t.join();

    bool ok = true;
    for (int i = 1; i < N; i++)
        if (refs[i] != refs[0])
            ok = false;

    print_result("Semua thread mendapat instance yang sama", ok);
}

void test_basic_transaction_lifecycle() {
    std::cout << "\n--- TEST 3: Basic Transaction Lifecycle ---" << std::endl;
    auto& ccm = ConcurrencyControlManager::get_instance("timestamp");

    int tid = ccm.begin_transaction();
    bool ok = (tid > 0);
    if (!ok)
        print_result("Transaction ID tidak valid", false);

    Row row = create_row(1, "test_table");

    try {
        ccm.log_object(row, tid);

        Response r = ccm.validate_object(row, tid, Action::READ);
        Response w = ccm.validate_object(row, tid, Action::WRITE);

        ok = r.allowed && w.allowed;
        ccm.end_transaction(tid);

        print_result("Transaction lifecycle berhasil", ok);
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] Exception: " << e.what() << std::endl;
    }
}

void test_timestamp_read_after_write() {
    std::cout << "\n--- TEST 4: Read After Write Conflict ---" << std::endl;

    auto& ccm = ConcurrencyControlManager::get_instance();

    Row r = create_row(100, "conflict_table");

    int t1 = ccm.begin_transaction();
    ccm.log_object(r, t1);
    assert(ccm.validate_object(r, t1, Action::WRITE).allowed);
    ccm.end_transaction(t1);

    int t2 = ccm.begin_transaction();
    ccm.log_object(r, t2);

    int t3 = ccm.begin_transaction();
    ccm.log_object(r, t3);
    assert(ccm.validate_object(r, t3, Action::WRITE).allowed);
    ccm.end_transaction(t3);

    Response rr = ccm.validate_object(r, t2, Action::READ);

    ccm.end_transaction(t2);
    print_result("Timestamp read conflict terdeteksi", !rr.allowed);
}

void test_timestamp_write_after_read() {
    std::cout << "\n--- TEST 5: Write After Read Conflict ---" << std::endl;

    auto& ccm = ConcurrencyControlManager::get_instance();

    Row r = create_row(200, "conflict_table");

    int ta = ccm.begin_transaction();
    ccm.log_object(r, ta);
    assert(ccm.validate_object(r, ta, Action::READ).allowed);

    int tb = ccm.begin_transaction();
    ccm.end_transaction(ta);

    int tc = ccm.begin_transaction();
    ccm.log_object(r, tc);
    assert(ccm.validate_object(r, tc, Action::READ).allowed);
    ccm.end_transaction(tc);

    ccm.log_object(r, tb);
    Response wr = ccm.validate_object(r, tb, Action::WRITE);

    print_result("Timestamp write conflict terdeteksi", !wr.allowed);

    ccm.end_transaction(tb);
}

void test_concurrent_transactions() {
    std::cout << "\n--- TEST 6: Concurrent Transactions ---" << std::endl;

    auto& ccm = ConcurrencyControlManager::get_instance();

    const int N = 5;
    const int OPS = 10;

    std::atomic<int> ok{0}, fail{0};
    std::vector<std::thread> th;

    for (int c = 0; c < N; c++) {
        th.emplace_back([&]() {
            for (int i = 0; i < OPS; i++) {
                int tid = ccm.begin_transaction();

                Row r = create_row(c, "shared_table");
                ccm.log_object(r, tid);

                Response rr = ccm.validate_object(r, tid, Action::READ);
                if (rr.allowed) {
                    Response wr = ccm.validate_object(r, tid, Action::WRITE);
                    wr.allowed ? ok++ : fail++;
                } else
                    fail++;

                ccm.end_transaction(tid);
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        });
    }

    for (auto& t : th)
        t.join();

    bool total_ok = ((ok + fail) == N * OPS);
    print_result("Concurrent transactions berhasil", total_ok && ok > 0);
}

void test_invalid_transaction_id() {
    std::cout << "\n--- TEST 7: Invalid Transaction ID ---" << std::endl;

    auto& ccm = ConcurrencyControlManager::get_instance();

    Row r = create_row(999, "test_table");

    bool caught = false;
    try {
        ccm.log_object(r, 999999);
    } catch (...) {
        caught = true;
    }

    print_result("Invalid transaction ID terdeteksi", caught);
}

void test_switch_algorithm() {
    std::cout << "\n--- TEST 8: Switch Algorithm ---" << std::endl;

    auto& ccm = ConcurrencyControlManager::get_instance();

    bool ok1 = true;
    try {
        ccm.switch_algorithm("timestamp");
    } catch (...) {
        ok1 = false;
    }
    print_result("Switch ke timestamp berhasil", ok1);

    bool ok2 = false;
    try {
        ccm.switch_algorithm("unsupported_algorithm");
    } catch (const std::invalid_argument&) {
        ok2 = true;
    }
    print_result("Unsupported algorithm terdeteksi", ok2);
}

/* TWO PHASE LOCKING*/

void test_twoPL_cc() {
    std::cout << "\nTesting Two-Phase Locking Concurrency Control\n" << std::endl;

    std::cout << "\n--- TEST 9: 2PL Concurruncy General ---" << std::endl;
    auto& ccm = ConcurrencyControlManager::get_instance();
    ccm.switch_algorithm("twophaselocking");

    Row row1 = create_row(1, "Group1");
    Row row2 = create_row(2, "Group2");

    int tid1 = ccm.begin_transaction();
    ccm.log_object(row1, tid1);
    Response response1 = ccm.validate_object(row1, tid1, Action::READ);
    print_response("Transaction " + std::to_string(tid1) + " READ on Row1: ", response1);
    print_result("T1 READ Row1", response1.allowed == true);

    int tid2 = ccm.begin_transaction();
    Response response2 = ccm.validate_object(row1, tid2, Action::READ);
    print_response("Transaction " + std::to_string(tid2) + " READ on Row1: ", response2);
    print_result("T2 READ Row1", response2.allowed == true);

    // tc: exclusive lock conflict
    // harusnya fail karena tid1 dan tid2 punya slock
    Response response3 = ccm.validate_object(row1, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row1: ", response3);
    print_result("T2 WRITE Row1 (expect reject)", response3.allowed == false);

    // tc: independent operations on different rows
    ccm.log_object(row2, tid2);
    Response response4 = ccm.validate_object(row2, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row2: ", response4);
    print_result("T2 WRITE Row2 (expect fail after abort)", response4.allowed == false);

    // tc: lock upgrade attempt (atau read di row lain)
    int tid3 = ccm.begin_transaction();
    Response response5 = ccm.validate_object(row2, tid3, Action::READ);
    print_response("Transaction " + std::to_string(tid3) + " READ on Row2: ", response5);
    print_result("T3 READ Row2", response5.allowed == true);
    ccm.end_transaction(tid1);

    // tid3 mencoba write row1 (setelah tid1 lepas lock)
    Response response6 = ccm.validate_object(row1, tid3, Action::WRITE);
    print_response("Transaction " + std::to_string(tid3) + " WRITE on Row1: ", response6);
    print_result("T3 WRITE Row1 after T1 ends", response6.allowed == true);

    ccm.end_transaction(tid2);
    ccm.end_transaction(tid3);
}

void test_twoPL_abort() {
    std::cout << "\nTesting Two-Phase Locking Abort Scenarios\n" << std::endl;
    std::cout << "\n--- TEST 10: Abort Scenarios ---" << std::endl;
    auto& ccm = ConcurrencyControlManager::get_instance();
    ccm.switch_algorithm("twophaselocking");

    Row row1 = create_row(10, "AliceBob");
    Row row2 = create_row(11, "CharlieDiana");

    int tid1 = ccm.begin_transaction();

    ccm.log_object(row1, tid1);
    Response response1 = ccm.validate_object(row1, tid1, Action::READ);
    print_response("Transaction " + std::to_string(tid1) + " READ on Row1: ", response1);
    print_result("T1 READ Row1 (expect allowed)", response1.allowed == true);

    int tid2 = ccm.begin_transaction();

    ccm.log_object(row2, tid2);
    Response response2 = ccm.validate_object(row2, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row2: ", response2);
    print_result("T2 WRITE Row2 (expect allowed)", response2.allowed == true);

    // tid1 commit -> masuk fase SHRINKING
    ccm.end_transaction(tid1);

    // coba ambil lock baru setelah commit (seharusnya gagal/abort)
    Response response3 = ccm.validate_object(row2, tid1, Action::READ);
    print_response("Transaction " + std::to_string(tid1) + " READ on Row2: ", response3);
    print_result("T1 READ Row2 after commit (expect fail)", response3.allowed == false);

    ccm.end_transaction(tid2);
}

// karena sekarang kalau transaksi nunggu langsung diabort, belum mungkin terjadi deadlock
void test_twoPL_deadlock() {
    std::cout << "\nTesting Two-Phase Locking Deadlock Scenario\n" << std::endl;
    std::cout << "\n--- TEST 11: Deadlock Scenarios ---" << std::endl;
    auto& ccm = ConcurrencyControlManager::get_instance();
    ccm.switch_algorithm("twophaselocking");

    Row row1 = create_row(20, "AliceOnly");
    Row row2 = create_row(21, "BobOnly");

    int tid1 = ccm.begin_transaction();
    ccm.log_object(row1, tid1);
    Response response1 = ccm.validate_object(row1, tid1, Action::READ);
    print_response("Transaction " + std::to_string(tid1) + " READ on Row1: ", response1);
    print_result("T1 READ Row1 (expect allowed)", response1.allowed == true);

    int tid2 = ccm.begin_transaction();
    ccm.log_object(row2, tid2);
    Response response2 = ccm.validate_object(row2, tid2, Action::READ);
    print_response("Transaction " + std::to_string(tid2) + " READ on Row2: ", response2);
    print_result("T2 READ Row2 (expect allowed)", response2.allowed == true);

    // T1 write R2 (punya T2) -> sekarang abort, nanti nunggu
    Response response3 = ccm.validate_object(row2, tid1, Action::WRITE);
    print_response("Transaction " + std::to_string(tid1) + " WRITE on Row2: ", response3);
    print_result("T1 WRITE Row2 (expect fail due to conflict)", response3.allowed == false);

    // T2 write R1 (punya T1) -> sekarang bisa langsung aja, nanti jadi deadlock
    Response response4 = ccm.validate_object(row1, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row1: ", response4);
    print_result("T2 WRITE Row1 (expect allowed)", response4.allowed == true);

    // sekarang T1 udah abort, nanti dicek apakah T1 masih bisa lanjut
    Response response5 = ccm.validate_object(row1, tid1, Action::WRITE);
    print_response("Transaction " + std::to_string(tid1) + " WRITE on Row1: ", response5);
    print_result("T1 WRITE Row1 after abort (expect fail)", response5.allowed == false);

    ccm.end_transaction(tid1);
    ccm.end_transaction(tid2);
}

void test_twoPL_deadlock_abort() {
    std::cout << "\nTesting Two-Phase Locking Deadlock with Abort Resolution\n" << std::endl;
    std::cout << "\n--- TEST 12: 2PL Deadlock with Abort Resolution ---\n";

    auto& ccm = ConcurrencyControlManager::get_instance();
    ccm.switch_algorithm("twophaselocking");

    Row row1 = create_row(30, "Alice");
    Row row2 = create_row(31, "Bob");

    int tid1 = ccm.begin_transaction();
    ccm.log_object(row1, tid1);
    Response response1 = ccm.validate_object(row1, tid1, Action::WRITE);
    print_response("Transaction " + std::to_string(tid1) + " WRITE on Row1: ", response1);
    print_result("T1 WRITE Row1 (expect allowed)", response1.allowed == true);

    int tid2 = ccm.begin_transaction();
    ccm.log_object(row2, tid2);
    Response response2 = ccm.validate_object(row2, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row2: ", response2);
    print_result("T2 WRITE Row2 (expect allowed)", response2.allowed == true);

    // T1 write R2 (punya T2) -> sekarang abort, nanti nunggu
    ccm.log_object(row2, tid1);
    Response response3 = ccm.validate_object(row2, tid1, Action::WRITE);
    print_response("Transaction " + std::to_string(tid1) + " WRITE on Row2: ", response3);
    print_result("T1 WRITE Row2 after conflict (expect fail)", response3.allowed == false);

    // T2 write R1 (punya T1) -> sekarang bisa langsung aja, nanti jadi deadlock
    ccm.log_object(row1, tid2);
    Response response4 = ccm.validate_object(row1, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row1: ", response4);
    print_result("T2 WRITE Row1 (expect allowed)", response4.allowed == true);

    Response response5 = ccm.validate_object(row1, tid1, Action::WRITE);
    print_response("T1 WRITE Row1 AFTER ABORT", response5);
    print_result("T1 WRITE Row1 after abort (expect fail)", response5.allowed == false);
    ccm.end_transaction(tid1);
    ccm.end_transaction(tid2);
}

int main() {
    try {
        test_singleton();
        test_singleton_thread_safety();
        test_basic_transaction_lifecycle();
        test_timestamp_read_after_write();
        test_timestamp_write_after_read();
        test_concurrent_transactions();
        test_invalid_transaction_id();
        test_switch_algorithm();
        test_twoPL_cc();
        test_twoPL_abort();
        test_twoPL_deadlock();
        test_twoPL_deadlock_abort();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}

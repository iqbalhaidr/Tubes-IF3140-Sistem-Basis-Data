// TODO: Tambahkan unit test untuk Concurrency Control Manager
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "concurrency_control.h"

using namespace mdbms;
using namespace mdbms::ccm;

int main() {
    bool all_passed = true;

    // TEST 1: Singleton Pattern
    std::cout << "\n--- TEST 1: Singleton Pattern ---" << std::endl;
    auto& ccm1 = ConcurrencyControlManager::get_instance("timestamp");
    auto& ccm2 = ConcurrencyControlManager::get_instance();

    if (&ccm1 != &ccm2) {
        std::cerr << "[FAIL] Singleton instance tidak sama" << std::endl;
        all_passed = false;
    } else {
        std::cout << "[PASS] Singleton instance konsisten" << std::endl;
    }

    // TEST 2: Thread Safety - Multiple threads mengakses singleton
    std::cout << "\n--- TEST 2: Thread Safety Singleton ---" << std::endl;
    const int NUM_THREADS = 10;
    std::vector<std::thread> threads;
    std::vector<ConcurrencyControlManager*> instances(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([&instances, i]() {
            auto& ccm = ConcurrencyControlManager::get_instance();
            instances[i] = &ccm;
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    bool all_same = true;
    for (int i = 1; i < NUM_THREADS; i++) {
        if (instances[0] != instances[i]) {
            all_same = false;
            break;
        }
    }

    if (!all_same) {
        std::cerr << "[FAIL] Thread safety singleton gagal" << std::endl;
        all_passed = false;
    } else {
        std::cout << "[PASS] Semua thread mendapat instance yang sama" << std::endl;
    }

    // TEST 3: Basic Transaction Lifecycle
    std::cout << "\n--- TEST 3: Basic Transaction Lifecycle ---" << std::endl;
    auto& ccm = ConcurrencyControlManager::get_instance("timestamp");

    int txn_id = ccm.begin_transaction();
    if (txn_id <= 0) {
        std::cerr << "[FAIL] Transaction ID tidak valid" << std::endl;
        all_passed = false;
    }

    Row row;
    row.table_name = "test_table";
    row.row_id = 1;

    try {
        ccm.log_object(row, txn_id);

        Response read_response = ccm.validate_object(row, txn_id, Action::READ);
        if (!read_response.allowed) {
            std::cerr << "[FAIL] READ operation gagal" << std::endl;
            all_passed = false;
        }

        Response write_response = ccm.validate_object(row, txn_id, Action::WRITE);
        if (!write_response.allowed) {
            std::cerr << "[FAIL] WRITE operation gagal" << std::endl;
            all_passed = false;
        }

        ccm.end_transaction(txn_id);
        std::cout << "[PASS] Transaction lifecycle berhasil" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] Exception: " << e.what() << std::endl;
        all_passed = false;
    }

    // TEST 4: Timestamp Ordering - Read after Write conflict
    std::cout << "\n--- TEST 4: Read After Write Conflict ---" << std::endl;
    Row test_row;
    test_row.table_name = "conflict_table";
    test_row.row_id = 100;

    int txn1 = ccm.begin_transaction();
    ccm.log_object(test_row, txn1);
    Response write1 = ccm.validate_object(test_row, txn1, Action::WRITE);
    assert(write1.allowed);
    ccm.end_transaction(txn1);

    int txn2 = ccm.begin_transaction();
    ccm.log_object(test_row, txn2);

    int txn3 = ccm.begin_transaction();
    ccm.log_object(test_row, txn3);
    Response write3 = ccm.validate_object(test_row, txn3, Action::WRITE);
    assert(write3.allowed);
    ccm.end_transaction(txn3);

    Response read2 = ccm.validate_object(test_row, txn2, Action::READ);
    if (read2.allowed) {
        std::cerr << "[FAIL] Read seharusnya gagal karena timestamp conflict" << std::endl;
        all_passed = false;
    } else {
        std::cout << "[PASS] Timestamp ordering read conflict terdeteksi" << std::endl;
    }
    ccm.end_transaction(txn2);

    // TEST 5: Timestamp Ordering - Write after Read conflict
    std::cout << "\n--- TEST 5: Write After Read Conflict ---" << std::endl;
    Row test_row2;
    test_row2.table_name = "conflict_table";
    test_row2.row_id = 200;

    int txn_a = ccm.begin_transaction();
    ccm.log_object(test_row2, txn_a);
    Response read_a = ccm.validate_object(test_row2, txn_a, Action::READ);
    assert(read_a.allowed);

    int txn_b = ccm.begin_transaction();
    ccm.end_transaction(txn_a);

    int txn_c = ccm.begin_transaction();
    ccm.log_object(test_row2, txn_c);
    Response read_c = ccm.validate_object(test_row2, txn_c, Action::READ);
    assert(read_c.allowed);
    ccm.end_transaction(txn_c);

    ccm.log_object(test_row2, txn_b);
    Response write_b = ccm.validate_object(test_row2, txn_b, Action::WRITE);
    if (write_b.allowed) {
        std::cerr << "[FAIL] Write seharusnya gagal karena timestamp conflict" << std::endl;
        all_passed = false;
    } else {
        std::cout << "[PASS] Timestamp ordering write conflict terdeteksi" << std::endl;
    }
    ccm.end_transaction(txn_b);

    // TEST 6: Multiple Concurrent Transactions
    std::cout << "\n--- TEST 6: Concurrent Transactions ---" << std::endl;
    const int NUM_CLIENTS = 5;
    const int OPS_PER_CLIENT = 10;
    std::vector<std::thread> clients;
    std::atomic<int> successful_txns{0};
    std::atomic<int> failed_txns{0};

    auto client_operations = [&](int client_id) {
        for (int i = 0; i < OPS_PER_CLIENT; i++) {
            int tid = ccm.begin_transaction();

            Row client_row;
            client_row.table_name = "shared_table";
            client_row.row_id = client_id;

            ccm.log_object(client_row, tid);
            Response r_resp = ccm.validate_object(client_row, tid, Action::READ);

            if (r_resp.allowed) {
                Response w_resp = ccm.validate_object(client_row, tid, Action::WRITE);
                if (w_resp.allowed) {
                    successful_txns++;
                } else {
                    failed_txns++;
                }
            } else {
                failed_txns++;
            }

            ccm.end_transaction(tid);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    };

    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients.emplace_back(client_operations, i);
    }

    for (auto& client : clients) {
        client.join();
    }

    int total_ops = successful_txns + failed_txns;
    std::cout << "Total operasi: " << total_ops << std::endl;
    std::cout << "Sukses: " << successful_txns << std::endl;
    std::cout << "Gagal: " << failed_txns << std::endl;

    if (total_ops != NUM_CLIENTS * OPS_PER_CLIENT) {
        std::cerr << "[FAIL] Total operasi tidak sesuai" << std::endl;
        all_passed = false;
    } else if (successful_txns == 0) {
        std::cerr << "[FAIL] Tidak ada transaksi yang sukses" << std::endl;
        all_passed = false;
    } else {
        std::cout << "[PASS] Concurrent transactions berhasil" << std::endl;
    }

    // TEST 7: Invalid Transaction ID
    std::cout << "\n--- TEST 7: Invalid Transaction ID ---" << std::endl;
    Row invalid_row;
    invalid_row.table_name = "test_table";
    invalid_row.row_id = 999;

    bool exception_caught = false;
    try {
        ccm.log_object(invalid_row, 999999);
    } catch (const std::runtime_error&) {
        exception_caught = true;
    }

    if (!exception_caught) {
        std::cerr << "[FAIL] Exception untuk invalid transaction ID tidak tertangkap" << std::endl;
        all_passed = false;
    } else {
        std::cout << "[PASS] Invalid transaction ID terdeteksi" << std::endl;
    }

    // TEST 8: Switch Algorithm
    std::cout << "\n--- TEST 8: Switch Algorithm ---" << std::endl;
    try {
        ccm.switch_algorithm("timestamp");
        std::cout << "[PASS] Switch ke timestamp berhasil" << std::endl;
    } catch (...) {
        std::cerr << "[FAIL] Switch ke timestamp gagal" << std::endl;
        all_passed = false;
    }

    bool unsupported_caught = false;
    try {
        ccm.switch_algorithm("unsupported_algorithm");
    } catch (const std::invalid_argument&) {
        unsupported_caught = true;
    }

    if (!unsupported_caught) {
        std::cerr << "[FAIL] Algoritma tidak didukung seharusnya throw exception" << std::endl;
        all_passed = false;
    } else {
        std::cout << "[PASS] Unsupported algorithm terdeteksi" << std::endl;
    }

    // HASIL AKHIR
    std::cout << "\n========================================" << std::endl;
    if (all_passed) {
        std::cout << "*** SEMUA TES CCM LOLOS! ***" << std::endl;
        return 0;
    } else {
        std::cerr << "*** BEBERAPA TES CCM GAGAL ***" << std::endl;
        return 1;
    }
}

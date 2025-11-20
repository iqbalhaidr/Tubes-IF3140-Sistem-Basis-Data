#include "failure_recovery.h"
#include "types.h"

#include <iostream>
#include <sstream>
#include <cassert>
#include <memory>
#include <functional>

namespace fr = mdbms::fr;
namespace mdbms_ = mdbms;

// --- Mocking Helper: Mengambil Output Konsol ---
std::string capture_output(std::function<void()> func) {
    std::stringstream captured_output;
    auto* original_buf = std::cout.rdbuf(captured_output.rdbuf());
    func();
    std::cout.rdbuf(original_buf);
    return captured_output.str();
}

void test_singleton_access() {
    std::cout << "Test 1: Akses Singleton dan Verifikasi Unik" << std::endl;
    fr::FailureRecoveryManager& instance1 = fr::FailureRecoveryManager::get_instance();
    fr::FailureRecoveryManager& instance2 = fr::FailureRecoveryManager::get_instance();

    // Pastikan kedua referensi menunjuk ke objek yang sama
    assert(&instance1 == &instance2);
    std::cout << "[PASS] Singleton terimplementasi dengan benar." << std::endl;
}

void test_write_log_for_control_queries() {
    std::cout << "\nTest 2: write_log untuk BEGIN/COMMIT/ABORT" << std::endl;
    fr::FailureRecoveryManager& frm = fr::FailureRecoveryManager::get_instance();

    // 2a. Test BEGIN TRANSACTION
    mdbms_::ExecutionResult begin_res;
    begin_res.success = true;
    begin_res.transaction_id = 10;
    begin_res.query = "BEGIN TRANSACTION";

    std::string output_begin = capture_output([&]() {
        frm.write_log(begin_res);
    });

    assert(output_begin.find("FRM: Log ID 1") != std::string::npos);
    assert(output_begin.find("Op: BEGIN") != std::string::npos);
    std::cout << "[PASS] BEGIN TRANSACTION berhasil di-log dengan ID 1." << std::endl;
    
    // 2b. Test INSERT Query
    mdbms_::ExecutionResult insert_res;
    insert_res.success = true;
    insert_res.transaction_id = 11;
    insert_res.query = "INSERT INTO Student VALUES (1, 'Budi')";
    
    // Buat data Row dummy untuk di-log sebagai new_value
    mdbms_::Row dummy_row;
    dummy_row.table_name = "Student";
    dummy_row.columns["StudentID"] = 1;
    insert_res.data = mdbms_::Rows<mdbms_::Row>({dummy_row});

    std::string output_insert = capture_output([&]() {
        frm.write_log(insert_res);
    });

    assert(output_insert.find("FRM: Log ID 2") != std::string::npos);
    assert(output_insert.find("Op: INSERT") != std::string::npos);
    std::cout << "[PASS] INSERT berhasil di-log dengan ID 2 dan Op: INSERT." << std::endl;
    
    // 2c. Test FAILED Query (seharusnya diabaikan)
    mdbms_::ExecutionResult failed_res;
    failed_res.success = false;
    failed_res.transaction_id = 12;
    failed_res.query = "DELETE FROM NonExistingTable";
    
    std::string output_failed = capture_output([&]() {
        frm.write_log(failed_res);
    });
    
    assert(output_failed.find("Mengabaikan log karena query gagal") != std::string::npos);
    std::cout << "[PASS] Query gagal diabaikan." << std::endl;
}


int main() {
    std::cout << "--- Menjalankan Unit Test Failure Recovery Manager ---\n";
    test_singleton_access();
    test_write_log_for_control_queries();
    
    // Membersihkan log buffer dan menulis ke file (Anggota 1)
    fr::FailureRecoveryManager::get_instance().save_checkpoint();

    std::cout << "\n*** SEMUA TES FRM LOLOS! ***" << std::endl;
    return 0;
}
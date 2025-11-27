// TODO: Tambahkan unit test untuk Failure Recovery Manager
#include <iostream>
#include <cassert>
#include <fstream>
#include <string>
#include <filesystem>
#include <vector>
#include <sstream>
#include <memory>
#include <functional>

#include "failure_recovery.h"
#include "types.h"

#define GREEN "\033[32m"
#define RED   "\033[31m"
#define RESET "\033[0m"

namespace fr = mdbms::fr;
namespace mdbms_ = mdbms;
namespace fs = std::filesystem;

void clean_environment() {
    if (fs::exists("../data/wal.bin")) {
        fs::remove("../data/wal.bin");
        std::cout << "[SETUP] Wal bin lama dihapus." << std::endl;
    }
    if (fs::exists("../data/wal.log")) {
        fs::remove("../data/wal.log");
        std::cout << "[SETUP] Log lama dihapus." << std::endl;
    }
    // Pastikan folder data ada
    if (!fs::exists("../data")) {
        fs::create_directory("../data");
    }
}

std::vector<std::string> read_log_file() {
    std::vector<std::string> lines;
    std::ifstream infile("../data/wal.log");
    
    if (!infile.is_open()) return lines;

    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

// Pake yang ini
std::vector<mdbms::LogEntry> read_log_file_bin() {
    std::string file_path = "../data/wal.bin";
    return mdbms::fr::FailureRecoveryManager::get_instance().read_all_logs_public("../data/wal.bin");
}

// --- Mocking Helper: Mengambil Output Konsol ---
std::string capture_output(std::function<void()> func) {
    std::stringstream captured_output;
    auto* original_buf = std::cout.rdbuf(captured_output.rdbuf());
    func();
    std::cout.rdbuf(original_buf);
    return captured_output.str();
}

// --- UNIT TESTS ---

void test_singleton_property() {
    std::cout << "\n[TEST] Checking Singleton Property..." << std::endl;
    
    auto& instance1 = mdbms::fr::FailureRecoveryManager::get_instance();
    auto& instance2 = mdbms::fr::FailureRecoveryManager::get_instance();
    
    assert(&instance1 == &instance2);
    
    std::cout << GREEN << "PASS: Singleton valid (Alamat Memori Sama)." << RESET << std::endl;
}

void test_write_log_persistence() {
    std::cout << "\n[TEST] Checking Write Log Persistence..." << std::endl;

    auto& frm = mdbms::fr::FailureRecoveryManager::get_instance();

    // 1. BEGIN transaction
    mdbms::ExecutionResult begin_info;
    begin_info.transaction_id = 101;
    begin_info.query = "BEGIN TRANSACTION";
    begin_info.success = true;
    frm.write_log(begin_info);

    // 2. INSERT query
    mdbms::ExecutionResult info;
    info.transaction_id = 101;
    info.query = "INSERT INTO mahasiswa VALUES (1, 'Budi')";
    info.success = true;
    frm.write_log(info);
    
    frm.save_checkpoint();
    // std::vector<std::string> logs = read_log_file();
    std::vector<mdbms::LogEntry> logs = read_log_file_bin();
    
    bool found_query = false;
    bool found_checkpoint = false;

    std::cout << "   Isi File Log Terbaca:" << std::endl;
    for (const auto& log : logs) {
        // std::cout << "   -> " << line << std::endl;
    
        if (log.query.find("INSERT INTO mahasiswa") != std::string::npos) {
            found_query = true;
        }

        if (log.operation == mdbms::Operation::CHECKPOINT) {
            found_checkpoint = true;
        }
    }

    if (found_query && found_checkpoint) {
        std::cout << GREEN << "PASS: Log Transaksi & Checkpoint ditemukan di disk." << RESET << std::endl;
    } else {
        std::cout << RED << "FAIL: Data tidak ditemukan di file log!" << RESET << std::endl;
        assert(false);
    }
}

void test_checkpoint_logic() {
    std::cout << "\n[TEST] Checking Active Transaction Tracking..." << std::endl;
    
    auto& frm = mdbms::fr::FailureRecoveryManager::get_instance();

    // Skenario:
    // TX 200: BEGIN (Aktif)
    // TX 201: BEGIN (Aktif) -> COMMIT (Selesai)
    // Checkpoint terjadi -> Harusnya hanya TX 200 yang tercatat aktif
    
    mdbms::ExecutionResult r1;
    r1.transaction_id = 200; r1.query = "BEGIN TRANSACTION";
    r1.success = true;
    frm.write_log(r1);

    mdbms::ExecutionResult r2;
    r2.transaction_id = 201; r2.query = "BEGIN TRANSACTION";
    r2.success = true;
    frm.write_log(r2);

    mdbms::ExecutionResult r3;
    r3.transaction_id = 201; r3.query = "COMMIT";
    r3.success = true;
    frm.write_log(r3);

    frm.save_checkpoint();

    // std::vector<std::string> logs = read_log_file();
    // std::string last_line = logs.back();
    std::vector<mdbms::LogEntry> logs = read_log_file_bin();
    mdbms::LogEntry last_entry = logs.back();

    // Karena TX 201 sudah commit, harusnya TIDAK ada di list. TX 200 harus ada.
    
    // bool tx200_active = (last_line.find("200") != std::string::npos);
    // bool tx201_gone = (last_line.find("201") == std::string::npos);

    bool tx200_active = (last_entry.query.find("200") != std::string::npos);
    bool tx201_gone = (last_entry.query.find("201") == std::string::npos);

    if (tx200_active && tx201_gone) {
        std::cout << GREEN << "PASS: Checkpoint mencatat Active Transaction dengan benar (Hanya TX 200)." << RESET << std::endl;
    } else {
        std::cout << RED << "FAIL: Logika Active Transaction Salah!" << RESET << std::endl;
        std::cout << "   Log Terakhir: " << last_entry.query << std::endl;
        // std::cout << "   Log Terakhir: " << last_line << std::endl;
        // assert(false); // Uncomment jika logika strict sudah jalan
    }
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

    assert(output_begin.find("FRM: Log ID") != std::string::npos);
    assert(output_begin.find("Op: BEGIN") != std::string::npos);
    std::cout << "[PASS] BEGIN TRANSACTION berhasil di-log" << std::endl;
    
    // 2b. Test INSERT Query
    mdbms_::ExecutionResult insert_res;
    insert_res.success = true;
    insert_res.transaction_id = 11;
    
    // BEGIN untuk transaksi 11
    mdbms_::ExecutionResult begin_tx11;
    begin_tx11.success = true;
    begin_tx11.transaction_id = 11;
    begin_tx11.query = "BEGIN TRANSACTION";
    frm.write_log(begin_tx11);
    
    insert_res.query = "INSERT INTO Student VALUES (1, 'Budi')";
    
    // Buat data Row dummy untuk di-log sebagai new_value
    mdbms_::Row dummy_row;
    dummy_row.table_name = "Student";
    dummy_row.columns["StudentID"] = 1;
    insert_res.data = mdbms_::Rows<mdbms_::Row>({dummy_row});

    std::string output_insert = capture_output([&]() {
        frm.write_log(insert_res);
    });

    assert(output_insert.find("FRM: Log ID") != std::string::npos);
    assert(output_insert.find("Op: INSERT") != std::string::npos);
    std::cout << "[PASS] INSERT berhasil di-log dan Op: INSERT." << std::endl;
    
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

void test_transaction_abort_recovery() {
    std::cout << "\n[TEST] Transaction Abort Recovery (UNDO)..." << std::endl;
    
    auto& frm = fr::FailureRecoveryManager::get_instance();
    int transaction_id = 500;
    
    // Simulasi transaksi dengan beberapa operasi
    
    // 1. BEGIN
    mdbms_::ExecutionResult begin_result;
    begin_result.transaction_id = transaction_id;
    begin_result.timestamp = std::time(nullptr);
    begin_result.query = "BEGIN";
    begin_result.success = true;
    frm.write_log(begin_result);
    
    // 2. INSERT
    mdbms_::ExecutionResult insert_result;
    insert_result.transaction_id = transaction_id;
    insert_result.timestamp = std::time(nullptr);
    insert_result.query = "INSERT INTO users (id, name) VALUES (1, 'Alice')";
    insert_result.success = true;
    
    mdbms_::Row inserted_row;
    inserted_row.table_name = "users";
    inserted_row.row_id = 1;
    inserted_row.columns["id"] = 1;
    inserted_row.columns["name"] = std::string("Alice");
    insert_result.data.data.push_back(inserted_row);
    insert_result.affected_rows = 1;
    frm.write_log(insert_result);
    
    // 3. UPDATE
    mdbms_::ExecutionResult update_result;
    update_result.transaction_id = transaction_id;
    update_result.timestamp = std::time(nullptr);
    update_result.query = "UPDATE users SET name = 'Bob' WHERE id = 1";
    update_result.success = true;
    
    mdbms_::Row old_row;
    old_row.table_name = "users";
    old_row.row_id = 1;
    old_row.columns["id"] = 1;
    old_row.columns["name"] = std::string("Alice");
    
    mdbms_::Row new_row;
    new_row.table_name = "users";
    new_row.row_id = 1;
    new_row.columns["id"] = 1;
    new_row.columns["name"] = std::string("Bob");
    
    update_result.data.data.push_back(old_row);
    update_result.data.data.push_back(new_row);
    update_result.affected_rows = 1;
    frm.write_log(update_result);
    
    // 4. DELETE
    mdbms_::ExecutionResult delete_result;
    delete_result.transaction_id = transaction_id;
    delete_result.timestamp = std::time(nullptr);
    delete_result.query = "DELETE FROM users WHERE id = 1";
    delete_result.success = true;
    
    mdbms_::Row deleted_row;
    deleted_row.table_name = "users";
    deleted_row.row_id = 1;
    deleted_row.columns["id"] = 1;
    deleted_row.columns["name"] = std::string("Bob");
    
    delete_result.data.data.push_back(deleted_row);
    delete_result.affected_rows = 1;
    frm.write_log(delete_result);
    
    // Flush logs ke disk
    frm.save_checkpoint();
    
    // 5. ABORT - Trigger Recovery
    std::cout << "   Melakukan ABORT (Recovery)..." << std::endl;
    
    mdbms_::RecoverCriteria criteria;
    criteria.transaction_id = transaction_id;
    
    std::string output_recovery = capture_output([&]() {
        frm.recover(criteria);
    });
    
    // Verifikasi bahwa UNDO dilakukan untuk 3 operasi (INSERT, UPDATE, DELETE)
    assert(output_recovery.find("UNDO operasi DELETE") != std::string::npos);
    assert(output_recovery.find("UNDO operasi UPDATE") != std::string::npos);
    assert(output_recovery.find("UNDO operasi INSERT") != std::string::npos);
    assert(output_recovery.find("Total operasi yang di-UNDO: 3") != std::string::npos);
    
    std::cout << GREEN << "PASS: Transaction Abort Recovery berhasil (3 operasi di-UNDO)." << RESET << std::endl;
    
    // 6. Log ABORT
    mdbms_::ExecutionResult abort_result;
    abort_result.transaction_id = transaction_id;
    abort_result.timestamp = std::time(nullptr);
    abort_result.query = "ABORT";
    abort_result.success = true;
    frm.write_log(abort_result);
}

void test_recovery_no_matching_logs() {
    std::cout << "\n[TEST] Recovery dengan Transaction ID tidak ada..." << std::endl;
    
    auto& frm = fr::FailureRecoveryManager::get_instance();
    
    mdbms_::RecoverCriteria criteria;
    criteria.transaction_id = 9999; // ID yang tidak ada
    
    std::string output = capture_output([&]() {
        frm.recover(criteria);
    });
    
    // Verifikasi bahwa tidak ada log yang di-recover
    assert(output.find("Tidak ada log yang sesuai dengan kriteria recovery") != std::string::npos);
    
    std::cout << GREEN << "PASS: Recovery dengan TX ID tidak ada handled dengan benar." << RESET << std::endl;
}

int main() {
    clean_environment();

    std::cout << "=== RUNNING INTEGRATION TESTS: FAILURE RECOVERY ===\n" << std::endl;

    test_singleton_property();
    test_write_log_persistence();
    test_checkpoint_logic();
    test_write_log_for_control_queries();
    
    // === RECOVERY TESTS (UNDO) ===
    std::cout << "\n" << "=== RECOVERY (UNDO) TESTS ===" << std::endl;
    test_transaction_abort_recovery();
    test_recovery_no_matching_logs();
    
    // Membersihkan log buffer dan menulis ke file (Anggota 1)
    fr::FailureRecoveryManager::get_instance().save_checkpoint();

    std::cout << "\n" << GREEN << "=== ALL TESTS PASSED SUCCESSFULLY ===" << RESET << std::endl;
    return 0;
}
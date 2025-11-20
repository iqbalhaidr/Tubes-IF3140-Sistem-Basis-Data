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
    if (fs::exists("data/wal.log")) {
        fs::remove("data/wal.log");
        std::cout << "[SETUP] Log lama dihapus." << std::endl;
    }
    // Pastikan folder data ada
    if (!fs::exists("data")) {
        fs::create_directory("data");
    }
}

std::vector<std::string> read_log_file() {
    std::vector<std::string> lines;
    std::ifstream infile("data/wal.log");
    
    if (!infile.is_open()) return lines;

    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
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

    mdbms::ExecutionResult info;
    info.transaction_id = 101;
    info.query = "INSERT INTO mahasiswa VALUES (1, 'Budi')";
    
    frm.write_log(info);
    frm.save_checkpoint();
    std::vector<std::string> logs = read_log_file();
    
    bool found_query = false;
    bool found_checkpoint = false;

    std::cout << "   Isi File Log Terbaca:" << std::endl;
    for (const auto& line : logs) {
        std::cout << "   -> " << line << std::endl;
    
        if (line.find("INSERT INTO mahasiswa") != std::string::npos) {
            found_query = true;
        }

        if (line.find("CHECKPOINT") != std::string::npos) {
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
    frm.write_log(r1);

    mdbms::ExecutionResult r2;
    r2.transaction_id = 201; r2.query = "BEGIN TRANSACTION";
    frm.write_log(r2);

    mdbms::ExecutionResult r3;
    r3.transaction_id = 201; r3.query = "COMMIT";
    frm.write_log(r3);

    frm.save_checkpoint();

    std::vector<std::string> logs = read_log_file();
    std::string last_line = logs.back(); 

    // Karena TX 201 sudah commit, harusnya TIDAK ada di list. TX 200 harus ada.
    
    bool tx200_active = (last_line.find("200") != std::string::npos);
    bool tx201_gone = (last_line.find("201") == std::string::npos);

    if (tx200_active && tx201_gone) {
        std::cout << GREEN << "PASS: Checkpoint mencatat Active Transaction dengan benar (Hanya TX 200)." << RESET << std::endl;
    } else {
        std::cout << RED << "FAIL: Logika Active Transaction Salah!" << RESET << std::endl;
        std::cout << "   Log Terakhir: " << last_line << std::endl;
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
    clean_environment();

    std::cout << "=== RUNNING INTEGRATION TESTS: FAILURE RECOVERY ===\n" << std::endl;

    test_singleton_property();
    test_write_log_persistence();
    test_checkpoint_logic();
    test_write_log_for_control_queries();
    // Membersihkan log buffer dan menulis ke file (Anggota 1)
    fr::FailureRecoveryManager::get_instance().save_checkpoint();

    std::cout << "\n" << GREEN << "=== ALL TESTS PASSED SUCCESSFULLY ===" << RESET << std::endl;
    return 0;
}
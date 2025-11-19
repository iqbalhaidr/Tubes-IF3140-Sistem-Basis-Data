#include "failure_recovery.h"
#include <iostream>
#include <fstream>

namespace mdbms::fr {

FailureRecoveryManager& FailureRecoveryManager::get_instance() {
    static FailureRecoveryManager instance;
    return instance;
}

FailureRecoveryManager::FailureRecoveryManager() {
    this->next_log_id = 1;
    this->next_checkpoint_id = 1;
    this->log_file_path = "data/wal.log";
    std::cout << "FRM: Konstruktor FailureRecoveryManager dipanggil (stub)..." << std::endl;
}

FailureRecoveryManager::~FailureRecoveryManager() {
    std::cout << "FRM: Destruktor FailureRecoveryManager dipanggil (stub)..." << std::endl;
    std::lock_guard<std::mutex> lock(this->mtx);
    flush_buffer();
    std::cout << "FRM: Checkpoint terakhir disimpan sebelum keluar (stub)..." << std::endl;
}

void FailureRecoveryManager::write_log(const ExecutionResult& info) {
    std::cout << "FRM: Menulis log untuk query: " << info.query << " (stub)..." << std::endl;
}

void FailureRecoveryManager::save_checkpoint() {
    std::cout << "FRM: Menyimpan checkpoint (stub)..." << std::endl;
    std::lock_guard<std::mutex> lock(this->mtx);
    flush_buffer();
}

void FailureRecoveryManager::recover(const RecoverCriteria&) {
    std::cout << "FRM: Melakukan recovery (stub)..." << std::endl;
}

void FailureRecoveryManager::flush_buffer() {
    if (this->log_buffer.empty()) {
            return;
        }

    std::ofstream log_file(this->log_file_path, std::ios::app);

    if (!log_file.is_open()) {
        std::cerr << "FRM: Gagal membuka file log untuk penulisan: " << this->log_file_path << std::endl;
        return;
    }

    int count = 0;
    for (const auto& entry : this->log_buffer) {
        // TODO (Anggota 2): Ganti dengan serialize_log(entry)
        // Format Sementara: ID,TransactionID,Table
        log_file << entry.log_id << "," 
                    << entry.transaction_id << "," 
                    << entry.table_name << "," 
                    << "PENDING_SERIALIZER" << "\n";
        count++;
    }

    log_file.close();
    this->log_buffer.clear();
    std::cout << "FRM: Flush log buffer ke file " << this->log_file_path << " (stub)..." << std::endl;
}

} // namespace mdbms::fr

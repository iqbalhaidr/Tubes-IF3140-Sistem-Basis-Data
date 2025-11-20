#include "failure_recovery.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace mdbms::fr {

//Helper function to determine operation type from query string
Operation determine_operation_type(const std::string& query) {
    if (query.empty()) {
        return Operation::BEGIN;
    }

    std::istringstream iss(query);
    std::string first_word;
    iss >> first_word;

    std::transform(first_word.begin(), first_word.end(), first_word.begin(), ::toupper);

    if (first_word == "BEGIN") {
        return Operation::BEGIN;
    }
    if (first_word == "COMMIT") {
        return Operation::COMMIT;
    }
    if (first_word == "ROLLBACK" || first_word == "ABORT") {
        return Operation::ABORT;
    }
    if (first_word == "INSERT") {
        return Operation::INSERT;
    }
    if (first_word == "UPDATE") {
        return Operation::UPDATE;
    }
    if (first_word == "DELETE") {
        return Operation::DELETE;
    }
    // SELECT yang berhasil di-log sebagai COMMIT non-data
    if (first_word == "SELECT") {
        return Operation::COMMIT;
    }

    return Operation::BEGIN;
}

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
    if (!info.success) {
        std::cout << "FRM: Mengabaikan log karena query gagal (T" << info.transaction_id << ")" << std::endl;
        return;
    }

    // 1. Inisialisasi LogEntry dan tentukan tipe operasi
    LogEntry entry;
    entry.transaction_id = info.transaction_id;
    entry.timestamp = info.timestamp;
    entry.query = info.query;
    entry.operation = determine_operation_type(info.query);

    // 2. Tentukan Old Value dan New Value
    if (entry.operation == Operation::INSERT || entry.operation == Operation::UPDATE ||
        entry.operation == Operation::DELETE) {
        
        if (!info.data.data.empty()) {
            entry.table_name = info.data.data[0].table_name;
        }

        if (entry.operation == Operation::INSERT) {
            entry.old_value = Row(); 
            if (!info.data.data.empty()) {
                entry.new_value = info.data.data[0];
            }
        } else if (entry.operation == Operation::UPDATE) {
            // UPDATE: Old value harus diambil QP/SM. New value adalah data yang telah di-update.
            // CATATAN: Perlu koordinasi dengan QP/SM agar ExecutionResult membawa old_value.
            entry.old_value = Row(); // Placeholder/Butuh Old Value 
            //TODO: Integrasi dengan QP/SM untuk mendapatkan old_value
            if (!info.data.data.empty()) {
                entry.new_value = info.data.data[0];
            }
        } else if (entry.operation == Operation::DELETE) {
            if (!info.data.data.empty()) {
                entry.old_value = info.data.data[0];
            }
            entry.new_value = Row();
        }
    } else {
        // Untuk BEGIN, COMMIT, ABORT, dan SELECT (Commit non-data): kosongkan field data
        entry.table_name.clear();
        entry.old_value = Row();
        entry.new_value = Row();
    }


    // 3. Masukkan ke log_buffer (menggunakan Mutex dan ID)
    {
        // Kunci mutex untuk akses thread-safe
        std::lock_guard<std::mutex> lock(this->mtx);
        
        entry.log_id = this->next_log_id++;
        this->log_buffer.push_back(entry);
    }

    // Output Debugging
    std::cout << "FRM: Log ID " << entry.log_id << " untuk T" << entry.transaction_id
              << " (" << entry.query.substr(0, std::min((int)entry.query.size(), 30))
              << (entry.query.size() > 30 ? "..." : "") << ") ditambahkan ke buffer. Op: "
              << (entry.operation == Operation::BEGIN ? "BEGIN" :
                  entry.operation == Operation::COMMIT ? "COMMIT" :
                  entry.operation == Operation::ABORT ? "ABORT" :
                  entry.operation == Operation::INSERT ? "INSERT" :
                  entry.operation == Operation::UPDATE ? "UPDATE" :
                  entry.operation == Operation::DELETE ? "DELETE" : "UNKNOWN")
              << std::endl;
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
        // TODO: Ganti dengan serialize_log(entry)
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

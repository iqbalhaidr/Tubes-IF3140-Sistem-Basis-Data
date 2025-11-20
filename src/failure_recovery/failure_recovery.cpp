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
    save_checkpoint();
    std::cout << "FRM: Checkpoint terakhir disimpan sebelum keluar (stub)..." << std::endl;
}

void FailureRecoveryManager::write_log(const ExecutionResult& info) {
    if (!info.success) {
        std::cout << "FRM: Mengabaikan log karena query gagal (T" << info.transaction_id << ")" << std::endl;
        return;
    }

    std::cout << "FRM: Menulis log untuk query: " << info.query << " (stub)..." << std::endl;
    std::lock_guard<std::mutex> lock(this->mtx);

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
    } else if (entry.operation == Operation::BEGIN) {
        this->active_transactions_cache.insert(entry.transaction_id);
        // Debug
        std::cout << "FRM: Transaksi " << entry.transaction_id << " dimulai." << std::endl;
    } else if (entry.operation == Operation::COMMIT || entry.operation == Operation::ABORT) {
        this->active_transactions_cache.erase(entry.transaction_id);
        // Debug
        std::cout << "FRM: Transaksi " << entry.transaction_id << " selesai." << std::endl;
    } else {
        // SELECT (Commit non-data): kosongkan field data
        entry.table_name.clear();
        entry.old_value = Row();
        entry.new_value = Row();
    }

    this->log_buffer.push_back(entry);

    if (this->log_buffer.size() >= this->MAX_BUFFER_SIZE) {
        std::cout << "FRM: Log buffer mencapai kapasitas maksimum. Melakukan flush..." << std::endl;
        flush_buffer();
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

    // Menambahkan entri log untuk checkpoint
    LogEntry checkpoint_entry;
    checkpoint_entry.log_id = this->next_log_id++;
    checkpoint_entry.transaction_id = -1;
    checkpoint_entry.timestamp = std::time(nullptr);
    checkpoint_entry.operation = Operation::CHECKPOINT;
    checkpoint_entry.table_name = "SYSTEM";

    std::string active_transactions_str = "[";
    bool first = true;
    for (int tid : this->active_transactions_cache) {
        if (!first) active_transactions_str += ",";
        active_transactions_str += std::to_string(tid);
        first = false;
    }
    active_transactions_str += "]";
    checkpoint_entry.query = "CHECKPOINT_L:" + active_transactions_str;

    this->log_buffer.push_back(checkpoint_entry);
    flush_buffer();

    // Menyimpan informasi checkpoint
    CheckpointInfo checkpoint;
    checkpoint.checkpoint_id = this->next_checkpoint_id++;
    checkpoint.timestamp = std::time(nullptr);

    for (int tid : this->active_transactions_cache) {
        checkpoint.active_transactions.push_back(tid);
    }

    this->checkpoints.push_back(checkpoint);
    std::cout << "FRM: Checkpoint " << checkpoint.checkpoint_id << " disimpan dengan " << checkpoint.active_transactions.size() << " transaksi aktif." << std::endl;
}

void FailureRecoveryManager::recover(const RecoverCriteria&) {
    std::cout << "FRM: Melakukan recovery (stub)..." << std::endl;
}

std::string FailureRecoveryManager::any_to_string(const std::any& value) {
    if (!value.has_value()) return "NULL";

    try {
        if (value.type() == typeid(int)) {
            return std::to_string(std::any_cast<int>(value));
        } else if (value.type() == typeid(float)) {
            return std::to_string(std::any_cast<float>(value));
        } else if (value.type() == typeid(std::string)) {
            return std::any_cast<std::string>(value);
        }
    } catch (...) {
        return "ERROR";
    }
    return "UNKNOWN_TYPE";
}

std::any FailureRecoveryManager::string_to_any(const std::string& str, DataType type) {
    if (type == DataType::INTEGER) return std::stoi(str);
    if (type == DataType::FLOAT) return std::stof(str);
    return str;
}

std::string FailureRecoveryManager::row_to_string(const Row& row) {
    if (row.row_id == -1 && row.columns.empty()) return "EMPTY"; 
    
    std::stringstream ss;

    ss << row.row_id << "#";

    ss << "{";
    bool first = true;
    for (const auto& [atr, val] : row.columns) {
        if (!first) ss << ",";
        ss << atr << ":" << any_to_string(val);
        first = false;
    }
    ss << "}";

    return ss.str();
}

Row FailureRecoveryManager::string_to_row(const std::string& row_string, const std::string& table_name) {
    Row row;
    row.table_name = table_name;

    if (row_string == "EMPTY") {
        row.row_id = -1;
        return row;
    }

    std::stringstream ss(row_string);
    std::string segment;
    std::vector<std::string> parts;

    size_t delimiter_pos = row_string.find('#'); // Cari pemisah ID dan Data
    if (delimiter_pos == std::string::npos) {
        row.row_id = -1; 
        return row;
    }

    // Parse Row ID
    std::string id_str = row_string.substr(0, delimiter_pos);
    row.row_id = std::stoi(id_str);

    // Parse Columns
    std::string content = row_string.substr(delimiter_pos + 1);
    content = content.substr(1, content.size() - 2);

    std::stringstream content_ss(content);
    std::string pair_str;
    
    while (std::getline(content_ss, pair_str, ',')) {
        size_t kv_sep = pair_str.find(':');
        
        if (kv_sep != std::string::npos) {
            std::string atr = pair_str.substr(0, kv_sep);
            std::string val = pair_str.substr(kv_sep + 1);

            row.columns[atr] = string_to_any(val, DataType::VARCHAR);
        }
    }

    return row;
}

std::string FailureRecoveryManager::operation_to_string(Operation op) {
    switch (op) {
        case Operation::BEGIN: return "BEGIN";
        case Operation::COMMIT: return "COMMIT";
        case Operation::ABORT: return "ABORT";
        case Operation::UPDATE: return "UPDATE";
        case Operation::INSERT: return "INSERT";
        case Operation::DELETE: return "DELETE";
        case Operation::CHECKPOINT: return "CHECKPOINT";
        default: return "UNKNOWN";
    }
}

Operation FailureRecoveryManager::string_to_operation(const std::string& str) {
    if (str == "BEGIN") return Operation::BEGIN;
    if (str == "COMMIT") return Operation::COMMIT;
    if (str == "ABORT") return Operation::ABORT;
    if (str == "UPDATE") return Operation::UPDATE;
    if (str == "INSERT") return Operation::INSERT;
    if (str == "DELETE") return Operation::DELETE;
    if (str == "CHECKPOINT") return Operation::CHECKPOINT;
    return Operation::ABORT; // Default safe fallback
}

std::string FailureRecoveryManager::serialize_log(const LogEntry& entry) {
    std::stringstream ss;

    ss << entry.log_id << "|";
    ss << entry.transaction_id << "|";
    ss << static_cast<long long>(entry.timestamp) << "|";
    ss << operation_to_string(entry.operation) << "|";
    ss << (entry.table_name.empty() ? "NON_TABLE" : entry.table_name) << "|";
    ss << row_to_string(entry.old_value) << "|";
    ss << row_to_string(entry.new_value) << "|";
    ss << entry.query;

    return ss.str();
}

LogEntry FailureRecoveryManager::deserialize_log(const std::string& serialized_log) {
    LogEntry entry;

    std::stringstream ss(serialized_log);
    std::string segment;
    std::vector<std::string> parts;

    while (std::getline(ss, segment, '|')) {
        parts.push_back(segment);
    }

    if (parts.size() < 8) {
        std::cerr << "FRM Error: Log korup/tidak lengkap -> " << serialized_log << std::endl;
        entry.log_id = -1;
        return entry;
    }

    try {
        entry.log_id = std::stoi(parts[0]);
        entry.transaction_id = std::stoi(parts[1]);
        entry.timestamp = static_cast<std::time_t>(std::stoll(parts[2]));
        entry.operation = string_to_operation(parts[3]);
        entry.table_name = (parts[4] == "NON_TABLE") ? "" : parts[4];
        entry.old_value = string_to_row(parts[5], entry.table_name);
        entry.new_value = string_to_row(parts[6], entry.table_name);
        entry.query = parts[7];

    } catch (const std::exception& e) {
        std::cerr << "FRM Error: Gagal parsing log entry -> " << e.what() << std::endl;
        entry.log_id = -1;
    }

    return entry;
}

void FailureRecoveryManager::append_log_to_file(const std::string& serialized_log, const std::string& file_path) {
    std::ofstream outfile(file_path, std::ios::app);

    if (!outfile.is_open()) {
        std::cerr << "FRM Critical Error: Gagal membuka file log untuk ditulis -> " << file_path << std::endl;
        return;
    }

    outfile << serialized_log << "\n";
    outfile.flush();
    outfile.close();
}

std::vector<LogEntry> FailureRecoveryManager::read_all_logs(const std::string& file_path) {
    std::vector<LogEntry> logs;
    std::ifstream infile(file_path);

    if (!infile.is_open()) {
        std::cout << "FRM Info: File log tidak ditemukan (" << file_path << "). Memulai dengan log kosong." << std::endl;
        return logs;
    }

    std::string line;
    int line_number = 0;

    while (std::getline(infile, line)) {
        line_number++;

        if (line.empty()) continue;
        LogEntry entry = deserialize_log(line);
        
        if (entry.log_id != -1) {
            logs.push_back(entry);
        } else {
            std::cerr << "FRM Warning: Skipping corrupt entry at line " << line_number << std::endl;
        }
    }

    infile.close();
    std::cout << "FRM: Berhasil memuat " << logs.size() << " entri log dari disk." << std::endl;
    return logs;
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

    for (const auto& entry : this->log_buffer) {
        log_file << serialize_log(entry) << "\n";
    }

    log_file.close();
    this->log_buffer.clear();
    std::cout << "FRM: Flush log buffer ke file " << this->log_file_path << "." << std::endl;
}

} // namespace mdbms::fr

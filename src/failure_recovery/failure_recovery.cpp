#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <filesystem>
#include "failure_recovery.h"

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

FailureRecoveryManager::FailureRecoveryManager() 
    : storage_engine_(sm::StorageEngine::get_instance()) {
    this->next_log_id = 1;
    this->next_checkpoint_id = 1;
    // path untuk menyimpan log file
    this->log_file_path = "../data/wal.bin";
    std::cout << "FRM: Konstruktor FailureRecoveryManager dipanggil..." << std::endl;
    std::cout << "FRM: Log file path: " << this->log_file_path << std::endl;

    recover_from_crash();

    std::vector<LogEntry> existing = read_all_logs(this->log_file_path);
    if (!existing.empty()) {
        this->next_log_id = existing.back().log_id + 1;
    }
}

FailureRecoveryManager::~FailureRecoveryManager() {
    std::cout << "FRM: Destruktor FailureRecoveryManager dipanggil..." << std::endl;
    save_checkpoint();
    std::cout << "FRM: Checkpoint terakhir disimpan sebelum keluar..." << std::endl;
}

void FailureRecoveryManager::write_log(const ExecutionResult& info) {
    if (!info.success) {
        std::cout << "FRM: Mengabaikan log karena query gagal (T" << info.transaction_id << ")" << std::endl;
        return;
    }

    std::cout << "FRM: Menulis log untuk query: " << info.query << "..." << std::endl;
    std::lock_guard<std::mutex> lock(this->mtx);

    // 1. Inisialisasi LogEntry dan tentukan tipe operasi
    LogEntry entry;
    entry.log_id = this->next_log_id++;
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
            // UPDATE: Old value di data[0], new value di data[1]
            // Memastikan Query Processor mengirimkan 2 row (Old dan New Value) per log entry.
            if (info.data.data.size() >= 2) {
                entry.table_name = info.data.data[0].table_name; // Ambil table name dari Old Value
                entry.old_value = info.data.data[0];             // First row is old value
                entry.new_value = info.data.data[1];             // Second row is new value
            } else {
                std::cerr << "FRM Error: Log UPDATE (T" << info.transaction_id 
                          << ") dibuang, tidak memiliki Old dan New Value yang lengkap (data.size() < 2)." << std::endl;
                return;
            }
        } else if (entry.operation == Operation::DELETE) {
            if (!info.data.data.empty()) {
                entry.old_value = info.data.data[0];
            }
            entry.new_value = Row();
        }
    } else if (entry.operation == Operation::BEGIN) {
        this->active_transactions_cache.insert(entry.transaction_id);
        std::cout << "FRM: Transaksi " << entry.transaction_id << " dimulai." << std::endl;
    } else if (entry.operation == Operation::COMMIT || entry.operation == Operation::ABORT) {
        this->active_transactions_cache.erase(entry.transaction_id);
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
    std::cout << "FRM: Menyimpan checkpoint..." << std::endl;
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

void FailureRecoveryManager::recover(const RecoverCriteria& criteria) {
    std::lock_guard<std::mutex> lock(this->mtx);
    
    std::cout << "FRM: Memulai proses recovery..." << std::endl;
    
    // Flush buffer untuk memastikan semua log ada di disk
    flush_buffer();
    
    // Baca semua log dari file
    std::vector<LogEntry> all_logs = read_all_logs(this->log_file_path);
    
    if (all_logs.empty()) {
        std::cout << "FRM: Tidak ada log untuk di-recover." << std::endl;
        return;
    }
    
    // Filter log berdasarkan transaction_id untuk transaction abort recovery
    std::vector<LogEntry> logs_to_recover;
    
    std::cout << "FRM: Recovery untuk transaction ID: " << criteria.transaction_id << std::endl;
    for (const auto& entry : all_logs) {
        if (entry.transaction_id == criteria.transaction_id) {
            logs_to_recover.push_back(entry);
        }
    }
    
    if (logs_to_recover.empty()) {
        std::cout << "FRM: Tidak ada log yang sesuai dengan kriteria recovery." << std::endl;
        return;
    }
    
    std::cout << "FRM: Ditemukan " << logs_to_recover.size() << " log entry untuk di-recover." << std::endl;
    
    // Proses recovery secara backward (dari entri terakhir ke awal)
    int undo_count = 0;
    for (auto it = logs_to_recover.rbegin(); it != logs_to_recover.rend(); ++it) {
        const LogEntry& entry = *it;
        
        // Skip operasi BEGIN, COMMIT, dan ABORT karena gaada efek ke data
        if (entry.operation == Operation::BEGIN || 
            entry.operation == Operation::COMMIT || 
            entry.operation == Operation::ABORT) {
            std::cout << "FRM: Melewati operasi " << operation_to_string(entry.operation) 
                      << " (log_id: " << entry.log_id << ")" << std::endl;
            continue;
        }
        
        // Lakukan UNDO untuk setiap operasi
        std::cout << "FRM: UNDO operasi " << operation_to_string(entry.operation) 
                  << " pada tabel " << entry.table_name 
                  << " (log_id: " << entry.log_id << ")" << std::endl;
        
        bool undo_success = undo_operation(entry);
        
        if (undo_success) {
            undo_count++;
            std::cout << "FRM: Berhasil UNDO log_id " << entry.log_id << std::endl;
        } else {
            std::cerr << "FRM Error: Gagal UNDO log_id " << entry.log_id << std::endl;
        }
    }
    
    std::cout << "FRM: Recovery selesai. Total operasi yang di-UNDO: " << undo_count << std::endl;
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

// ===============================================================================================================================================
// Constants for std::any type tagging
const uint32_t TYPE_INT = 1;
const uint32_t TYPE_FLOAT = 2;
const uint32_t TYPE_STRING = 3;

void FailureRecoveryManager::write_string(std::ofstream& out, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    out.write(str.c_str(), len);
}

std::string FailureRecoveryManager::read_string(std::ifstream& in) {
    uint32_t len;
    in.read(reinterpret_cast<char*>(&len), sizeof(len));

    std::string str;
    str.resize(len);
    in.read(&str[0], len);
    return str;
}

void FailureRecoveryManager::write_any(std::ofstream& out, const std::any& val) {
    if (!val.has_value()) {
        return; 
    }

    if (val.type() == typeid(int)) {
        out.write(reinterpret_cast<const char*>(&TYPE_INT), sizeof(TYPE_INT));
        int val_casted = std::any_cast<int>(val);
        out.write(reinterpret_cast<const char*>(&val_casted), sizeof(val_casted));
    } 
    else if (val.type() == typeid(float)) {
        out.write(reinterpret_cast<const char*>(&TYPE_FLOAT), sizeof(TYPE_FLOAT));
        float val_casted = std::any_cast<float>(val);
        out.write(reinterpret_cast<const char*>(&val_casted), sizeof(val_casted));
    } 
    else if (val.type() == typeid(std::string)) {
        out.write(reinterpret_cast<const char*>(&TYPE_STRING), sizeof(TYPE_STRING));
        std::string str_val = std::any_cast<std::string>(val);
        write_string(out, str_val);
    } 
    else {
        std::cerr << "Error: Unsupported type in write_any" << std::endl;
    }
}

std::any FailureRecoveryManager::read_any(std::ifstream& in) {
    uint32_t tipe_data;
    in.read(reinterpret_cast<char*>(&tipe_data), sizeof(tipe_data));

    if (tipe_data == TYPE_INT) {
        int val;
        in.read(reinterpret_cast<char*>(&val), sizeof(val));
        return val;
    } 
    else if (tipe_data == TYPE_FLOAT) {
        float val;
        in.read(reinterpret_cast<char*>(&val), sizeof(val));
        return val;
    } 
    else if (tipe_data == TYPE_STRING) {
        return read_string(in);
    }
    
    // Error message?
    return std::any();
}

void FailureRecoveryManager::write_columns(std::ofstream& out, const std::map<std::string, std::any>& columns) {
    uint32_t count = static_cast<uint32_t>(columns.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [atr, val] : columns) {
        write_string(out, atr);
        write_any(out, val);
    }
}

std::map<std::string, std::any> FailureRecoveryManager::read_columns(std::ifstream& in) {
    uint32_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    std::map<std::string, std::any> columns;
    for (uint32_t i = 0; i < count; i++) {
        std::string atr = read_string(in);
        std::any val = read_any(in);
        columns[atr] = val;
    }
    return columns;
}

void FailureRecoveryManager::write_row(std::ofstream& out, const Row& row) {
    write_string(out, row.table_name);
    write_columns(out, row.columns);
    out.write(reinterpret_cast<const char*>(&row.row_id), sizeof(row.row_id));
}

Row FailureRecoveryManager::read_row(std::ifstream& in) {
    Row row;
    row.table_name = read_string(in);
    row.columns = read_columns(in);
    in.read(reinterpret_cast<char*>(&row.row_id), sizeof(row.row_id));
    return row;
}

void FailureRecoveryManager::write_log_to_file(std::ofstream& out, const LogEntry& entry) {
    out.write(reinterpret_cast<const char*>(&entry.log_id), sizeof(entry.log_id));
    out.write(reinterpret_cast<const char*>(&entry.transaction_id), sizeof(entry.transaction_id));
    out.write(reinterpret_cast<const char*>(&entry.timestamp), sizeof(entry.timestamp));
    
    int op = static_cast<int>(entry.operation);
    out.write(reinterpret_cast<const char*>(&op), sizeof(op));

    if (entry.operation == Operation::BEGIN || entry.operation == Operation::COMMIT) {
        write_string(out, entry.query);
    } else if (entry.operation == Operation::INSERT) {
        write_string(out, entry.table_name);
        write_row(out, entry.new_value);
        write_string(out, entry.query);
    } else if (entry.operation == Operation::DELETE) {
        write_string(out, entry.table_name);
        write_row(out, entry.old_value);
        write_string(out, entry.query);
    } else if (entry.operation == Operation::UPDATE) {
        write_string(out, entry.table_name);
        write_row(out, entry.new_value);
        write_row(out, entry.old_value);
        write_string(out, entry.query);
    } else if (entry.operation == Operation::CHECKPOINT) {
        write_string(out, entry.table_name);
        write_string(out, entry.query);
    }
}

LogEntry FailureRecoveryManager::read_log_from_file(std::ifstream& in) {
    LogEntry entry;

    in.read(reinterpret_cast<char*>(&entry.log_id), sizeof(entry.log_id));
    in.read(reinterpret_cast<char*>(&entry.transaction_id), sizeof(entry.transaction_id));
    in.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
    
    int op;
    in.read(reinterpret_cast<char*>(&op), sizeof(op));
    entry.operation = static_cast<Operation>(op);

    if (entry.operation == Operation::BEGIN || entry.operation == Operation::COMMIT) {
        entry.query = read_string(in);
    } else if (entry.operation == Operation::INSERT) {
        entry.table_name = read_string(in);
        entry.new_value = read_row(in);
        entry.query = read_string(in);
    } else if (entry.operation == Operation::DELETE) {
        entry.table_name = read_string(in);
        entry.old_value = read_row(in);
        entry.query = read_string(in);
    } else if (entry.operation == Operation::UPDATE) {
        entry.table_name = read_string(in);
        entry.new_value = read_row(in);
        entry.old_value = read_row(in);
        entry.query = read_string(in);
    } else if (entry.operation == Operation::CHECKPOINT) {
        entry.table_name = read_string(in);
        entry.query = read_string(in);
    }

    return entry;
}

std::vector<LogEntry> FailureRecoveryManager::read_all_logs(const std::string& file_path) {
    std::vector<LogEntry> logs;
    std::ifstream infile(file_path, std::ios::binary);

    if (!infile.is_open()) {
        std::cerr << "FRM Critical Error: Gagal membuka file log untuk dibaca -> " << file_path << std::endl;
        return logs;
    }

    while (infile.peek() != EOF) {
        LogEntry entry = read_log_from_file(infile);
        
        if (infile.fail()) {
            std::cerr << "FRM Error: Data corrupt or partial read encountered." << std::endl;
            break;
        }
        
        logs.push_back(entry);
    }
    
    infile.close();
    return logs;
}

// Fungsi akal-akalan supaya public (untuk testing saja)
std::vector<LogEntry> FailureRecoveryManager::read_all_logs_public(const std::string& file_path) {
    return read_all_logs(file_path);
}

// ===============================================================================================================================================

// Helper untuk convert std::any ke string
std::string FailureRecoveryManager::any_to_string(const std::any& val) {
    if (!val.has_value()) return "NULL";

    if (val.type() == typeid(int)) {
        return std::to_string(std::any_cast<int>(val));
    } else if (val.type() == typeid(float)) {
        return std::to_string(std::any_cast<float>(val));
    } else if (val.type() == typeid(std::string)) {
        return "\"" + std::any_cast<std::string>(val) + "\""; // Pakai kutip biar jelas string
    }
    return "UNKNOWN_TYPE";
}

std::string FailureRecoveryManager::row_to_string(const Row& row) {
    if (row.row_id == -1 && row.columns.empty()) return "{}";

    std::ostringstream oss;
    oss << "{ID:" << row.row_id << ",Data:[";
    
    bool first = true;
    for (const auto& [col_name, val] : row.columns) {
        if (!first) oss << ";"; // Pakai titik koma biar beda dengan pemisah log
        oss << col_name << "=" << any_to_string(val);
        first = false;
    }
    oss << "]}";
    return oss.str();
}

std::string FailureRecoveryManager::sanitize_for_log(std::string input) {
    // Ganti newline dengan spasi agar tetap 1 baris
    std::replace(input.begin(), input.end(), '\n', ' ');
    std::replace(input.begin(), input.end(), '\r', ' ');
    return input;
}

void FailureRecoveryManager::write_log_to_text_file(std::ofstream& out, const LogEntry& entry) {
    // 1. Format Timestamp (Compact)
    char time_buf[26];
    #ifdef _WIN32
        ctime_s(time_buf, sizeof(time_buf), &entry.timestamp);
    #else
        ctime_r(&entry.timestamp, time_buf);
    #endif
    std::string time_str(time_buf);
    if (!time_str.empty() && time_str.back() == '\n') time_str.pop_back();

    // 2. Tulis Kolom Utama (Timestamp | LogID | TransID | Op)
    out << "[" << time_str << "] | "
        << "LID:" << entry.log_id << " | "
        << "TID:" << entry.transaction_id << " | "
        << operation_to_string(entry.operation);

    // 3. Tulis Detail (Tergantung Operasi)
    if (entry.operation == Operation::INSERT) {
        out << " | Tbl:" << entry.table_name
            << " | New:" << row_to_string(entry.new_value);
    } 
    else if (entry.operation == Operation::DELETE) {
        out << " | Tbl:" << entry.table_name
            << " | Old:" << row_to_string(entry.old_value);
    } 
    else if (entry.operation == Operation::UPDATE) {
        out << " | Tbl:" << entry.table_name
            << " | Old:" << row_to_string(entry.old_value)
            << " | New:" << row_to_string(entry.new_value);
    }
    else if (entry.operation == Operation::CHECKPOINT) {
        // Query di checkpoint berisi list active transactions
        out << " | Info:" << sanitize_for_log(entry.query); 
    }

    // 4. Tulis Query (Kecuali Checkpoint yang sudah ditulis di atas)
    // Pastikan query disanitasi agar tidak ada newline
    if (entry.operation != Operation::CHECKPOINT && !entry.query.empty()) {
        out << " | Qry:" << sanitize_for_log(entry.query);
    } else if (entry.operation == Operation::ABORT) {
        out << " | Status:ABORTED";
    }

    // Akhiri baris
    out << "\n";
}

// ===============================================================================================================================================

std::vector<Condition> FailureRecoveryManager::row_to_conditions(const Row& row, const std::string& table_name) {
    std::vector<Condition> conditions;
    
    // Use primary key columns to identify the row
    // Common primary key column names
    std::vector<std::string> pk_columns = {"StudentID", "CourseID", "id"};
    
    for (const auto& pk_col : pk_columns) {
        if (row.columns.find(pk_col) != row.columns.end()) {
            Condition cond;
            cond.column = pk_col;
            cond.operation = "=";
            cond.operand = row.columns.at(pk_col);
            conditions.push_back(cond);
            std::cout << "FRM: Using '" << pk_col << "' as identifying condition" << std::endl;
            break; // Only need one primary key
        }
    }
    
    return conditions;
}

std::any FailureRecoveryManager::string_to_any(const std::string& str) {
    // Try to infer type from string
    if (str.empty()) return str;
    
    // Try integer
    try {
        size_t pos;
        int int_val = std::stoi(str, &pos);
        if (pos == str.length()) {
            return int_val;
        }
    } catch (...) {}
    
    // Try float
    try {
        size_t pos;
        float float_val = std::stof(str, &pos);
        if (pos == str.length()) {
            return float_val;
        }
    } catch (...) {}
    
    // Default to string
    return str;
}

std::set<int> FailureRecoveryManager::parse_checkpoint_list(const std::string& query) {
    std::set<int> active_tids;
    size_t start = query.find('[');
    size_t end = query.find(']');
    
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return active_tids;
    }
    
    std::string list_str = query.substr(start + 1, end - start - 1);
    std::istringstream iss(list_str);
    std::string token;
    
    while (std::getline(iss, token, ',')) {
        try {
            int tid = std::stoi(token);
            active_tids.insert(tid);
        } catch (...) {}
    }
    
    return active_tids;
}

bool FailureRecoveryManager::undo_operation(const LogEntry& entry) {
    std::cout << "FRM: Melakukan UNDO untuk operasi " << operation_to_string(entry.operation) << std::endl;
    
    try {
        switch (entry.operation) {
            case Operation::INSERT: {
                // UNDO INSERT -> DELETE row yang baru diinsert (menggunakan new_value)
                std::cout << "FRM: UNDO INSERT - Menghapus row dengan ID " << entry.new_value.row_id 
                          << " dari tabel " << entry.table_name << std::endl;
                
                // Create conditions to identify the row to delete
                std::vector<Condition> conditions = row_to_conditions(entry.new_value, entry.table_name);
                
                if (conditions.empty()) {
                    std::cerr << "FRM Error: Tidak bisa membuat kondisi untuk DELETE" << std::endl;
                    return false;
                }
                
                // Create DataDeletion and call storage manager
                DataDeletion deletion;
                deletion.table = entry.table_name;
                deletion.conditions = conditions;
                
                int deleted = storage_engine_.delete_block(deletion);
                std::cout << "FRM: Berhasil menghapus " << deleted << " row" << std::endl;
                
                return deleted > 0;
            }
            
            case Operation::DELETE: {
                // UNDO DELETE -> INSERT kembali row yang dihapus (menggunakan old_value)
                std::cout << "FRM: UNDO DELETE - Mengembalikan row dengan ID " << entry.old_value.row_id 
                          << " ke tabel " << entry.table_name << std::endl;
                
                // Create DataWrite for INSERT and call storage manager
                DataWrite<Row> write;
                write.table = entry.table_name;
                write.new_value = entry.old_value;
                write.is_insert = true;
                
                int inserted = storage_engine_.write_block(write);
                std::cout << "FRM: Berhasil mengembalikan " << inserted << " row" << std::endl;
                
                return inserted > 0;
            }
            
            case Operation::UPDATE: {
                // UNDO UPDATE -> UPDATE kembali ke nilai lama (old_value)
                std::cout << "FRM: UNDO UPDATE - Mengembalikan row dengan ID " << entry.old_value.row_id 
                          << " ke nilai lama pada tabel " << entry.table_name << std::endl;
                
                // Create conditions to identify the row to update
                std::vector<Condition> conditions = row_to_conditions(entry.new_value, entry.table_name);
                
                if (conditions.empty()) {
                    std::cerr << "FRM Error: Tidak bisa membuat kondisi untuk UPDATE" << std::endl;
                    return false;
                }
                
                // Create DataWrite for UPDATE and call storage manager
                DataWrite<Row> write;
                write.table = entry.table_name;
                write.new_value = entry.old_value;
                write.conditions = conditions;
                write.is_insert = false;
                
                int updated = storage_engine_.write_block(write);
                std::cout << "FRM: Berhasil mengembalikan " << updated << " row ke nilai lama" << std::endl;
                
                return updated > 0;
            }
            
            default:
                std::cerr << "FRM Error: Operasi " << operation_to_string(entry.operation) 
                          << " tidak dapat di-UNDO" << std::endl;
                return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "FRM Error: Exception during UNDO - " << e.what() << std::endl;
        return false;
    }
}

bool FailureRecoveryManager::redo_operation(const LogEntry& entry) {
    std::cout << "FRM: Melakukan REDO untuk operasi " << operation_to_string(entry.operation) << " LID:" << entry.log_id << std::endl;

    try {
        switch (entry.operation) {
            case Operation::INSERT: {
                std::cout << "FRM: REDO INSERT - Memasukkan kembali row dengan ID " << entry.new_value.row_id 
                          << " ke tabel " << entry.table_name << std::endl;

                // REDO INSERT: Masukkan kembali new_value
                DataWrite<Row> write;
                write.table = entry.table_name;
                write.new_value = entry.new_value;
                write.is_insert = true;
                storage_engine_.write_block(write);
                return true;
            }
            case Operation::DELETE: {
                std::cout << "FRM: REDO DELETE - Menghapus kembali row dengan ID " << entry.old_value.row_id 
                          << " dari tabel " << entry.table_name << std::endl;

                // REDO DELETE: Hapus kembali old_value
                std::vector<Condition> conditions = row_to_conditions(entry.old_value, entry.table_name);
                
                if (conditions.empty()) {
                    std::cerr << "FRM Error: Tidak bisa membuat kondisi untuk REDO DELETE" << std::endl;
                    return false;
                }

                DataDeletion deletion;
                deletion.table = entry.table_name;
                deletion.conditions = conditions;
                storage_engine_.delete_block(deletion);
                return true;
            }
            case Operation::UPDATE: {
                std::cout << "FRM: REDO UPDATE - Memperbarui row dengan ID " << entry.new_value.row_id 
                          << " pada tabel " << entry.table_name << std::endl;

                // REDO UPDATE: Perbarui ke new_value
                std::vector<Condition> conditions = row_to_conditions(entry.new_value, entry.table_name);
                
                if (conditions.empty()) {
                    std::cerr << "FRM Error: Tidak bisa membuat kondisi untuk REDO UPDATE" << std::endl;
                    return false;
                }

                DataWrite<Row> write;
                write.table = entry.table_name;
                write.new_value = entry.new_value;
                write.conditions = conditions;
                write.is_insert = false;
                storage_engine_.write_block(write);
                return true;
            }
            default:
                std::cerr << "FRM Error: Operasi " << operation_to_string(entry.operation) 
                          << " tidak dapat di-REDO" << std::endl;
                return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "FRM Error: Exception during REDO - " << e.what() << std::endl;
        return false;
    }

    return true;
}

void FailureRecoveryManager::recover_from_crash() {
    if (!std::filesystem::exists(this->log_file_path)) return;

    std::cout << "FRM: Memulai recovery dari crash..." << std::endl;

    std::vector<LogEntry> all_logs = read_all_logs(this->log_file_path);
    if (all_logs.empty()) {
        std::cout << "FRM: Tidak ada log untuk di-recover." << std::endl;
        return;
    }

    std::set<int> undo_list;
    int redo_start_index = 0;

    // Proses mencari checkpoint terakhir dan build undo_list
    for (int i = static_cast<int>(all_logs.size()) - 1; i >= 0; i--) {
        if (all_logs[i].operation == Operation::CHECKPOINT) {
            undo_list = parse_checkpoint_list(all_logs[i].query);
            redo_start_index = i;
            std::cout << "FRM: Checkpoint ditemukan di log_id " << all_logs[i].log_id 
                      << " dengan " << undo_list.size() << " transaksi aktif di checkpoint." << std::endl;
            break;
        }
    }

    // Scan log setelah checkpoint untuk update status transaksi
    for (size_t i = redo_start_index; i < all_logs.size(); i++) {
        const LogEntry& entry = all_logs[i];
        
        if (entry.operation == Operation::BEGIN) {
            undo_list.insert(entry.transaction_id);
        } 
        else if (entry.operation == Operation::COMMIT || entry.operation == Operation::ABORT) {
            undo_list.erase(entry.transaction_id);
        }

        if (entry.operation == Operation::INSERT || 
            entry.operation == Operation::UPDATE || 
            entry.operation == Operation::DELETE) {
        
            redo_operation(entry);
        }
    }
    
    std::cout << "FRM: Ditemukan " << undo_list.size() << " transaksi uncommitted yang perlu di-UNDO." << std::endl;

    // Fase UNDO untuk transaksi yang belum selesai
    std::cout << "FRM: Memulai Fase UNDO untuk " << undo_list.size() << " transaksi yang belum selesai." << std::endl;
    
    if (undo_list.empty()) {
        std::cout << "FRM: Tidak ada transaksi yang perlu di-UNDO." << std::endl;
        return;
    } else {
        for (int i = static_cast<int>(all_logs.size()) - 1; i >= 0; i--) {
            const LogEntry& entry = all_logs[i];
            if (undo_list.count(entry.transaction_id)) {
                if (entry.operation == Operation::BEGIN) {
                    undo_list.erase(entry.transaction_id);
                } 
                else if (entry.operation != Operation::CHECKPOINT) {
                    undo_operation(entry);
                }
            }
            
            if (undo_list.empty()) break;
        }
    }

    std::cout << "FRM: Recovery dari crash selesai." << std::endl;
}

void FailureRecoveryManager::flush_buffer() {
    if (this->log_buffer.empty()) {
        return;
    }

    std::ofstream log_file(this->log_file_path, std::ios::app | std::ios::binary);

    if (!log_file.is_open()) {
        std::cerr << "FRM: Gagal membuka file log untuk penulisan: " << this->log_file_path << std::endl;
        return;
    }

    std::string text_log_path = "../data/wal.log";
    std::ofstream text_file(text_log_path, std::ios::app);

    for (const auto& entry : this->log_buffer) {
        write_log_to_file(log_file, entry);

        if (text_file.is_open()) {
            write_log_to_text_file(text_file, entry);
        }
    }

    log_file.close();
    if (text_file.is_open()) text_file.close();
    this->log_buffer.clear();
    std::cout << "FRM: Flush log buffer ke file " << this->log_file_path << "." << std::endl;
}

void FailureRecoveryManager::debug_run_crash_recovery() {
    std::cout << "\n[TEST] Memicu System Failure Recovery secara manual..." << std::endl;
    recover_from_crash();
}

void FailureRecoveryManager::reset_state_for_testing() {
    std::lock_guard<std::mutex> lock(this->mtx);
    this->active_transactions_cache.clear();
    this->log_buffer.clear();
    this->checkpoints.clear();
    this->next_log_id = 1;
    this->next_checkpoint_id = 1;
    std::cout << "[TEST] FailureRecoveryManager state reset" << std::endl;
}

} // namespace mdbms::fr

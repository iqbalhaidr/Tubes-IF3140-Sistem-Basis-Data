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
    if (first_word == "CREATE") {
        return Operation::CREATE_TABLE;
    }
    if (first_word == "DROP") {
        return Operation::DROP_TABLE;
    }

    return Operation::BEGIN;
}

FailureRecoveryManager& FailureRecoveryManager::get_instance() {
    static FailureRecoveryManager instance;
    return instance;
}

FailureRecoveryManager::FailureRecoveryManager() 
    : storage_engine_(sm::StorageEngine::get_instance()), running_(true) {
    this->next_log_id = 1;
    this->next_checkpoint_id = 1;
    // path untuk menyimpan log file
    this->log_file_path = "data/wal.bin";
    std::cout << "FRM: Konstruktor FailureRecoveryManager dipanggil..." << std::endl;
    std::cout << "FRM: Log file path: " << this->log_file_path << std::endl;

    recover_from_crash();

    std::vector<LogEntry> existing = read_all_logs(this->log_file_path);
    if (!existing.empty()) {
        this->next_log_id = existing.back().log_id + 1;
    }
    
    // Start periodic checkpoint thread
    checkpoint_thread_ = std::thread(&FailureRecoveryManager::checkpoint_worker, this);
    std::cout << "FRM: Periodic checkpoint thread started (interval: " 
              << CHECKPOINT_INTERVAL_SECONDS << " seconds)" << std::endl;
}

FailureRecoveryManager::~FailureRecoveryManager() {
    std::cout << "FRM: Destruktor FailureRecoveryManager dipanggil..." << std::endl;
    
    // Stop checkpoint thread
    running_ = false;
    if (checkpoint_thread_.joinable()) {
        checkpoint_thread_.join();
        std::cout << "FRM: Periodic checkpoint thread stopped" << std::endl;
    }
    
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
    } else if (entry.operation == Operation::CREATE_TABLE) {
        entry.table_name = info.table_name;
        try {
            TableSchema schema = storage_engine_.get_table_schema(entry.table_name);
            entry.created_schema = schema;
            std::cout << "FRM: Logging CREATE TABLE untuk table " 
                      << entry.table_name << " (schema saved for recovery)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "FRM Warning: Gagal mendapatkan schema untuk tabel yang dibuat: " 
                      << entry.table_name << ". Error: " << e.what() << std::endl;
            entry.created_schema = std::nullopt;
        }
    } else if (entry.operation == Operation::DROP_TABLE) {
        entry.table_name = info.table_name;
        auto it = pending_drop_schemas_.find(entry.table_name);
        if (it != pending_drop_schemas_.end()) {
            entry.dropped_schema = it->second;
            pending_drop_schemas_.erase(it);
            std::cout << "FRM: Logging DROP TABLE untuk table " 
                    << entry.table_name << " (schema dari cache)" << std::endl;
        } else {
            try {
                TableSchema schema = storage_engine_.get_table_schema(entry.table_name);
                entry.dropped_schema = schema;
                std::cout << "FRM: Logging DROP TABLE untuk table " 
                        << entry.table_name << " (schema dari disk - fallback)" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "FRM Warning: Schema tidak tersedia: " << e.what() << std::endl;
                entry.dropped_schema = std::nullopt;
            }
        }
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

void FailureRecoveryManager::commit_transaction(int transaction_id) {
    std::lock_guard<std::mutex> lock(this->mtx);
    
    std::cout << "FRM: Committing transaction " << transaction_id << " with WAL protocol..." << std::endl;
    
    // tulis commit log entry ke buffer
    LogEntry commit_entry;
    commit_entry.log_id = this->next_log_id++;
    commit_entry.transaction_id = transaction_id;
    commit_entry.timestamp = std::time(nullptr);
    commit_entry.operation = Operation::COMMIT;
    commit_entry.query = "COMMIT";
    this->log_buffer.push_back(commit_entry);
    
    // flush wal
    std::cout << "FRM: Step 1 - Flushing WAL (COMMIT record to disk)..." << std::endl;
    flush_buffer();  // ✅ COMMIT record safely on disk
    
    // flush data
    std::cout << "FRM: Step 2 - Flushing data pages to disk..." << std::endl;
    storage_engine_.checkpoint();  // ✅ Data to disk
    
    // apus active transaction
    active_transactions_cache.erase(transaction_id);
    
    std::cout << "FRM: Transaction " << transaction_id << " committed successfully (WAL → Data)" << std::endl;
}

void FailureRecoveryManager::abort_transaction(int transaction_id) {
    std::lock_guard<std::mutex> lock(this->mtx);

    std::cout << "FRM: Aborting transaction " << transaction_id << "..." << std::endl;
    
    // clear buffer
    // harusnya clear buffer hanya untuk transaksi yang diabort (tpi skrng masih clear semua)
    std::cout << "FRM: Discarding uncommitted buffer pages..." << std::endl;
    storage_engine_.clear_buffer_for_testing(); 
    
    // undo dari log untuk data yang sudah diflush ke disk
    std::cout << "FRM: Performing UNDO from log..." << std::endl;
    RecoverCriteria criteria;
    criteria.transaction_id = transaction_id;
    criteria.use_timestamp = false;
    recover(criteria);
    
    // tulis abort log entry ke buffer
    LogEntry abort_entry;
    abort_entry.log_id = this->next_log_id++;
    abort_entry.transaction_id = transaction_id;
    abort_entry.timestamp = std::time(nullptr);
    abort_entry.operation = Operation::ABORT;
    abort_entry.query = "ABORT";
    this->log_buffer.push_back(abort_entry);
    flush_buffer();
    
    // apus active transaction
    active_transactions_cache.erase(transaction_id);
    
    std::cout << "FRM: Transaction " << transaction_id << " aborted successfully" << std::endl;
}

void FailureRecoveryManager::save_checkpoint() {
    std::cout << "FRM: Menyimpan checkpoint..." << std::endl;
    std::lock_guard<std::mutex> lock(this->mtx);
    
    // flush wal
    std::cout << "FRM: Step 1 - Flushing WAL buffer..." << std::endl;
    flush_buffer();
    
    // tulis checkpoint START log
    LogEntry checkpoint_start;
    checkpoint_start.log_id = this->next_log_id++;
    checkpoint_start.transaction_id = -1;
    checkpoint_start.timestamp = std::time(nullptr);
    checkpoint_start.operation = Operation::CHECKPOINT;
    checkpoint_start.table_name = "SYSTEM";
    checkpoint_start.query = "CHECKPOINT_START";
    this->log_buffer.push_back(checkpoint_start);
    flush_buffer();  // Ensure checkpoint START on disk
    
    // flush data
    std::cout << "FRM: Step 2 - Flushing data pages to disk..." << std::endl;
    storage_engine_.checkpoint();

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
    checkpoint_entry.query = "CHECKPOINT_END:" + active_transactions_str;

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
        case Operation::CREATE_TABLE: return "CREATE_TABLE";
        case Operation::DROP_TABLE: return "DROP_TABLE";
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

void FailureRecoveryManager::write_columns(std::ofstream& out, const std::unordered_map<std::string, std::any>& columns) {
    uint32_t count = static_cast<uint32_t>(columns.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [atr, val] : columns) {
        write_string(out, atr);
        write_any(out, val);
    }
}

std::unordered_map<std::string, std::any> FailureRecoveryManager::read_columns(std::ifstream& in) {
    uint32_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    std::unordered_map<std::string, std::any> columns;
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

void FailureRecoveryManager::write_schema(std::ofstream& out, const TableSchema& schema) {
    // 1. Table Name
    write_string(out, schema.table_name);

    // 2. Column Names
    uint32_t col_count = static_cast<uint32_t>(schema.column_names.size());
    out.write(reinterpret_cast<const char*>(&col_count), sizeof(col_count));
    for (const auto& name : schema.column_names) {
        write_string(out, name);
    }

    // 3. Column Types
    // Ensure types size matches names size for consistency, though schema struct implies they should match
    uint32_t type_count = static_cast<uint32_t>(schema.column_types.size());
    out.write(reinterpret_cast<const char*>(&type_count), sizeof(type_count));
    for (const auto& type : schema.column_types) {
        int type_int = static_cast<int>(type);
        out.write(reinterpret_cast<const char*>(&type_int), sizeof(type_int));
    }

    // 4. Column Sizes
    uint32_t size_count = static_cast<uint32_t>(schema.column_sizes.size());
    out.write(reinterpret_cast<const char*>(&size_count), sizeof(size_count));
    for (int size : schema.column_sizes) {
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
    }

    // 5. Primary Key
    write_string(out, schema.primary_key);

    // 6. Foreign Keys
    uint32_t fk_count = static_cast<uint32_t>(schema.foreign_keys.size());
    out.write(reinterpret_cast<const char*>(&fk_count), sizeof(fk_count));
    for (const auto& [col, ref] : schema.foreign_keys) {
        write_string(out, col);
        write_string(out, ref);
    }
}

TableSchema FailureRecoveryManager::read_schema(std::ifstream& in) {
    TableSchema schema;

    // 1. Table Name
    schema.table_name = read_string(in);

    // 2. Column Names
    uint32_t col_count;
    in.read(reinterpret_cast<char*>(&col_count), sizeof(col_count));
    schema.column_names.resize(col_count);
    for (uint32_t i = 0; i < col_count; ++i) {
        schema.column_names[i] = read_string(in);
    }

    // 3. Column Types
    uint32_t type_count;
    in.read(reinterpret_cast<char*>(&type_count), sizeof(type_count));
    schema.column_types.resize(type_count);
    for (uint32_t i = 0; i < type_count; ++i) {
        int type_int;
        in.read(reinterpret_cast<char*>(&type_int), sizeof(type_int));
        schema.column_types[i] = static_cast<DataType>(type_int);
    }

    // 4. Column Sizes
    uint32_t size_count;
    in.read(reinterpret_cast<char*>(&size_count), sizeof(size_count));
    schema.column_sizes.resize(size_count);
    for (uint32_t i = 0; i < size_count; ++i) {
        int size;
        in.read(reinterpret_cast<char*>(&size), sizeof(size));
        schema.column_sizes[i] = size;
    }

    // 5. Primary Key
    schema.primary_key = read_string(in);

    // 6. Foreign Keys
    uint32_t fk_count;
    in.read(reinterpret_cast<char*>(&fk_count), sizeof(fk_count));
    for (uint32_t i = 0; i < fk_count; ++i) {
        std::string col = read_string(in);
        std::string ref = read_string(in);
        schema.foreign_keys[col] = ref;
    }

    return schema;
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
    } else if (entry.operation == Operation::CREATE_TABLE) {
        write_string(out, entry.table_name);
        write_string(out, entry.query);
        
        bool has_schema = entry.created_schema.has_value();
        out.write(reinterpret_cast<const char*>(&has_schema), sizeof(has_schema));
        if (has_schema) {
            write_schema(out, entry.created_schema.value());
        }
    } else if (entry.operation == Operation::DROP_TABLE) {
        write_string(out, entry.table_name);
        write_string(out, entry.query);
        
        bool has_schema = entry.dropped_schema.has_value();
        out.write(reinterpret_cast<const char*>(&has_schema), sizeof(has_schema));
        if (has_schema) {
            write_schema(out, entry.dropped_schema.value());
        }
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
    } else if (entry.operation == Operation::CREATE_TABLE) {
        entry.table_name = read_string(in);
        entry.query = read_string(in);

        bool has_schema;
        in.read(reinterpret_cast<char*>(&has_schema), sizeof(has_schema));
        if (has_schema) {
            entry.created_schema = read_schema(in);
        }
    } else if (entry.operation == Operation::DROP_TABLE) {
        entry.table_name = read_string(in);
        entry.query = read_string(in);
        
        bool has_schema;
        in.read(reinterpret_cast<char*>(&has_schema), sizeof(has_schema));
        if (has_schema) {
            entry.dropped_schema = read_schema(in);
        }
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

std::string FailureRecoveryManager::schema_to_string(const TableSchema& schema) {
    std::ostringstream oss;
    oss << "{Table:" << schema.table_name << ", PK:" << schema.primary_key << ", Cols:[";
    
    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        if (i > 0) oss << "; ";
        oss << schema.column_names[i] << "(";
        
        if (i < schema.column_types.size()) {
            switch (schema.column_types[i]) {
                case DataType::INTEGER: oss << "INT"; break;
                case DataType::FLOAT: oss << "FLOAT"; break;
                case DataType::CHAR: oss << "CHAR"; break;
                case DataType::VARCHAR: oss << "VARCHAR"; break;
                default: oss << "UNK"; break;
            }
        }
        
        if (i < schema.column_sizes.size() && schema.column_sizes[i] > 0) {
             oss << ":" << schema.column_sizes[i];
        }
        oss << ")";
    }
    oss << "]";

    if (!schema.foreign_keys.empty()) {
        oss << ", FKs:[";
        bool first = true;
        for (const auto& [col, ref] : schema.foreign_keys) {
            if (!first) oss << "; ";
            oss << col << "->" << ref;
            first = false;
        }
        oss << "]";
    }
    oss << "}";
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
    else if (entry.operation == Operation::CREATE_TABLE) {
        if (entry.created_schema.has_value()) {
            out << " | Schema:" << schema_to_string(entry.created_schema.value());
        } else {
            out << " | Schema:NULL";
        }
    }
    else if (entry.operation == Operation::DROP_TABLE) {
        if (entry.dropped_schema.has_value()) {
            out << " | BackupSchema:" << schema_to_string(entry.dropped_schema.value());
        } else {
            out << " | BackupSchema:NULL";
        }
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

            case Operation::CREATE_TABLE: {
                std::cout << "FRM: UNDO CREATE_TABLE - Menghapus tabel " << entry.table_name << std::endl;
                
                bool dropped = storage_engine_.drop_table(entry.table_name);
                if (dropped) {
                    std::cout << "FRM: Tabel " << entry.table_name << " berhasil dihapus." << std::endl;
                } else {
                    std::cerr << "FRM Error: Gagal menghapus tabel " << entry.table_name << std::endl;
                }
                return dropped;
            }

            case Operation::DROP_TABLE: {
                std::cout << "FRM: UNDO DROP_TABLE - Mengembalikan tabel " << entry.table_name << std::endl;
                
                if (!entry.dropped_schema.has_value()) {
                    std::cerr << "FRM Error: Skema untuk tabel " << entry.table_name << " tidak tersedia, tidak dapat mengembalikan tabel." << std::endl;
                    return false;
                }

                const TableSchema& schema = entry.dropped_schema.value();
                bool created = storage_engine_.create_table(schema);
                if (created) {
                    std::cout << "FRM: Tabel " << entry.table_name << " berhasil dikembalikan." << std::endl;
                } else {
                    std::cerr << "FRM Error: Gagal mengembalikan tabel " << entry.table_name << std::endl;
                }
                return created;
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

            case Operation::CREATE_TABLE: {
                std::cout << "FRM: REDO CREATE_TABLE - Membuat tabel " << entry.table_name << std::endl;

                if (!entry.created_schema.has_value()) {
                    std::cerr << "FRM Error: Schema untuk tabel " << entry.table_name 
                            << " tidak tersedia untuk REDO CREATE." << std::endl;
                    
                    std::cout << "FRM: Melewati REDO CREATE (schema tidak tersimpan, "
                            << "table mungkin sudah ada)" << std::endl;
                    return true;
                }

                const TableSchema& schema = entry.created_schema.value();
                bool created = storage_engine_.create_table(schema);
                
                if (created) {
                    std::cout << "FRM: Tabel " << entry.table_name << " berhasil dibuat." << std::endl;
                } else {
                    std::cout << "FRM: Tabel " << entry.table_name 
                            << " mungkin sudah ada, melewati REDO CREATE." << std::endl;
                }
                
                return true;
            }

            case Operation::DROP_TABLE: {
                std::cout << "FRM: REDO DROP_TABLE - Menghapus tabel " << entry.table_name << std::endl;

                bool dropped = storage_engine_.drop_table(entry.table_name);
                if (dropped) {
                    std::cout << "FRM: Tabel " << entry.table_name << " berhasil dihapus." << std::endl;
                } else {
                    std::cerr << "FRM Error: Gagal menghapus tabel " << entry.table_name << std::endl;
                }
                return dropped;
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

    // FASE ANALYSIS
    std::cout << "\nFASE ANALYSIS" << std::endl;
    
    std::set<int> undo_list;      // Transaksi yang perlu di-UNDO (uncommitted)
    std::set<int> committed_txs;  // Transaksi yang sudah COMMIT
    int redo_start_index = 0;

    // Mencari checkpoint terakhir
    for (int i = static_cast<int>(all_logs.size()) - 1; i >= 0; i--) {
        if (all_logs[i].operation == Operation::CHECKPOINT) {
            undo_list = parse_checkpoint_list(all_logs[i].query);
            redo_start_index = i;
            std::cout << "FRM: Checkpoint ditemukan di log_id " << all_logs[i].log_id 
                      << " dengan " << undo_list.size() << " transaksi aktif." << std::endl;
            break;
        }
    }

    // Scan log setelah checkpoint untuk menentukan status transaksi
    for (size_t i = redo_start_index; i < all_logs.size(); i++) {
        const LogEntry& entry = all_logs[i];
        
        if (entry.operation == Operation::BEGIN) {
            undo_list.insert(entry.transaction_id);
        } 
        else if (entry.operation == Operation::COMMIT) {
            undo_list.erase(entry.transaction_id);
            committed_txs.insert(entry.transaction_id);
        }
        else if (entry.operation == Operation::ABORT) {
            undo_list.erase(entry.transaction_id);
        }
    }
    
    // debugger
    std::cout << "FRM: Ditemukan " << committed_txs.size() << " transaksi committed (perlu REDO)." << std::endl;
    std::cout << "FRM: Ditemukan " << undo_list.size() << " transaksi uncommitted (perlu UNDO)." << std::endl;

    // FASE REDO
    std::cout << "\nFASE REDO" << std::endl;
    
    if (committed_txs.empty()) {
        std::cout << "FRM: Tidak ada transaksi committed yang perlu di-REDO." << std::endl;
    } else {
        int redo_count = 0;
        // REDO semua operasi dari transaksi yang sudah COMMIT (forward scan)
        for (size_t i = redo_start_index; i < all_logs.size(); i++) {
            const LogEntry& entry = all_logs[i];
            
            // Hanya REDO operasi data dari transaksi yang committed
            if (committed_txs.count(entry.transaction_id) && 
                (entry.operation == Operation::INSERT || 
                 entry.operation == Operation::UPDATE || 
                 entry.operation == Operation::DELETE || 
                 entry.operation == Operation::CREATE_TABLE ||
                 entry.operation == Operation::DROP_TABLE)) {
                
                std::cout << "FRM: REDO T" << entry.transaction_id << " - " 
                          << operation_to_string(entry.operation) 
                          << " pada tabel " << entry.table_name 
                          << " (log_id: " << entry.log_id << ")" << std::endl;
                
                redo_operation(entry);
                redo_count++;
            }
        }
        std::cout << "FRM: Fase REDO selesai. Total operasi yang di-REDO: " << redo_count << std::endl;
    }

    // FASE UNDO
    std::cout << "\nFASE UNDO" << std::endl;
    
    if (undo_list.empty()) {
        std::cout << "FRM: Tidak ada transaksi uncommitted yang perlu di-UNDO." << std::endl;
    } else {
        int undo_count = 0;
        std::set<int> undo_list_copy = undo_list;
        
        // UNDO semua operasi dari transaksi yang belum selesai (backward scan)
        for (int i = static_cast<int>(all_logs.size()) - 1; i >= 0; i--) {
            const LogEntry& entry = all_logs[i];
            
            if (undo_list_copy.count(entry.transaction_id)) {
                if (entry.operation == Operation::BEGIN) {
                    undo_list_copy.erase(entry.transaction_id);
                    std::cout << "FRM: Mencapai BEGIN untuk T" << entry.transaction_id << std::endl;
                } 
                else if (entry.operation == Operation::INSERT || 
                         entry.operation == Operation::UPDATE || 
                         entry.operation == Operation::DELETE ||
                         entry.operation == Operation::CREATE_TABLE ||
                         entry.operation == Operation::DROP_TABLE) {
                    std::cout << "FRM: UNDO T" << entry.transaction_id << " - " 
                              << operation_to_string(entry.operation) 
                              << " pada tabel " << entry.table_name 
                              << " (log_id: " << entry.log_id << ")" << std::endl;
                    
                    undo_operation(entry);
                    undo_count++;
                }
            }
            
            if (undo_list_copy.empty()) break;
        }
        std::cout << "FRM: Fase UNDO selesai. Total operasi yang di-UNDO: " << undo_count << std::endl;
    }
    
    // debugger
    std::cout << "\nFRM: Recovery dari crash selesai." << std::endl;
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

    std::string text_log_path = "data/wal.log";
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

void FailureRecoveryManager::checkpoint_worker() {
    std::cout << "FRM: Checkpoint worker thread started" << std::endl;
    
    while (running_) {
        // Sleep in small intervals to allow quick shutdown
        for (int i = 0; i < CHECKPOINT_INTERVAL_SECONDS && running_; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (running_) {
            std::cout << "\nFRM: [PERIODIC] Triggering automatic checkpoint..." << std::endl;
            try {
                save_checkpoint();
                std::cout << "FRM: [PERIODIC] Automatic checkpoint completed" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "FRM Error: Periodic checkpoint failed - " << e.what() << std::endl;
            }
        }
    }
    
    std::cout << "FRM: Checkpoint worker thread exiting" << std::endl;
}

void FailureRecoveryManager::prepare_ddl_operation(const std::string& table_name, Operation op) {
    std::lock_guard<std::mutex> lock(this->mtx);
    
    if (op == Operation::DROP_TABLE) {
        try {
            TableSchema schema = storage_engine_.get_table_schema(table_name);
            pending_drop_schemas_[table_name] = schema;
            std::cout << "FRM: Schema untuk DROP TABLE " << table_name 
                      << " disimpan di cache." << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "FRM Warning: Gagal backup schema untuk " << table_name 
                      << ": " << e.what() << std::endl;
        }
    }
}

} // namespace mdbms::fr

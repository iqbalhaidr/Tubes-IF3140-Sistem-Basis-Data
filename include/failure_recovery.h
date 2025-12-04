#pragma once
#include <vector>
#include <ctime>
#include <string>
#include <sstream>
#include <mutex>
#include <set>
#include <thread>
#include <atomic>
#include <chrono>
#include "types.h"
#include "storage_manager.h"

namespace mdbms::fr {

class FailureRecoveryManager {
public:
    static FailureRecoveryManager& get_instance();
    FailureRecoveryManager(const FailureRecoveryManager&) = delete;
    FailureRecoveryManager& operator=(const FailureRecoveryManager&) = delete;
    ~FailureRecoveryManager();
    void write_log(const ExecutionResult& info);
    void save_checkpoint();
    void recover(const RecoverCriteria& criteria);
    
    // Transaction lifecycle with proper WAL protocol
    void commit_transaction(int transaction_id);
    void abort_transaction(int transaction_id);

    // Untuk testing purposes
    std::vector<LogEntry> read_all_logs_public(const std::string& file_path);
    void debug_run_crash_recovery();
    void reset_state_for_testing();
    void flush_logs_for_testing() { flush_buffer(); }

    // Helper untuk store dan retrieve TableSchema <-> binary
    // Made public for testing purposes
    void write_schema(std::ofstream& out, const TableSchema& schema);
    TableSchema read_schema(std::ifstream& in);
    
    // Fungsi menulis log ke file binary (jangan lupa std::ios::app supaya append bukan rewrite!)
    // BEGIN, COMMIT: Atribut table_name, old_value, new_value dianggap tidak ada, sisanya ada
    // ABORT: Atribut table_name, old_value, new_value, query dianggap tidak ada, sisanya ada
    // CHECKPOINT: Atribut old_value, new_value, query dianggap tidak ada, sisanya ada
    // INSERT: Atribut old_value dianggap tidak ada, sisanya ada
    // DELETE: Atribut new_value dianggap tidak ada, sisanya ada
    // UPDATE: Seluruh atribut ada
    // CREATE_TABLE/DROP_TABLE table_name, old_value, new_value dianggap tidak ada, sisanya ada/opsional
    
    // Made public for testing purposes
    void write_log_to_file(std::ofstream& out, const LogEntry& entry);
    LogEntry read_log_from_file(std::ifstream& in);

    // Fungsi baru untuk menulis log format teks
    // Made public for testing purposes
    void write_log_to_text_file(std::ofstream& out, const LogEntry& entry);

private:
    FailureRecoveryManager();
    const size_t MAX_BUFFER_SIZE = 50;
    std::vector<LogEntry> log_buffer;
    std::set<int> active_transactions_cache;
    std::vector<CheckpointInfo> checkpoints;
    std::string log_file_path;
    std::mutex mtx;
    int next_log_id;
    int next_checkpoint_id;
    sm::StorageEngine& storage_engine_;
    
    // Periodic checkpoint thread
    std::thread checkpoint_thread_;
    std::atomic<bool> running_;
    const int CHECKPOINT_INTERVAL_SECONDS = 300; // 5 minutes
    
    void checkpoint_worker();
    
    // Helper convert struct Operation
    std::string operation_to_string(Operation op);
    
    // Helper untuk storage manager integration
    std::vector<Condition> row_to_conditions(const Row& row, const std::string& table_name);
    std::any string_to_any(const std::string& str);
    
    // ========================================================= BINARY LOG ========================================================================

    // Helper untuk store dan retrieve std::string <-> binary
    void write_string(std::ofstream& out, const std::string& str);
    std::string read_string(std::ifstream& in);

    // Helper untuk store dan retrieve std::any <-> binary
    // Diberikan tanda tipe data, integer:1, float:2, string:3
    void write_any(std::ofstream& out, const std::any& val);
    std::any read_any(std::ifstream& in);

    // Helper untuk store dan retrieve columns (std::unordered_map<std::string, std::any>) <-> binary
    void write_columns(std::ofstream& out, const std::unordered_map<std::string, std::any>& columns);
    std::unordered_map<std::string, std::any> read_columns(std::ifstream& in);

    // Helper untuk store dan retrieve Row <-> binary
    void write_row(std::ofstream& out, const Row& row);
    Row read_row(std::ifstream& in);

    // Fungsi membaca seluruh file log binary
    std::vector<LogEntry> read_all_logs(const std::string& file_path);

    // ============================================================== TEXT LOG =======================================================================

    // Helper konversi ke string agar bisa dibaca manusia
    std::string any_to_string(const std::any& val);
    std::string row_to_string(const Row& row);
    std::string schema_to_string(const TableSchema& schema);

    std::string sanitize_for_log(std::string input);

    // ===============================================================================================================================================
    
    // Helper untuk parsing checkpoint list dari query string
    std::set<int> parse_checkpoint_list(const std::string& query);
    
    // Helper untuk melakukan UNDO operasi berdasarkan log entry
    bool undo_operation(const LogEntry& entry);

    // Helper untuk melakukan REDO operasi berdasarkan log entry
    bool redo_operation(const LogEntry& entry);

    void recover_from_crash();
    
    void flush_buffer();
};

} // namespace mdbms::fr
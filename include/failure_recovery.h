#pragma once
#include <vector>
#include <ctime>
#include <string>
#include <sstream>
#include <mutex>
#include <set>
#include "types.h"

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

    // ===============================================================================================================================================
    // Fungsi akal-akalan supaya public (untuk testing saja)
    std::vector<LogEntry> read_all_logs_public(const std::string& file_path);
    // ===============================================================================================================================================

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
    
    // Helper convert struct Operation
    std::string operation_to_string(Operation op);
    
    // ========================================================= BINARY LOG ========================================================================

    // Helper untuk store dan retrieve std::string <-> binary
    void write_string(std::ofstream& out, const std::string& str);
    std::string read_string(std::ifstream& in);

    // Helper untuk store dan retrieve std::any <-> binary
    // Diberikan tanda tipe data, integer:1, float:2, string:3
    void write_any(std::ofstream& out, const std::any& val);
    std::any read_any(std::ifstream& in);

    // Helper untuk store dan retrieve columns (std::map<std::string, std::any>) <-> binary
    void write_columns(std::ofstream& out, const std::map<std::string, std::any>& columns);
    std::map<std::string, std::any> read_columns(std::ifstream& in);

    // Helper untuk store dan retrieve Row <-> binary
    void write_row(std::ofstream& out, const Row& row);
    Row read_row(std::ifstream& in);

    // Helper untuk retrieve LogEntry <-> binary
    LogEntry read_log_from_file(std::ifstream& in);
    
    // Fungsi menulis log ke file binary (jangan lupa std::ios::app supaya append bukan rewrite!)
    // BEGIN, COMMIT: Atribut table_name, old_value, new_value dianggap tidak ada, sisanya ada
    // ABORT: Atribut table_name, old_value, new_value, query dianggap tidak ada, sisanya ada
    // CHECKPOINT: Atribut old_value, new_value, query dianggap tidak ada, sisanya ada
    // INSERT: Atribut old_value dianggap tidak ada, sisanya ada
    // DELETE: Atribut new_value dianggap tidak ada, sisanya ada
    // UPDATE: Seluruh atribut ada
    void write_log_to_file(std::ofstream& out, const LogEntry& entry);

    // Fungsi membaca seluruh file log binary
    std::vector<LogEntry> read_all_logs(const std::string& file_path);

    // ============================================================== TEXT LOG =======================================================================

    // Helper konversi ke string agar bisa dibaca manusia
    std::string any_to_string(const std::any& val);
    std::string row_to_string(const Row& row);

    std::string sanitize_for_log(std::string input);

    // Fungsi baru untuk menulis log format teks
    void write_log_to_text_file(std::ofstream& out, const LogEntry& entry);

    // ===============================================================================================================================================
    
    // Helper untuk melakukan UNDO operasi berdasarkan log entry
    // Baru undo karena untuk milestone 2 fokus ke transaction abort recovery. Redo diperlukan buat system failure nanti
    bool undo_operation(const LogEntry& entry);
    
    void flush_buffer();
};

} // namespace mdbms::fr
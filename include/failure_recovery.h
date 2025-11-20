#pragma once
#include <vector>
#include <ctime>
#include <string>
#include <sstream>
#include <mutex>
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

private:
    FailureRecoveryManager();
    std::vector<LogEntry> log_buffer;
    std::vector<CheckpointInfo> checkpoints;
    std::string log_file_path;
    std::mutex mtx;
    int next_log_id;
    int next_checkpoint_id;
    
    // Helper untuk convert std::any ke string dan sebaliknya
    std::string any_to_string(const std::any& value);
    std::any string_to_any(const std::string& str, DataType type);
    
    // Format: ROW_ID#{atr1:val1,atr2:val2,...}
    // TODO: handling jika value atribut terdapat ",", ":", "#", "{", "}"
    std::string row_to_string(const Row& row);
    
    // Catatan: saat ini semua value dianggap sebagai string, karena tidak memiliki informasi TableSchema/DataType
    Row string_to_row(const std::string& row_string, const std::string& table_name);
    
    // Helper convert struct Operation
    std::string operation_to_string(Operation op);
    Operation string_to_operation(const std::string& str);
    
    // Format: LOG_ID | TX_ID | TIMESTAMP | OP | TABLE | OLD_VAL | NEW_VAL | QUERY
    // Catatan: TABLE = "NON_TABLE" untuk operasi BEGIN, COMMIT (karena TABLE harusnya kosong)
    // TODO: handling jika ada yang memiliki value "|"
    std::string serialize_log(const LogEntry& entry);
    LogEntry deserialize_log(const std::string& serialized_log);
    
    // Fungsi menulis dan membaca log file
    void append_log_to_file(const std::string& serialized_log, const std::string& file_path);
    std::vector<LogEntry> read_all_logs(const std::string& file_path);
    
    void flush_buffer();
};

} // namespace mdbms::fr
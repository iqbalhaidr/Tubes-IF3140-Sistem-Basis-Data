#pragma once

#include <string>
#include <vector>
#include <any>

namespace mdbms {

// Tempat untuk struct/tipe data yang dipakai bersama
// Hanya contoh sementara, silakan diubah sesuai kebutuhan

struct Rows {
    int rows_count;
    std::vector<std::any> data; // Sesuaikan tipe datanya
};

struct ExecutionResult {
    int transaction_id;
    std::string message;
    std::string query;
    Rows data;
};

struct Row {
    std::string table_name;
    std::map<std::string, std::any> columns;
    int row_id;

    Row() : row_id(-1) {}
};

// Failure Recovery
enum class Operation {
    BEGIN,
    COMMIT,
    ABORT,
    UPDATE,
    INSERT,
    DELETE
};

struct RecoverCriteria {
    std::time_t timestamp;
    int transaction_id;
    bool use_timestamp;

    RecoverCriteria() : timestamp(0), transaction_id(-1), use_timestamp(false) {}
};

struct LogEntry {
    int log_id;
    int transaction_id;
    std::time_t timestamp;
    Operation operation;
    std::string table_name;
    Row old_value;
    Row new_value;
    std::string query;

    LogEntry() : log_id(-1), transaction_id(-1), timestamp(std::time(nullptr)) {}
};

struct CheckpointInfo {
    int checkpoint_id;
    std::time_t timestamp;     
    std::vector<int> active_transactions;

    CheckpointInfo() : checkpoint_id(-1), timestamp(std::time(nullptr)) {}
};

} // namespace mdbms

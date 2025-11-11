#pragma once

#include <any>
#include <map>
#include <string>
#include <vector>

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

enum class Action { 
    READ, 
    WRITE 
};

enum class TransactionStatus {
    ACTIVE,
    PARTIALLY_COMMITTED,
    COMMITTED,
    FAILED,
    ABORTED,
    TERMINATED
};

struct Row {
    std::string table_name;
    std::map<std::string, std::any> columns;
    int row_id;

    Row() : row_id(-1) {}
};

struct Response {
    bool allowed;
    int transaction_id;
    std::string reason;

    Response() : allowed(false), transaction_id(-1) {}
    Response(bool allow, int tid) : allowed(allow), transaction_id(tid) {}
};

} // namespace mdbms
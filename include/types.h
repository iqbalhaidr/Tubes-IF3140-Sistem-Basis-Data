#pragma once

#include <any>
#include <ctime>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace mdbms {

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

enum class IndexType {
    BTREE,
    HASH
};

enum class SearchType {
    LINEAR,
    INDEX_SCAN
};

// Nanti sesuain sama kelompok storage manager
enum class DataType {
    INTEGER,
    FLOAT,
    CHAR,
    VARCHAR
};

struct Row {
    std::string table_name;
    std::unordered_map<std::string, std::any> columns;
    int row_id;

    Row() : row_id(-1) {}
};

template <typename T = Row>
struct Rows {
    std::vector<T> data;
    int rows_count;
    std::vector<std::string> column_names;

    Rows() : rows_count(0) {}

    explicit Rows(const std::vector<T>& rows) : data(rows), rows_count(static_cast<int>(rows.size())) {}
};

struct ExecutionResult {
    int transaction_id;
    std::time_t timestamp;
    std::string message;
    std::string query;
    Rows<Row> data;
    int affected_rows;
    bool success;
    std::string table_name;

    ExecutionResult()
        : transaction_id(-1),
          timestamp(std::time(nullptr)),
          affected_rows(0),
          success(false),
          table_name("") {}
};

// Concurrency Control
struct Response {
    bool allowed;
    int transaction_id;

    Response() : allowed(false), transaction_id(-1) {}
    Response(bool allow, int tid) : allowed(allow), transaction_id(tid) {}
};

// Storage Manager
struct Condition {
    std::string column;
    std::string operation;
    std::any operand;

    Condition() = default;
    Condition(const std::string& col, const std::string& op, const std::any& val)
        : column(col), operation(op), operand(val) {}
};

struct DataRetrieval {
    std::string table;
    std::vector<std::string> columns;
    std::vector<Condition> conditions;
    SearchType search_type;
    std::string index_column;  // Kolom yang digunakan untuk index (jika ada)

    DataRetrieval() : search_type(SearchType::LINEAR) {}
};

template <typename T = Row>
struct DataWrite {
    std::string table;
    std::vector<std::string> columns;  // Kolom yang akan di-update (untuk UPDATE)
    std::vector<Condition> conditions;  // Kondisi WHERE (untuk UPDATE)
    T new_value;  // Nilai baru (Row untuk INSERT, nilai untuk UPDATE)
    bool is_insert;  // true = INSERT, false = UPDATE

    DataWrite() : is_insert(false) {}
};

struct DataDeletion {
    std::string table;
    std::vector<Condition> conditions;

    DataDeletion() = default;
    explicit DataDeletion(const std::string& tbl) : table(tbl) {}
};

struct Statistic {
    std::string table_name;
    int n_r;  // Number of tuples in relation r
    int b_r;  // Number of blocks containing tuples of r
    int l_r;  // Size of tuple of r (bytes)
    int f_r;  // Blocking factor
    std::map<std::string, int> V_a_r;  // Distinct values untuk setiap atribut A

    Statistic() : n_r(0), b_r(0), l_r(0), f_r(0) {}
};

struct TableSchema {
    std::string table_name;
    std::vector<std::string> column_names;
    std::vector<DataType> column_types;
    std::vector<int> column_sizes;  // Ukuran kolom (untuk CHAR/VARCHAR)
    std::string primary_key;  // Nama kolom primary key
    std::map<std::string, std::string> foreign_keys;  // <kolom, tabel_referensi.kolom>

    TableSchema() = default;
};

// Failure Recovery
enum class Operation {
    BEGIN,
    COMMIT,
    ABORT,
    UPDATE,
    INSERT,
    DELETE,
    CHECKPOINT,
    CREATE_TABLE,
    DROP_TABLE
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
    std::optional<TableSchema> dropped_schema;
    std::optional<TableSchema> created_schema;

    LogEntry() : log_id(-1), transaction_id(-1), timestamp(std::time(nullptr)) {}
};

struct CheckpointInfo {
    int checkpoint_id;
    std::time_t timestamp;     
    std::vector<int> active_transactions;

    CheckpointInfo() : checkpoint_id(-1), timestamp(std::time(nullptr)) {}
};

}  // namespace mdbms

#pragma once

#include <string>
#include <vector>
#include <any>
#include <map>
#include <variant>
#include <stdexcept>
#include <ctime>

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
    std::map<std::string, std::any> columns; 
    int row_id;                                

    Row() : row_id(-1) {}
};


template<typename T = Row>
struct Rows {
    std::vector<T> data;
    int rows_count; 
    std::vector<std::string> column_names;

    Rows() : rows_count(0) {}

    Rows(const std::vector<T>& rows) : data(rows), rows_count(rows.size()) {}
};

struct ExecutionResult {
    int transaction_id;   
    std::time_t timestamp;
    std::string message;
    std::string query;
    Rows<Row> data;
    int affected_rows;
    bool success;

    ExecutionResult() : transaction_id(-1), timestamp(std::time(nullptr)), affected_rows(0), success(false) {}
};


// Concurrency Control
struct Response {
    bool allowed;            
    int transaction_id;       

    Response() : allowed(false), transaction_id(-1) {}
    Response(bool allow, int tid) : allowed(allow), transaction_id(tid) {}
};

struct LockInfo {
    int transaction_id;         
    Action lock_type;           // Tipe lock (READ/WRITE)
    std::time_t timestamp;      // Waktu lock diberikan

    LockInfo() : transaction_id(-1), lock_type(Action::READ), timestamp(std::time(nullptr)) {}
};

// Storage Manager
struct Condition {
    std::string column;      
    std::string operation;    
    std::any operand;    

    Condition() {}
    Condition(const std::string& col, const std::string& op, const std::any& val) : column(col), operation(op), operand(val) {}
};

struct DataRetrieval {
    std::string table;                    
    std::vector<std::string> columns;      
    std::vector<Condition> conditions;    
    SearchType search_type;                 
    std::string index_column; // Kolom yang digunakan untuk index (jika ada)

    DataRetrieval() : search_type(SearchType::LINEAR) {}
};

template<typename T = Row>
struct DataWrite {
    std::string table;                    
    std::vector<std::string> columns;       // Kolom yang akan di-update (untuk UPDATE)
    std::vector<Condition> conditions;      // Kondisi WHERE (untuk UPDATE)
    T new_value;                           // Nilai baru (Row untuk INSERT, nilai untuk UPDATE)

    DataWrite(){}
};

struct DataDeletion {
    std::string table;                    
    std::vector<Condition> conditions;

    DataDeletion() {}
    DataDeletion(const std::string& tbl) : table(tbl) {}
};

struct Statistic {
    std::string table_name;      
    int n_r;                                // Number of tuples in relation r
    int b_r;                                // Number of blocks containing tuples of r
    int l_r;                                // Size of tuple of r (bytes)
    int f_r;                                // Blocking factor 
    std::map<std::string, int> V_a_r;       // Distinct values untuk setiap atribut A

    Statistic() : n_r(0), b_r(0), l_r(0), f_r(0) {}
};

struct TableSchema {
    std::string table_name;                
    std::vector<std::string> column_names; 
    std::vector<DataType> column_types; 
    std::vector<int> column_sizes;                    // Ukuran kolom (untuk CHAR/VARCHAR)
    std::string primary_key;                          // Nama kolom primary key
    std::map<std::string, std::string> foreign_keys;  // <kolom, tabel_referensi.kolom>

    TableSchema() {}
};

// Failure Recovery
struct RecoverCriteria {
    std::time_t timestamp;      // Recovery hingga timestamp tertentu
    int transaction_id;         // Recovery untuk transaction ID tertentu 
    bool use_timestamp;         // true jika menggunakan timestamp, false jika menggunakan transaction_id

    RecoverCriteria() : timestamp(0), transaction_id(-1), use_timestamp(false) {}
};

struct LogEntry {
    int log_id;                 // ID unik log entry
    int transaction_id;         // ID transaksi
    std::time_t timestamp;      // Waktu log dibuat
    std::string operation;      // Jenis operasi: BEGIN, COMMIT, ABORT, UPDATE, INSERT, DELETE
    std::string table_name;     // Nama tabel yang terpengaruh
    Row old_value;             // Nilai lama (untuk UNDO)
    Row new_value;             // Nilai baru (untuk REDO)
    std::string query;          // Query yang dieksekusi

    LogEntry() : log_id(-1), transaction_id(-1), timestamp(std::time(nullptr)) {}
};

struct CheckpointInfo {
    int checkpoint_id;        
    std::time_t timestamp;      
    std::vector<int> active_transactions;  // Daftar transaksi yang aktif saat checkpoint

    CheckpointInfo() : checkpoint_id(-1), timestamp(std::time(nullptr)) {}
};

} // namespace mdbms
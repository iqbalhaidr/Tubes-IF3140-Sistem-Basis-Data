#pragma once

#include <string>
#include <vector>
#include <any>
#include <memory> // Untuk shared_ptr / weak_ptr
#include <variant> // Untuk operand

namespace mdbms {

// --- Tipe Dasar (Dari Spesifikasi Query Processor & Storage Manager) ---

// Row type definition
using Row = std::vector<std::any>;

// Enum for action types in concurrency control
enum class Action {
    READ,
    WRITE
};

// Enum for concurrency control response
enum class Response {
    ALLOW,
    DENY
};

/**
 * @struct Condition
 * @brief Merepresentasikan satu kondisi di klausa WHERE (misal: 'harga > 1000')
 * @cite: 81
 */
struct Condition {
    std::string column;
    std::string operation; // enum("=", "<>", ">", ">=", "<", "<=")
    std::any operand; // Changed from variant to std::any for flexibility
};

/**
 * @struct Rows
 * @brief Merepresentasikan hasil data tabular.
 * @cite: 24
 */
struct Rows {
    int rows_count;
    // std::any bisa jadi sulit dikelola.
    // Mari kita asumsikan (untuk implementasi ini)
    // data adalah matriks dari std::any.
    // vector<Row>, dimana Row = vector<std::any>
    std::vector<std::vector<std::any>> data;
    
    // Anda juga perlu metadata kolom (nama, tipe) di sini
    // std::vector<std::string> column_names;
};

/**
 * @struct ExecutionResult
 * @brief Objek hasil standar yang dikembalikan oleh QP.
 * @cite: 24
 */
struct ExecutionResult {
    int transaction_id;
    std::string message;
    std::string query;
    Rows data;
    // 'timestamp' dihilangkan untuk kesederhanaan,
    // sesuai implementasi stub Anda
};


// --- Tipe Query Optimizer (Juga digunakan oleh QP) ---

struct QueryTree; // Forward declaration

/**
 * @struct QueryTree
 * @brief Node dalam pohon query plan.
 * @cite: 24, 53
 */
struct QueryTree {
    std::string type; // misal: "SELECT", "WHERE_CLAUSE", "COLUMN_LIST"
    std::string value; // misal: "Student", "salary", ">"
    std::vector<std::shared_ptr<QueryTree>> children;
    std::weak_ptr<QueryTree> parent;
};

/**
 * @struct ParsedQuery
 * @brief Objek yang dikembalikan QO, berisi query plan.
 * @cite: 24, 53
 */
struct ParsedQuery {
    std::string query;
    std::shared_ptr<QueryTree> query_tree;
};


// --- Tipe Storage Manager (Digunakan oleh QP untuk meminta data) ---

/**
 * @struct DataRetrieval
 * @brief Permintaan untuk membaca data.
 * @cite: 81
 */
struct DataRetrieval {
    std::vector<std::string> tables;
    std::vector<std::string> columns;
    std::vector<Condition> conditions;
};

/**
 * @struct DataWrite
 * @brief Permintaan untuk menulis (memodifikasi/insert) data.
 * @cite: 81
 */
struct DataWrite {
    std::string table;
    std::vector<Condition> conditions; // Untuk mencari baris yang akan di-update
    std::vector<std::string> columns_to_update;
    std::vector<std::any> new_values; // Nilai baru
};

/**
 * @struct DataDeletion
 * @brief Permintaan untuk menghapus data.
 * @cite: 81
 */
struct DataDeletion {
    std::string table;
    std::vector<Condition> conditions;
};

} // namespace mdbms
#pragma once

#include <string>
#include <vector>
#include <any>
#include <map>
#include <variant>
#include <stdexcept>

namespace mdbms {

    // operand dalam kondisi, bisa int atau string
    // misal WHERE StudentID = 101, maka mdbms::ConditionOperand id_val = 101
    using ConditionOperand = std::variant<int, float, std::string>;

    // merepresentasikan satu baris data
    using RowData = std::map<std::string, ConditionOperand>;

    // hasil pembacaan data
    struct Rows {
        std::vector<RowData> data;
        int rows_count = 0;
    };

    // Enum untuk operasi perbandingan
    enum class OpType {
        EQ, // =
        NEQ, // <>
        GT, // >
        GTE, // >=
        LT, // <
        LTE // <=
    };

    // Merepresentasikan satu kondisi WHERE
    struct Condition {
        std::string column;
        OpType operation;
        ConditionOperand operand;
    };

    // untuk permintaan read_block()
    struct DataRetrieval {
        std::string table;
        std::vector<std::string> columns;
        std::vector<Condition> conditions;
    };

    // untuk permintaan write_block()
    // Untuk INSERT: new_values berisi data lengkap, conditions kosong
    // Untuk UPDATE: new_values berisi kolom yg diubah, conditions TIDAK kosong
    struct DataWrite {
        std::string table;
        RowData new_values;
        std::vector<Condition> conditions;
    };

    // untuk permintaan delete_block()
    struct DataDeletion {
        std::string table;
        std::vector<Condition> conditions;
    };

    // Tipe data untuk skema
    enum class DataType {
        INT,
        FLOAT,
        STRING
    };

    struct ColumnSchema {
        std::string name;
        DataType type;
        int length; // Untuk varchar/char
    };

    using TableSchema = std::vector<ColumnSchema>;

    struct ExecutionResult {
        int transaction_id;
        std::string message;
        std::string query;
        Rows data;
    };

} // namespace mdbms

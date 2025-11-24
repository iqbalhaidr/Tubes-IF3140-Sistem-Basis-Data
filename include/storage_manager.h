#pragma once
#include "types.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <any>

namespace mdbms::sm {

class StorageEngine {
public:
    StorageEngine();
    StorageEngine(const std::string& data_dir);
    
    static StorageEngine& get_instance();

    Rows<Row> read_block(const DataRetrieval& retrieval);
    int write_block(const DataWrite<Row>& write);
    int delete_block(const DataDeletion& deletion);
    void set_index(const std::string& table, const std::string& column, const IndexType index_type);

    std::unordered_map<std::string, std::string> table_index;      // table -> column
    std::unordered_map<std::string, IndexType> table_index_type;   // table -> type

private:
    std::string data_dir_;

    TableSchema getSchema(const std::string& table);
    void serialize_row(std::ostream& out, const Row& row, const TableSchema& schema);
    Row deserialize_row(std::istream& in, const TableSchema& schema);
    bool check_conditions(const Row& row, const std::vector<Condition>& conditions);

    // Indexing
    void build_hash_index(const TableSchema& schema, const std::string& table, const std::string& column);
    void update_index_after_insert(const std::string& table, const std::string& column, int64_t row_offset, const Row& row);
    void update_index_after_update(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row, const Row& new_row);
    void update_index_after_delete(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row);

    // Read Block
    bool lookup_index(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets);
    bool lookup_hash(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets);
    Rows<Row> read_using_offsets(const DataRetrieval& retrieval, const std::vector<int64_t>& offsets);
    Rows<Row> full_scan(const DataRetrieval& retrieval);
    
    // Helper
    bool any_to_int32(const std::any &a, int32_t &out);
    bool any_to_float(const std::any &a, float &out);
    bool any_to_string(const std::any &a, std::string &out);
};

} // namespace mdbms::sm

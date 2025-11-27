#pragma once
#include "types.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <any>
#include <optional>

namespace mdbms::sm {

class HashIndexEngine {
public:
    HashIndexEngine();
    HashIndexEngine(const std::string& data_dir);

    std::unordered_map<std::string, std::string> table_index;      // table -> column
    std::unordered_map<std::string, IndexType> table_index_type;   // table -> type

    void build_hash_index(const TableSchema& schema, const std::string& table, const std::string& column);
    void update_index_after_insert(const std::string& table, const std::string& column, int64_t row_offset, const Row& row);
    void update_index_after_update(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row, const Row& new_row);
    void update_index_after_delete(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row);
    bool lookup_index(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets);
    bool lookup_hash(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets);

private:
    std::string data_dir_;
    
    // Helper
    bool any_to_int32(const std::any &a, int32_t &out);
    bool any_to_float(const std::any &a, float &out);
    bool any_to_string(const std::any &a, std::string &out);
    Row deserialize_row(std::istream& in, const TableSchema& schema); // duplicate because im not sure what the best way for this
};

class StorageEngine {
public:
    StorageEngine();
    StorageEngine(const std::string& data_dir);

    // CRUD
    Rows<Row> read_block(const DataRetrieval& retrieval);
    Rows<Row> read_using_offsets(const DataRetrieval& retrieval, const std::vector<int64_t>& offsets);
    Rows<Row> full_scan(const DataRetrieval& retrieval);
    int write_block(const DataWrite<Row>& write);
    int delete_block(const DataDeletion& deletion);
    
    // schema manager
    std::vector<TableSchema> get_tables();
    TableSchema get_table_schema(const std::string& table);
    bool create_table(const TableSchema& schema);
    bool delete_table(const TableSchema& schema);
    
    // other
    std::map<std::string, Statistic> get_stats();
    void set_index(const std::string& table, const std::string& column, const IndexType index_type);
    
    // Helper methods for integration
    std::vector<std::string> get_column_names(const std::string& table);
    bool drop_table(const std::string& table);
    std::optional<Statistic> build_dummy_get_stat(const std::string& table) const;
    static StorageEngine& get_instance();

    
private:
    std::string data_dir_;
    HashIndexEngine hash_index_engine; // for now adanya hash dulu, btree masih rusak, nanti dibuat interface yg gabungan

    // CRUD helper
    void serialize_row(std::ostream& out, const Row& row, const TableSchema& schema);
    Row deserialize_row(std::istream& in, const TableSchema& schema);
    bool check_conditions(const Row& row, const std::vector<Condition>& conditions);
    bool check_and_update_tables_metadata(const TableSchema& schema);

    // Other Helper
    std::string to_string_any(const std::any& a);

};

} // namespace mdbms::sm

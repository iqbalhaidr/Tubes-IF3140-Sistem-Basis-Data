#pragma once
#include "types.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>

namespace mdbms::sm {

class StorageEngine {
public:
    StorageEngine();
    StorageEngine(const std::string& data_dir);
    Rows<Row> read_block(const DataRetrieval& retrieval);
    int write_block(const DataWrite<Row>& write);
    int delete_block(const DataDeletion& deletion);
    void set_index(const std::string& table, const std::string& column, const IndexType index_type);

private:
    std::string data_dir_;
    TableSchema getSchema(const std::string& table);
    void serialize_row(std::ostream& out, const Row& row, const TableSchema& schema);
    Row deserialize_row(std::istream& in, const TableSchema& schema);
    bool check_conditions(const Row& row, const std::vector<Condition>& conditions);
    void build_hash_index(const TableSchema& schema, const std::string& table, const std::string& column);

    // helper
    bool any_to_int32(const std::any &a, int32_t &out);
    bool any_to_float(const std::any &a, float &out);
    bool any_to_string(const std::any &a, std::string &out);
};

} // namespace mdbms::sm

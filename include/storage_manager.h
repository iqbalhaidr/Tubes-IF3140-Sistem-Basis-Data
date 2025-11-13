#pragma once
#include "types.h"
#include <string>
#include <fstream>

namespace mdbms::sm {

class StorageEngine {
public:
    // Constructor
    StorageEngine(const std::string& data_dir);
    Rows<Row> read_block(const DataRetrieval& retrieval);
    int write_block(const DataWrite<Row>& write);
    int delete_block(const DataDeletion& deletion);

private:
    std::string data_dir_;
    TableSchema getSchema(const std::string& table);
    void serialize_row(std::ostream& out, const Row& row, const TableSchema& schema);
    Row deserialize_row(std::istream& in, const TableSchema& schema);
    bool check_conditions(const Row& row, const std::vector<Condition>& conditions);
};

} // namespace mdbms::sm

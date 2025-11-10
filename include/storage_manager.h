#pragma once
#include "types.h"
#include <string>
#include <fstream>

namespace mdbms::sm {

class StorageEngine {
public:
    // Constructor
    StorageEngine(const std::string& data_dir);

    Rows read_block(const DataRetrieval& retrieval);
    int write_block(const DataWrite& write);
    int delete_block(const DataDeletion& deletion);

private:
    std::string data_dir_;

    // Mengambil skema tabel
    TableSchema getSchema(const std::string& table);

    void serialize_row(std::ostream& out, const RowData& row, const TableSchema& schema);

    RowData deserialize_row(std::istream& in, const TableSchema& schema);

    bool check_conditions(const RowData& row, const std::vector<Condition>& conditions);
};

} // namespace mdbms::sm
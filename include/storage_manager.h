#pragma once
#include "types.h"

namespace mdbms::sm {

    // Definisikan DataRetrieval, DataWrite, dll. di sini atau di types.h jika dipakai bersama

    class StorageEngine {
       public:
        Rows<Row> read_block(const DataRetrieval& retrieval);
        int write_block(const DataWrite<Row>& write);
        int delete_block(const DataDeletion& deletion);
        bool has_index(const std::string& table, const std::string& column);
    };

}  // namespace mdbms::sm

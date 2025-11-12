#pragma once
#include "types.h"

namespace mdbms::sm {

// Definisikan DataRetrieval, DataWrite, dll. di sini atau di types.h jika dipakai bersama

class StorageEngine {
public:
    // Rows read_block(/* DataRetrieval retrieval */);
    int write_block(/* DataWrite write */);
    int delete_block(/* DataDeletion deletion */);
};

} // namespace mdbms::sm

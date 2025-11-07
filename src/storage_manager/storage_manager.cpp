#include "storage_manager.h"
#include <iostream>

namespace mdbms::sm {

Rows StorageEngine::read_block(/* ... */) {
    std::cout << "SM: Membaca block (stub)..." << std::endl;
    Rows r;
    r.rows_count = 0;
    return r;
}

int StorageEngine::write_block(/* ... */) {
    std::cout << "SM: Menulis block (stub)..." << std::endl;
    return 1;

int StorageEngine::delete_block(/* ... */) {
    std::cout << "SM: Menghapus block (stub)..." << std::endl;
    return 1;
}

} // namespace mdbms::sm

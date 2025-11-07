#include "concurrency_control.h"
#include <iostream>

namespace mdbms::ccm {

int ConcurrencyControlManager::begin_transaction() {
    std::cout << "CCM: Memulai transaksi (stub)..." << std::endl;
    return 1;
}

void ConcurrencyControlManager::log_object(/* ... */) {
    std::cout << "CCM: Logging objek (stub)..." << std::endl;
}

bool ConcurrencyControlManager::validate_object(/* ... */) {
    std::cout << "CCM: Memvalidasi objek (stub)..." << std::endl;
    return true;
}

void ConcurrencyControlManager::end_transaction(int transaction_id) {
    std::cout << "CCM: Mengakhiri transaksi " << transaction_id << " (stub)..." << std::endl;
}

} // namespace mdbms::ccm

#include "concurrency_control.h"
#include <iostream>

namespace mdbms::ccm {

int ConcurrencyControlManager::begin_transaction() {
    std::cout << "CCM: Memulai transaksi (stub)..." << std::endl;
    return 1;
}

void ConcurrencyControlManager::log_object(const Row& object, int transaction_id) {
    std::cout << "CCM: Logging objek (stub) untuk transaksi " << transaction_id
              << " pada tabel " << object.table_name << std::endl;
}

Response ConcurrencyControlManager::validate_object(const Row& object, int transaction_id, Action action) {
    std::cout << "CCM: Memvalidasi objek (stub) untuk transaksi " << transaction_id
              << " aksi " << (action == Action::READ ? "READ" : "WRITE")
              << " pada tabel " << object.table_name << std::endl;
    return Response(true, transaction_id);
}

void ConcurrencyControlManager::end_transaction(int transaction_id) {
    std::cout << "CCM: Mengakhiri transaksi " << transaction_id << " (stub)..." << std::endl;
}

} // namespace mdbms::ccm

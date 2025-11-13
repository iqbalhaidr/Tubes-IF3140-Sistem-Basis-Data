#include "failure_recovery.h"
#include <iostream>

namespace mdbms::fr {

void FailureRecoveryManager::write_log(const ExecutionResult& info) {
    std::cout << "FRM: Menulis log untuk query: " << info.query << " (stub)..." << std::endl;
}

void FailureRecoveryManager::save_checkpoint() {
    std::cout << "FRM: Menyimpan checkpoint (stub)..." << std::endl;
}

void FailureRecoveryManager::recover(const RecoverCriteria&) {
    std::cout << "FRM: Melakukan recovery (stub)..." << std::endl;
}

} // namespace mdbms::fr

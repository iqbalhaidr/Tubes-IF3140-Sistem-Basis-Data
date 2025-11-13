#pragma once
#include "types.h"
#include <vector>
#include <ctime>

namespace mdbms::fr {

class FailureRecoveryManager {
public:
    void write_log(const ExecutionResult& info);
    void save_checkpoint();
    void recover(const RecoverCriteria& criteria);
};

} // namespace mdbms::fr
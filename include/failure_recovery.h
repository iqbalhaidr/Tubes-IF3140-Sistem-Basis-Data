#pragma once
#include "types.h"

namespace mdbms::fr {

class FailureRecoveryManager {
public:
    void write_log(const ExecutionResult& info);
    void save_checkpoint();
    void recover(/* RecoverCriteria criteria */);
};

} // namespace mdbms::fr

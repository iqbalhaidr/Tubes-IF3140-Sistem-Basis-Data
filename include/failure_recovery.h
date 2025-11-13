#pragma once
#include "types.h"
#include <vector>
#include <ctime>
#include <string>

namespace mdbms::fr {

class FailureRecoveryManager {
public:
    void write_log(const ExecutionResult& info);
    void save_checkpoint();
    void recover(const RecoverCriteria& criteria);

private:
    std::vector<LogEntry> log_buffer;
    std::vector<CheckpointInfo> checkpoints;
    std::string log_file_path;
    int next_log_id;
    int next_checkpoint_id;
};

} // namespace mdbms::fr
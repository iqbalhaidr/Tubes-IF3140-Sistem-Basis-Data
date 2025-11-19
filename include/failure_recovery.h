#pragma once
#include <vector>
#include <ctime>
#include <string>
#include <mutex>
#include "types.h"

namespace mdbms::fr {

class FailureRecoveryManager {
public:
    static FailureRecoveryManager& get_instance();
    FailureRecoveryManager(const FailureRecoveryManager&) = delete;
    FailureRecoveryManager& operator=(const FailureRecoveryManager&) = delete;
    ~FailureRecoveryManager();
    void write_log(const ExecutionResult& info);
    void save_checkpoint();
    void recover(const RecoverCriteria& criteria);

private:
    FailureRecoveryManager();
    std::vector<LogEntry> log_buffer;
    std::vector<CheckpointInfo> checkpoints;
    std::string log_file_path;
    std::mutex mtx;
    int next_log_id;
    int next_checkpoint_id;
    void flush_buffer();
};

} // namespace mdbms::fr
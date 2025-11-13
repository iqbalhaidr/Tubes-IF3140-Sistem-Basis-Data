#pragma once
#include "types.h"

namespace mdbms::ccm {

class ConcurrencyControlManager {
public:
    int begin_transaction();
    void log_object(const Row& object, int transaction_id);
    Response validate_object(const Row& object, int transaction_id, Action action);
    void end_transaction(int transaction_id);

    // Mengubah concurrency control protocol
    void set_protocol(const std::string& protocol);
};

} // namespace mdbms::ccm
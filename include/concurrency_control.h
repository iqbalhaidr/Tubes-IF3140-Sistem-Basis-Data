#pragma once
#include "types.h"

namespace mdbms::ccm {

class ConcurrencyControlManager {
public:
    int begin_transaction();
    void log_object(/* ... */);
    bool validate_object(/* ... */);
    void end_transaction(int transaction_id);
};

} // namespace mdbms::ccm

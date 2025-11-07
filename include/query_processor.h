#pragma once
#include "types.h"

namespace mdbms::qp {

class QueryProcessor {
public:
    ExecutionResult execute_query(const std::string& query);
};

} // namespace mdbms::qp

#pragma once
#include "parsed_query.h"

namespace mdbms::qo {

class OptimizationEngine {
public:
    ParsedQuery parse_query(const std::string& query);
    ParsedQuery optimize_query(const ParsedQuery& query);
};

} // namespace mdbms::qo

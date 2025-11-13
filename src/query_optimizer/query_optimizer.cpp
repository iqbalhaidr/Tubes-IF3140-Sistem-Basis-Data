#include "query_optimizer.h"

namespace mdbms::qo {

ParsedQuery sql_parser(const std::string& query); // yang punya internal
QueryTree* plan_tree(const ParsedQuery& parsed);  // builder QueryTree

OptimizationEngine::OptimizationEngine() = default;
OptimizationEngine::~OptimizationEngine() = default;

ParsedQuery OptimizationEngine::parse_query(const std::string& query) { // ini buat public
    ParsedQuery parsed = ::mdbms::qo::sql_parser(query);

    // Build QueryTree from parsed query
    parsed.query_tree = plan_tree(parsed);
    
    return parsed;
}

ParsedQuery OptimizationEngine::optimize_query(const ParsedQuery& query) {
    ParsedQuery optimized = query;
    if (!optimized.query_tree) {
        return optimized;
    }

    // TODO(#optimizer): gunakan optimizer_rules setelah QueryTree siap diproses.
    return optimized;
}

} // namespace mdbms::qo

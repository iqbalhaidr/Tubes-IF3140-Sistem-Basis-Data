#include "query_optimizer.h"

namespace mdbms::qo {

OptimizationEngine& OptimizationEngine::get_instance() {
    static OptimizationEngine instance;
    return instance;
}

OptimizationEngine::OptimizationEngine() = default;
OptimizationEngine::~OptimizationEngine() = default;

ParsedQuery OptimizationEngine::parse_query(const std::string& query) {
    ParsedQuery parsed = sql_parser(query);
    parsed.query_tree = plan_tree(parsed);
    return parsed;
}

ParsedQuery OptimizationEngine::optimize_query(const ParsedQuery& query) {
    // DUMMY 
    ParsedQuery optimized = query;
    if (!optimized.query_tree) {
        return optimized;
    }

    if (query.query_tree) {
        optimized.query_tree = optimize_tree(
            query.query_tree->clone(),
            query.where_conditions,
            query.from_tables,
            query.select_columns
        );
        optimized.query_tree->estimated_cost = optimized.estimated_cost;
    } else {
        optimized.query_tree = new QueryTree();
        optimized.query_tree->type = "DUMMY";
        optimized.query_tree->value = "noop";
        optimized.query_tree->estimated_rows = 0;
        optimized.query_tree->estimated_cost = optimized.estimated_cost;
    }

    // TODO(#optimizer): gunakan optimizer_rules setelah QueryTree siap diproses.
    return optimized;
}

} // namespace mdbms::qo

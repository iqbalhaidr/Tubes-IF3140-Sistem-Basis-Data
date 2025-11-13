#include "query_optimizer.h"

namespace mdbms::qo {

namespace {

ParsedQuery build_dummy_parsed_query(const std::string& query) {
    ParsedQuery parsed;
    parsed.original_query = query;
    parsed.query_type = "SELECT";
    parsed.select_columns = {"*"};
    parsed.from_tables = {"dummy_table"};
    parsed.order_by_column = "";
    parsed.order_ascending = true;
    parsed.limit_value = -1;
    parsed.estimated_cost = 0;

    auto* root = new QueryTree();
    root->type = "SELECT";
    root->value = "dummy_table";
    root->estimated_rows = 1;
    root->estimated_cost = 0;

    parsed.query_tree = root;
    return parsed;
}

}  // namespace

OptimizationEngine::OptimizationEngine() = default;
OptimizationEngine::~OptimizationEngine() = default;

// ParsedQuery OptimizationEngine::parse_query(const std::string& query) { // ini buat public
//     ParsedQuery parsed = ::mdbms::qo::sql_parser(query);

//     Build QueryTree from parsed query
//     parsed.query_tree = plan_tree(parsed);
    
//     return parsed;
// }

ParsedQuery OptimizationEngine::parse_query(const std::string& query) {
    return build_dummy_parsed_query(query);
}

ParsedQuery OptimizationEngine::optimize_query(const ParsedQuery& query) {
    ParsedQuery optimized = query;
    if (!optimized.query_tree) {
        return optimized;
    }

    if (query.query_tree) {
        optimized.query_tree = query.query_tree->clone();
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

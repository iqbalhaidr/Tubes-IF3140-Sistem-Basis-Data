#include "query_optimizer.h"
#include "plan_tree.h"
#include "optimizer_rules.h"
#include "sql_parser.h"
#include <iostream>

namespace mdbms::qo {

ParsedQuery OptimizationEngine::parse_query(const std::string& query) {
    ParsedQuery pq = parse_sql(query);
    return pq;
}

ParsedQuery OptimizationEngine::optimize_query(const ParsedQuery& query) {
    std::cout << "QO: Optimizing query..." << std::endl;

    ParsedQuery optimized = query;
    if (!optimized.query_tree) {
        std::cout << "QO: Tidak ada query tree yang bisa dioptimasi." << std::endl;
        return optimized;
    }
    optimized.query_tree = apply_optimizer_rules(optimized.query_tree);
    return optimized;
}

} // namespace mdbms::qo

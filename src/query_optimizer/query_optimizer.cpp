#include "query_optimizer.h"
#include <iostream>

namespace mdbms::qo {

ParsedQuery OptimizationEngine::parse_query(const std::string& query) {
    std::cout << "QO: Parsing query: " << query << std::endl;
    ParsedQuery pq;
    pq.query = query;
    return pq;
}

ParsedQuery OptimizationEngine::optimize_query(const ParsedQuery& query) {
    std::cout << "QO: Optimizing query..." << std::endl;
    return query;
}

} // namespace mdbms::qo

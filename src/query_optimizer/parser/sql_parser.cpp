#include "sql_parser.h"
#include "plan_tree.h"

namespace mdbms::qo {

ParsedQuery parse_sql(const std::string& query) {
    ParsedQuery pq;
    pq.query = normalize_sql(query);

    // debug
    const PlanSegments segments = parse_plan_segments(pq.query);


    // pq.query_tree = plan_tree(segments);
    
    return pq;
}

} // namespace mdbms::qo

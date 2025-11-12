#include "query_optimizer.h"

namespace mdbms::qo {

QueryTree* plan_tree(const ParsedQuery& parsed) {
    // TODO: bangun QueryTree langsung dari ParsedQuery (SELECT, JOIN, WHERE, dll).
    return parsed.query_tree;
}

} // namespace mdbms::qo

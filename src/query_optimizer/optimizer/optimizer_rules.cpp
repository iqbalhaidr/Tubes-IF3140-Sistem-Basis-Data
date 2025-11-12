#include "query_optimizer.h"

namespace mdbms::qo {

namespace {

QueryTree* passthrough(QueryTree* plan) {
    return plan;
}

} // namespace

QueryTree* splitting_conjunction(QueryTree* plan) {
    return plan;
}

QueryTree* pushdown_selection(QueryTree* plan) {
    return plan;
}

QueryTree* redundant_projection(QueryTree* plan) {
    return plan;
}

QueryTree* apply_optimizer_rules(QueryTree* plan) {
    if (!plan) {
        return nullptr;
    }
    // Placeholder pipeline; each stage currently no-ops.
    plan = splitting_conjunction(plan);
    plan = pushdown_selection(plan);
    plan = redundant_projection(plan);
    return passthrough(plan);
}

} // namespace mdbms::qo

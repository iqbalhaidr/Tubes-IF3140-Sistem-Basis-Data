#include "cost_model.h"

namespace mdbms::qo {

static int count_nodes(const QueryTree* n) {
    if (!n) return 0;
    int sum = 1;
    for (auto &c : n->children)
        sum += count_nodes(c.get());
    return sum;
}

int estimate_cost(const QueryTree& root) {
    return count_nodes(&root) * 10; // simple heuristic
}

}

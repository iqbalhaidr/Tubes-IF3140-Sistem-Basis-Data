#include "cost_model.h"

namespace mdbms::qo {
namespace {

int count_nodes(const QueryTree* node) {
    if (!node) {
        return 0;
    }

    int sum = 1;
    for (const auto& child : node->children) {
        sum += count_nodes(child.get());
    }
    return sum;
}

} // namespace

int estimate_cost(const QueryTree& root) {
    return count_nodes(&root) * 10; // simple heuristic
}

} // namespace mdbms::qo

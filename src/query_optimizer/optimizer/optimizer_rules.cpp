#include "query_optimizer.h"

namespace mdbms::qo {

namespace {

QueryTree* passthrough(QueryTree* plan) {
    return plan;
}

} // namespace

QueryTree* splitting_conjunction(QueryTree* plan) {
    if (!plan) return nullptr;

    for (auto& child : plan->children) {
        child = splitting_conjunction(child); // recursive
    }

    // hanya untuk SELECT
    if (plan->type == "SELECT") {
        std::string v = plan->value;
        size_t pos = v.find("AND");

        if (pos != std::string::npos) {
            // pecah kiri dan kanan
            std::string left = v.substr(0, pos);
            std::string right = v.substr(pos + 3);

            // trim spasi
            auto trim = [](std::string s){
                size_t start = s.find_first_not_of(" ");
                size_t end   = s.find_last_not_of(" ");
                return s.substr(start, end - start + 1);
            };

            left = trim(left);
            right = trim(right);

            // buat node kanan
            QueryTree* rightNode = new QueryTree();
            rightNode->type = "SELECT";
            rightNode->value = right;
            rightNode->children = plan->children; 

            // node kiri menggantikan plan
            plan->value = left;
            plan->children.clear();
            plan->add_child(rightNode);
        }
    }

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

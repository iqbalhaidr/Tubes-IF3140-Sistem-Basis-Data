#include "plan_tree.h"

namespace mdbms::qo {

std::unique_ptr<QueryTree> plan_tree(const ParsedQuery& pq)
{
    auto root = std::make_unique<QueryTree>("PROJECT", "cols");

    QueryTree* curr = root->add_child(
        std::make_unique<QueryTree>("SCAN", pq.from_tables[0])
    );

    return root;
}

}

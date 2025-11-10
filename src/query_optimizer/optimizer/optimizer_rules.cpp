#include "optimizer_rules.h"

namespace mdbms::qo {


// ini buat contoh aja, sebenarnya bebas optimizer nya bentukan nya kea gimana, yang penting dia bikin QueryTree yang optimal (optimized_tree)  
std::unique_ptr<QueryTree> splitting_conjunction(const QueryTree& plan) {
    // TODO


    return std::make_unique<QueryTree>(plan);
}

std::unique_ptr<QueryTree> pushdown_selection(const QueryTree& plan) {
    // TODO


    return std::make_unique<QueryTree>(plan);
}

std::unique_ptr<QueryTree> redundant_projection(const QueryTree& plan) {
    // TODO


    return std::make_unique<QueryTree>(plan);
}


// ini nanti dia connect ke optimizer_rules yang ada di query_processor.cpp

std::unique_ptr<QueryTree> apply_optimizer_rules(const QueryTree& plan) {
    // auto current_plan = split_conjunction(plan);
    // current_plan = pushdown_selection(*current_plan);
    // current_plan = redundant_projection(*current_plan);
    // return current_plan;
}

}

#include "optimizer_rules.h"

namespace mdbms::qo {
namespace {


} // namespace

// ini buat contoh aja, sebenarnya bebas optimizer nya bentukan nya kea gimana,
// yang penting dia bikin QueryTree yang optimal (optimized_tree)
QueryTreePtr splitting_conjunction(const QueryTreePtr& plan) {

}

QueryTreePtr pushdown_selection(const QueryTreePtr& plan) {

}

QueryTreePtr redundant_projection(const QueryTreePtr& plan) {

}

// ini nanti dia connect ke optimizer_rules yang ada di query_processor.cpp
QueryTreePtr apply_optimizer_rules(const QueryTreePtr& plan) {
    // if (!plan) {
    //     return nullptr;
    // }

    // auto current_plan = splitting_conjunction(plan);
    // current_plan = pushdown_selection(current_plan);
    // current_plan = redundant_projection(current_plan);
    // return current_plan;
}

} // namespace mdbms::qo

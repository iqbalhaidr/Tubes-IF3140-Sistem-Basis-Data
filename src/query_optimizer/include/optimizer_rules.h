#pragma once
#include <memory>
#include "query_tree.h"

namespace mdbms::qo {

// Aturan 1 – Split conjunctive selections
std::unique_ptr<QueryTree> splitting_conjunction(const QueryTree&);

// Aturan 2 – Pushdown selection
std::unique_ptr<QueryTree> pushdown_selection(const QueryTree&);

// Aturan 3 – Redundant projection
std::unique_ptr<QueryTree> redundant_projection(const QueryTree&);


// Semua Rules
std::unique_ptr<QueryTree> apply_optimizer_rules(const QueryTree&);

}

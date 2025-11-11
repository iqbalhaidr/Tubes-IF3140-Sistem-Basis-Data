#pragma once

#include "query_tree.h"

namespace mdbms::qo {

// Aturan 1 – Split conjunctive selections
QueryTreePtr splitting_conjunction(const QueryTreePtr& plan);

// Aturan 2 – Pushdown selection
QueryTreePtr pushdown_selection(const QueryTreePtr& plan);

// Aturan 3 – Redundant projection
QueryTreePtr redundant_projection(const QueryTreePtr& plan);

// Semua Rules
QueryTreePtr apply_optimizer_rules(const QueryTreePtr& plan);

}

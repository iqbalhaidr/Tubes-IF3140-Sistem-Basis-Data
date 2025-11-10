#pragma once
#include "query_tree.h"

namespace mdbms::qo {

int estimate_cost(const QueryTree& root);

}

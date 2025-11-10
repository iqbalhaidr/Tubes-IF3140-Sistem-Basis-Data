#pragma once
#include <memory>
#include "parsed_query.h"
#include "query_tree.h"

namespace mdbms::qo {

std::unique_ptr<QueryTree> plan_tree(const ParsedQuery& pq);

}

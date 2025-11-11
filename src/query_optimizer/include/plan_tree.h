#pragma once

#include "plan_segments.h"
#include "query_tree.h"

namespace mdbms::qo {

QueryTreePtr plan_tree(const PlanSegments& segments);

}

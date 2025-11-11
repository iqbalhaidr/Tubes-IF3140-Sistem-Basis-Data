#pragma once

#include <string>
#include "parsed_query.h"
#include "plan_segments.h"

namespace mdbms::qo {

// Parse SQL string → ParsedQuery
ParsedQuery parse_sql(const std::string& query);

// Debug helper: expose extracted clauses before tree construction
PlanSegments parse_plan_segments(const std::string& query);

}

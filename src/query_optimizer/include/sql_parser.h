#pragma once
#include <string>
#include "parsed_query.h"

namespace mdbms::qo {

// Parse SQL string → ParsedQuery
ParsedQuery parse_sql(const std::string& query);

}

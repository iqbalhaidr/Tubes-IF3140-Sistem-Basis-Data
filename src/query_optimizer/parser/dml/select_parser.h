#pragma once

#include <string>

namespace mdbms::qo {

struct ParsedQuery;

void parse_select_query(const std::string& query, ParsedQuery& pq);

}  // namespace mdbms::qo


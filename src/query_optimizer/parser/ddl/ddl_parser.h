#pragma once

#include <string>

namespace mdbms::qo {

struct ParsedQuery;

void parse_create_table(const std::string& query, ParsedQuery& pq);
void parse_drop_table(const std::string& query, ParsedQuery& pq);

}  // namespace mdbms::qo


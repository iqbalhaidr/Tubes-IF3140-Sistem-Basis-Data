#pragma once

#include <string>

namespace mdbms::qo {

struct ParsedQuery;

void parse_update_query(const std::string& query, ParsedQuery& pq);
void parse_insert_query(const std::string& query, ParsedQuery& pq);
void parse_delete_query(const std::string& query, ParsedQuery& pq);

}  // namespace mdbms::qo


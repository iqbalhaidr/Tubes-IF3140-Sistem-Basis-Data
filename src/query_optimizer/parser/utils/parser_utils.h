#pragma once

#include "query_optimizer.h"
#include "utils/string_utils.h"

#include <any>
#include <string>
#include <vector>

namespace mdbms::qo {

std::vector<std::string> split_list(const std::string& clause, char delim);
std::vector<std::string> split_on_keyword(const std::string& input,
                                          const std::string& keyword);
std::vector<std::string> split_top_level(const std::string& input);
std::string strip_quotes(std::string value);
std::vector<std::string> tokenize(const std::string& input);
std::string join_tokens(const std::vector<std::string>& tokens);

TableReference parse_table_reference(const std::string& raw);
void record_table_reference(ParsedQuery& pq, TableReference ref);
bool is_join_keyword(const std::string& upper_token);

std::any parse_literal(const std::string& token);
Condition parse_condition(const std::string& raw_condition);
std::vector<Condition> parse_conditions(const std::string& clause);

void parse_order_by(const std::string& clause, ParsedQuery& pq);
void parse_limit(const std::string& clause, ParsedQuery& pq);

}  // namespace mdbms::qo

#pragma once

#include "query_optimizer.h"
#include <set>

namespace mdbms::qo
{
    
std::string trim_outer(const std::string& s);
std::string get_table_from_column(const std::string& column);
std::string get_table_alias(const std::string& table_str);
void get_subtree_tables(QueryTree* node, std::set<std::string>& tables);
int estimate_selectivity(const Condition& cond);
std::vector<std::string> parse_project_columns(const std::string& value);
bool columns_are_identical(const std::vector<std::string>& cols1, const std::vector<std::string>& cols2);
void get_subtree_columns(QueryTree* node, std::set<std::string>& columns);


} // namespace mdbms::qo
#pragma once

#include <string>
#include <vector>

namespace mdbms::qo {

// Buat kebutuhan tree
struct PlanSegments {
    std::vector<std::string> select_list;
    std::vector<std::string> from_tables;
    struct JoinCondition {
        std::string left;
        std::string right;
    };
    std::vector<JoinCondition> joins;
    std::vector<std::string> where_conditions;
};

std::string normalize_sql(const std::string& query);
PlanSegments parse_plan_segments(const std::string& query);

} // namespace mdbms::qo

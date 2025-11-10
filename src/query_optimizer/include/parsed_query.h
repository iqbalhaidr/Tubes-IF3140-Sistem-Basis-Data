#pragma once
#include <memory>
#include <string>
#include <vector>
#include "query_tree.h"

namespace mdbms::qo {

struct ParsedQuery {
    std::string raw_query;

    // SELECT
    std::vector<std::string> select_list;

    // FROM (semua tabel)
    std::vector<std::string> from_tables;

    struct JoinCondition {
        std::string left;
        std::string right;
    };
    std::vector<JoinCondition> joins;

    // WHERE
    std::vector<std::string> where_conditions;

    //  plan tree (sebelum optimize)
    std::unique_ptr<QueryTree> plan_tree;

    // Optimized plan (sesudah optimize)
    std::unique_ptr<QueryTree> optimized_tree;

    ParsedQuery() = default;
    ParsedQuery(const ParsedQuery& other)
        : raw_query(other.raw_query),
          select_list(other.select_list),
          from_tables(other.from_tables),
          joins(other.joins),
          where_conditions(other.where_conditions) {
        if (other.plan_tree) {
            plan_tree = std::make_unique<QueryTree>(*other.plan_tree);
        }
        if (other.optimized_tree) {
            optimized_tree = std::make_unique<QueryTree>(*other.optimized_tree);
        }
    }

    ParsedQuery& operator=(const ParsedQuery& other) {
        if (this == &other) {
            return *this;
        }
        raw_query = other.raw_query;
        select_list = other.select_list;
        from_tables = other.from_tables;
        joins = other.joins;
        where_conditions = other.where_conditions;
        plan_tree = other.plan_tree
                           ? std::make_unique<QueryTree>(*other.plan_tree)
                           : nullptr;
        optimized_tree = other.optimized_tree
                             ? std::make_unique<QueryTree>(*other.optimized_tree)
                             : nullptr;
        return *this;
    }

    ParsedQuery(ParsedQuery&&) noexcept = default;
    ParsedQuery& operator=(ParsedQuery&&) noexcept = default;
};

} // namespace mdbms::qo

#include "dml/select_parser.h"

#include <algorithm>

#include "utils/parser_utils.h"

namespace mdbms::qo {
namespace {

constexpr size_t kNpos = std::string::npos;

void parse_from_clause(const std::string& clause, ParsedQuery& pq) {
    auto tokens = tokenize(clause);
    size_t idx = 0;
    std::vector<std::string> base_tokens;

    while (idx < tokens.size() && !is_join_keyword(to_upper(tokens[idx]))) {
        base_tokens.push_back(tokens[idx]);
        ++idx;
    }

    std::string base_section = join_tokens(base_tokens);
    for (const auto& table : split_list(base_section, ',')) {
        TableReference ref = parse_table_reference(table);
        ref.introduced_by_join = false;
        record_table_reference(pq, ref);
    }

    while (idx < tokens.size()) {
        bool natural_join = false;
        std::string join_type = "INNER";

        while (idx < tokens.size()) {
            std::string upper = to_upper(tokens[idx]);
            if (upper == "NATURAL") {
                natural_join = true;
                join_type = "NATURAL";
                ++idx;
            } else if (upper == "INNER" || upper == "LEFT" ||
                       upper == "RIGHT" || upper == "FULL") {
                if (!natural_join) {
                    join_type = upper;
                }
                ++idx;
            } else if (upper == "OUTER") {
                ++idx;
            } else if (upper == "JOIN") {
                ++idx;
                break;
            } else {
                break;
            }
        }

        if (idx >= tokens.size()) {
            break;
        }

        std::vector<std::string> table_tokens;
        while (idx < tokens.size()) {
            std::string upper = to_upper(tokens[idx]);
            if (!natural_join && upper == "ON") {
                ++idx;
                break;
            }
            if (is_join_keyword(upper)) {
                break;
            }
            table_tokens.push_back(tokens[idx]);
            ++idx;
        }

        TableReference ref = parse_table_reference(join_tokens(table_tokens));
        ref.introduced_by_join = true;
        record_table_reference(pq, ref);

        JoinClause clause_info;
        clause_info.table = ref;
        clause_info.join_type = join_type;
        clause_info.is_natural = natural_join;

        if (!natural_join) {
            std::vector<std::string> condition_tokens;
            while (idx < tokens.size()) {
                std::string upper = to_upper(tokens[idx]);
                if (is_join_keyword(upper)) {
                    break;
                }
                condition_tokens.push_back(tokens[idx]);
                ++idx;
            }

            std::string condition = join_tokens(condition_tokens);
            size_t eq = condition.find('=');
            if (eq != std::string::npos) {
                clause_info.left_expression = trim(condition.substr(0, eq));
                clause_info.right_expression = trim(condition.substr(eq + 1));
                pq.join_pairs.emplace_back(clause_info.left_expression,
                                           clause_info.right_expression);
            } else {
                pq.join_pairs.emplace_back("", "");
            }
        } else {
            pq.join_pairs.emplace_back("", "");
        }

        pq.join_clauses.push_back(clause_info);
    }
}

}  // namespace

void parse_select_query(const std::string& query, ParsedQuery& pq) {
    std::string upper = to_upper(query);
    size_t select_pos = upper.find("SELECT");
    size_t from_pos = upper.find("FROM", select_pos == kNpos ? 0 : select_pos);
    if (select_pos == kNpos || from_pos == kNpos || from_pos <= select_pos) {
        return;
    }

    size_t select_start = select_pos + 6;
    std::string select_clause = query.substr(select_start, from_pos - select_start);
    std::vector<std::string> columns = split_list(select_clause, ',');
    if (columns.empty()) {
        columns.push_back("*");
    }
    pq.select_columns = columns;

    size_t where_pos = upper.find("WHERE", from_pos);
    size_t order_pos = upper.find("ORDER BY", from_pos);
    size_t limit_pos = upper.find("LIMIT", from_pos);

    size_t from_end = query.size();
    for (size_t pos : {where_pos, order_pos, limit_pos}) {
        if (pos != kNpos && pos > from_pos) {
            from_end = std::min(from_end, pos);
        }
    }

    std::string from_clause = query.substr(from_pos + 4, from_end - (from_pos + 4));
    parse_from_clause(from_clause, pq);

    if (where_pos != kNpos) {
        size_t where_start = where_pos + 5;
        size_t where_end = query.size();
        for (size_t pos : {order_pos, limit_pos}) {
            if (pos != kNpos && pos > where_pos) {
                where_end = std::min(where_end, pos);
            }
        }
        std::string where_clause = query.substr(where_start, where_end - where_start);
        auto conditions = parse_conditions(where_clause);
        pq.where_conditions.insert(pq.where_conditions.end(), conditions.begin(), conditions.end());
    }

    if (order_pos != kNpos) {
        size_t order_start = order_pos + std::string("ORDER BY").size();
        size_t order_end = (limit_pos != kNpos && limit_pos > order_pos) ? limit_pos : query.size();
        std::string order_clause = query.substr(order_start, order_end - order_start);
        parse_order_by(order_clause, pq);
    }

    if (limit_pos != kNpos) {
        size_t limit_start = limit_pos + 5;
        std::string limit_clause = query.substr(limit_start);
        parse_limit(limit_clause, pq);
    }
}

}  // namespace mdbms::qo

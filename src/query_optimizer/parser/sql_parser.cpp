#include "query_optimizer.h"
#include "string_utils.h"

#include <array>
#include <sstream>
#include <string_view>

namespace mdbms::qo {
namespace {

struct SelectQueryParts {
    std::vector<std::string> select_list;
    std::vector<std::string> from_tables;
    std::vector<std::pair<std::string, std::string>> joins;
    std::vector<std::string> where_conditions;
};

std::vector<std::string> split_list(const std::string& clause, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(clause);
    std::string item;
    while (std::getline(ss, item, delim)) {
        auto token = trim(item);
        if (!token.empty()) {
            out.push_back(token);
        }
    }
    return out;
}

std::vector<std::string> split_on_keyword(const std::string& input,
                                          const std::string& keyword) {
    std::vector<std::string> parts;
    if (input.empty()) {
        return parts;
    }

    std::string upper = to_upper(input);
    std::string upper_keyword = to_upper(keyword);
    size_t start = 0;
    size_t pos = upper.find(upper_keyword, start);

    while (pos != std::string::npos) {
        std::string chunk = trim(input.substr(start, pos - start));
        if (!chunk.empty()) {
            parts.push_back(chunk);
        }
        start = pos + upper_keyword.size();
        pos = upper.find(upper_keyword, start);
    }

    std::string tail = trim(input.substr(start));
    if (!tail.empty()) {
        parts.push_back(tail);
    }

    return parts;
}

SelectQueryParts extract_select_parts(const std::string& normalized_query) {
    SelectQueryParts parts;
    std::string upper = to_upper(normalized_query);
    constexpr size_t npos = std::string::npos;

    size_t pSel = upper.find("SELECT");
    size_t pFrom = upper.find("FROM", pSel == npos ? 0 : pSel + 6);
    if (pSel == npos || pFrom == npos || pFrom <= pSel) {
        return parts;
    }

    const size_t select_start = pSel + 6;
    std::string select_clause = normalized_query.substr(select_start, pFrom - select_start);
    for (const auto& item : split_list(select_clause, ',')) {
        parts.select_list.push_back(item);
    }

    size_t pWhere = upper.find("WHERE", pFrom);
    const size_t from_body_start = pFrom + 4;
    std::string from_section =
        (pWhere == npos)
            ? normalized_query.substr(from_body_start)
            : normalized_query.substr(from_body_start, pWhere - from_body_start);

    from_section = trim(from_section);
    if (!from_section.empty()) {
        std::string from_upper = to_upper(from_section);
        size_t first_join = from_upper.find("JOIN");
        std::string base_tables_section =
            (first_join == npos) ? from_section : from_section.substr(0, first_join);

        for (const auto& table : split_list(base_tables_section, ',')) {
            parts.from_tables.push_back(table);
        }

        size_t join_pos = first_join;
        while (join_pos != npos) {
            size_t join_keyword_end = join_pos + 4;
            size_t on_pos = from_upper.find("ON", join_keyword_end);
            if (on_pos == npos) {
                break;
            }

            std::string join_table =
                trim(from_section.substr(join_keyword_end, on_pos - join_keyword_end));

            size_t next_join = from_upper.find("JOIN", on_pos + 2);
            size_t condition_end = (next_join == npos) ? from_section.size() : next_join;

            std::string condition =
                trim(from_section.substr(on_pos + 2, condition_end - (on_pos + 2)));
            if (!condition.empty()) {
                size_t eq = condition.find('=');
                if (eq != npos) {
                    std::string left = trim(condition.substr(0, eq));
                    std::string right = trim(condition.substr(eq + 1));
                    if (!left.empty() && !right.empty()) {
                        parts.joins.emplace_back(left, right);
                    }
                }
            }

            if (!join_table.empty()) {
                parts.from_tables.push_back(join_table);
            }

            join_pos = next_join;
        }
    }

    if (pWhere != npos) {
        const size_t where_start = pWhere + 5;
        std::string where_clause = normalized_query.substr(where_start);
        for (const auto& cond : split_on_keyword(where_clause, "AND")) {
            parts.where_conditions.push_back(cond);
        }
    }

    return parts;
}

std::string detect_query_type(const std::string& normalized_query) {
    if (normalized_query.empty()) {
        return {};
    }

    std::string upper = to_upper(normalized_query);
    if (upper.rfind("SELECT", 0) == 0) {
        return "SELECT";
    }
    if (upper.rfind("INSERT", 0) == 0) {
        return "INSERT";
    }
    if (upper.rfind("UPDATE", 0) == 0) {
        return "UPDATE";
    }
    if (upper.rfind("DELETE", 0) == 0) {
        return "DELETE";
    }
    return {};
}

std::string strip_quotes(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

Condition parse_condition(const std::string& raw_condition) {
    static constexpr std::array<const char*, 7> kOperators = {
        "<=", ">=", "!=", "<>", "=", "<", ">"
    };

    Condition condition;
    const std::string trimmed = trim(raw_condition);
    for (const char* op : kOperators) {
        const std::string_view token(op);
        const auto pos = trimmed.find(token);
        if (pos == std::string::npos) {
            continue;
        }

        condition.column = trim(trimmed.substr(0, pos));
        condition.operation = (token == "<>") ? "!=" : std::string(token);
        std::string rhs = trim(trimmed.substr(pos + token.size()));
        condition.operand = strip_quotes(rhs);
        return condition;
    }

    condition.column = trimmed;
    condition.operation.clear();
    condition.operand.reset();
    return condition;
}

} // namespace

ParsedQuery sql_parser(const std::string& query) {
    ParsedQuery pq;
    pq.original_query = trim(query);
    pq.query_type = detect_query_type(pq.original_query);

    if (pq.query_type == "SELECT") {
        const SelectQueryParts parts = extract_select_parts(pq.original_query);
        pq.select_columns = parts.select_list;
        pq.from_tables = parts.from_tables;
        pq.join_pairs = parts.joins;
        for (const auto& raw_condition : parts.where_conditions) {
            pq.where_conditions.push_back(parse_condition(raw_condition));
        }
    }

    return pq;
}

} // namespace mdbms::qo

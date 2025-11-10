#include "sql_parser.h"
#include "parsed_query.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace mdbms::qo {
namespace {

std::string up(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        auto token = trim(item);
        if (!token.empty()) {
            out.push_back(token);
        }
    }
    return out;
}

std::vector<std::string> split_on_keyword(const std::string& input, const std::string& keyword) {
    std::vector<std::string> parts;
    if (input.empty()) {
        return parts;
    }

    std::string upper = up(input);
    std::string upper_keyword = up(keyword);
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

} // namespace

ParsedQuery parse_sql(const std::string& query) {
    ParsedQuery pq;
    pq.raw_query = trim(query);
    
    // if (pq.raw_query.empty()) {
    //     return pq;
    // }

    std::string upper = up(pq.raw_query);
    constexpr size_t npos = std::string::npos;

    size_t pSel = upper.find("SELECT");
    size_t pFrom = upper.find("FROM", pSel == npos ? 0 : pSel + 6);
    if (pSel == npos || pFrom == npos || pFrom <= pSel) {
        return pq;
    }

    // SELECT
    size_t select_start = pSel + 6;
    std::string select_clause = pq.raw_query.substr(select_start, pFrom - select_start);
    for (const auto& item : split(select_clause, ',')) {
        pq.select_list.push_back(item);
    }

    // FROM / JOIN
    size_t pWhere = upper.find("WHERE", pFrom);
    size_t from_body_start = pFrom + 4;
    std::string from_section =
        (pWhere == npos)
            ? pq.raw_query.substr(from_body_start)
            : pq.raw_query.substr(from_body_start, pWhere - from_body_start);

    from_section = trim(from_section);
    if (!from_section.empty()) {
        std::string from_upper = up(from_section);
        size_t first_join = from_upper.find("JOIN");
        std::string base_tables_section =
            (first_join == npos) ? from_section : from_section.substr(0, first_join);

        for (const auto& table : split(base_tables_section, ',')) {
            pq.from_tables.push_back(table);
        }

        size_t join_pos = first_join;
        while (join_pos != npos) {
            size_t join_keyword_end = join_pos + 4;
            size_t on_pos = from_upper.find("ON", join_keyword_end);
            if (on_pos == npos) {
                break;
            }

            std::string join_table = trim(
                from_section.substr(join_keyword_end, on_pos - join_keyword_end));
            if (!join_table.empty()) {
                pq.from_tables.push_back(join_table);
            }

            size_t next_join = from_upper.find("JOIN", on_pos + 2);
            size_t condition_end = (next_join == npos) ? from_section.size() : next_join;

            std::string condition = trim(
                from_section.substr(on_pos + 2, condition_end - (on_pos + 2)));
            if (!condition.empty()) {
                size_t eq = condition.find('=');
                if (eq != npos) {
                    std::string left = trim(condition.substr(0, eq));
                    std::string right = trim(condition.substr(eq + 1));
                    if (!left.empty() && !right.empty()) {
                        pq.joins.push_back({left, right});
                    }
                }
            }

            join_pos = next_join;
        }
    }

    // WHERE 
    if (pWhere != npos) {
        size_t where_start = pWhere + 5;
        std::string where_clause = pq.raw_query.substr(where_start);
        for (const auto& cond : split_on_keyword(where_clause, "AND")) {
            pq.where_conditions.push_back(cond);
        }
    }

    return pq;
}

} // namespace mdbms::qo

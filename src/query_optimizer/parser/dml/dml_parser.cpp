#include "dml/dml_parser.h"

#include "utils/parser_utils.h"

namespace mdbms::qo {

void parse_update_query(const std::string& query, ParsedQuery& pq) {
    std::string upper = to_upper(query);
    size_t update_pos = upper.find("UPDATE");
    size_t set_pos = upper.find("SET", update_pos == std::string::npos ? 0 : update_pos);
    if (update_pos == std::string::npos || set_pos == std::string::npos) {
        return;
    }

    size_t table_start = update_pos + 6;
    std::string table_section = query.substr(table_start, set_pos - table_start);
    TableReference ref = parse_table_reference(table_section);
    record_table_reference(pq, ref);
    pq.target_table = ref.table_name;

    size_t where_pos = upper.find("WHERE", set_pos);
    std::string set_clause =
        (where_pos == std::string::npos)
            ? query.substr(set_pos + 3)
            : query.substr(set_pos + 3, where_pos - (set_pos + 3));

    for (const auto& assignment : split_list(set_clause, ',')) {
        size_t eq = assignment.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string column = trim(assignment.substr(0, eq));
        std::string value = trim(assignment.substr(eq + 1));
        pq.set_values[column] = parse_literal(value);
    }

    if (where_pos != std::string::npos) {
        std::string where_clause = query.substr(where_pos + 5);
        auto conditions = parse_conditions(where_clause);
        if (!conditions.empty()) {
            pq.where_conditions.push_back(conditions.front());
        }
    }
}

void parse_insert_query(const std::string& query, ParsedQuery& pq) {
    std::string upper = to_upper(query);
    size_t into_pos = upper.find("INTO");
    size_t values_pos = upper.find("VALUES", into_pos == std::string::npos ? 0 : into_pos);
    if (into_pos == std::string::npos || values_pos == std::string::npos) {
        return;
    }

    size_t table_start = into_pos + 4;
    std::string table_and_columns = query.substr(table_start, values_pos - table_start);
    table_and_columns = trim(table_and_columns);

    size_t paren = table_and_columns.find('(');
    std::string table_part = (paren == std::string::npos)
                                 ? table_and_columns
                                 : table_and_columns.substr(0, paren);
    TableReference ref = parse_table_reference(table_part);
    record_table_reference(pq, ref);
    pq.target_table = ref.table_name;

    if (paren != std::string::npos) {
        size_t close_paren = table_and_columns.find(')', paren);
        if (close_paren != std::string::npos) {
            std::string column_section =
                table_and_columns.substr(paren + 1, close_paren - paren - 1);
            pq.insert_columns = split_list(column_section, ',');
        }
    }

    size_t values_start = query.find('(', values_pos);
    size_t values_end = query.find(')', values_start == std::string::npos ? 0 : values_start);
    if (values_start != std::string::npos && values_end != std::string::npos && values_end > values_start) {
        std::string values_section =
            query.substr(values_start + 1, values_end - values_start - 1);
        for (const auto& value : split_list(values_section, ',')) {
            pq.insert_values.push_back(parse_literal(value));
        }
    }
}

void parse_delete_query(const std::string& query, ParsedQuery& pq) {
    std::string upper = to_upper(query);
    size_t from_pos = upper.find("FROM");
    if (from_pos == std::string::npos) {
        return;
    }

    size_t where_pos = upper.find("WHERE", from_pos);
    std::string table_section =
        (where_pos == std::string::npos)
            ? query.substr(from_pos + 4)
            : query.substr(from_pos + 4, where_pos - (from_pos + 4));
    TableReference ref = parse_table_reference(table_section);
    record_table_reference(pq, ref);
    pq.target_table = ref.table_name;

    if (where_pos != std::string::npos) {
        std::string where_clause = query.substr(where_pos + 5);
        auto conditions = parse_conditions(where_clause);
        if (!conditions.empty()) {
            pq.where_conditions.push_back(conditions.front());
        }
    }
}

}  // namespace mdbms::qo

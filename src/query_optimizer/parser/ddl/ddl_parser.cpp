#include "ddl/ddl_parser.h"

#include <sstream>

#include "utils/parser_utils.h"

namespace mdbms::qo {
namespace {

constexpr size_t kNpos = std::string::npos;

int extract_length(const std::string& token, std::string& base_type) {
    size_t open = token.find('(');
    if (open == std::string::npos) {
        base_type = token;
        return 0;
    }
    size_t close = token.find(')', open);
    base_type = token.substr(0, open);
    if (close == std::string::npos) {
        return 0;
    }
    std::string number = token.substr(open + 1, close - open - 1);
    try {
        return std::stoi(number);
    } catch (...) {
        return 0;
    }
}

void apply_primary_keys(std::vector<ColumnDefinition>& columns,
                        const std::vector<std::string>& pk_columns) {
    for (const auto& pk : pk_columns) {
        for (auto& column : columns) {
            if (to_upper(column.name) == to_upper(trim(pk))) {
                column.is_primary_key = true;
            }
        }
    }
}

void apply_foreign_key(std::vector<ColumnDefinition>& columns,
                       const std::vector<std::string>& fk_columns,
                       const std::string& ref_table,
                       const std::vector<std::string>& ref_columns) {
    if (fk_columns.empty()) {
        return;
    }

    std::string source = trim(fk_columns.front());
    std::string target_column = ref_columns.empty() ? "" : trim(ref_columns.front());
    for (auto& column : columns) {
        if (to_upper(column.name) == to_upper(source)) {
            column.is_foreign_key = true;
            column.references_table = trim(ref_table);
            column.references_column = target_column;
        }
    }
}

void parse_column_definition(const std::string& definition, ParsedQuery& pq) {
    std::string trimmed = trim(definition);
    if (trimmed.empty()) {
        return;
    }

    std::string upper = to_upper(trimmed);
    if (upper.rfind("PRIMARY KEY", 0) == 0) {
        size_t open = trimmed.find('(');
        size_t close = trimmed.find(')', open == kNpos ? 0 : open);
        if (open != kNpos && close != kNpos && close > open) {
            std::string inside = trimmed.substr(open + 1, close - open - 1);
            apply_primary_keys(pq.column_definitions, split_list(inside, ','));
        }
        return;
    }

    if (upper.rfind("FOREIGN KEY", 0) == 0) {
        size_t open = trimmed.find('(');
        size_t close = trimmed.find(')', open == kNpos ? 0 : open);
        size_t ref_pos = upper.find("REFERENCES");
        if (open != kNpos && close != kNpos &&
            ref_pos != kNpos && close > open) {
            std::string cols = trimmed.substr(open + 1, close - open - 1);
            std::string ref_section = trimmed.substr(ref_pos + 10);
            ref_section = trim(ref_section);
            size_t ref_open = ref_section.find('(');
            size_t ref_close = ref_section.find(')', ref_open == kNpos ? 0 : ref_open);
            std::string ref_table = (ref_open == kNpos)
                                        ? ref_section
                                        : ref_section.substr(0, ref_open);
            std::string ref_cols =
                (ref_open != kNpos && ref_close != kNpos && ref_close > ref_open)
                    ? ref_section.substr(ref_open + 1, ref_close - ref_open - 1)
                    : "";
            apply_foreign_key(pq.column_definitions,
                              split_list(cols, ','),
                              ref_table,
                              split_list(ref_cols, ','));
        }
        return;
    }

    std::istringstream iss(trimmed);
    std::string name;
    std::string type_token;
    iss >> name;
    iss >> type_token;
    if (name.empty() || type_token.empty()) {
        return;
    }

    ColumnDefinition column;
    column.name = name;
    column.length = extract_length(type_token, column.data_type);

    std::string remainder;
    std::getline(iss, remainder);
    std::string rem_upper = to_upper(remainder);
    if (rem_upper.find("PRIMARY KEY") != std::string::npos) {
        column.is_primary_key = true;
    }
    size_t ref_pos = rem_upper.find("REFERENCES");
    if (ref_pos != std::string::npos) {
        std::string ref_section = remainder.substr(ref_pos + 10);
        ref_section = trim(ref_section);
        size_t ref_open = ref_section.find('(');
        size_t ref_close = ref_section.find(')', ref_open == kNpos ? 0 : ref_open);
        column.references_table =
            (ref_open == kNpos) ? ref_section : ref_section.substr(0, ref_open);
        if (ref_open != kNpos && ref_close != kNpos && ref_close > ref_open) {
            column.references_column = ref_section.substr(ref_open + 1, ref_close - ref_open - 1);
            column.is_foreign_key = true;
        }
    }

    pq.column_definitions.push_back(column);
}

}  // namespace

void parse_create_table(const std::string& query, ParsedQuery& pq) {
    std::string upper = to_upper(query);
    size_t table_pos = upper.find("TABLE");
    size_t open = query.find('(', table_pos == kNpos ? 0 : table_pos);
    size_t close = query.rfind(')');
    if (table_pos == kNpos || open == kNpos || close == kNpos || close <= open) {
        return;
    }

    size_t name_start = table_pos + 5;
    std::string table_name = query.substr(name_start, open - name_start);
    table_name = trim(table_name);
    pq.target_table = table_name;

    std::string body = query.substr(open + 1, close - open - 1);
    for (const auto& definition : split_top_level(body)) {
        parse_column_definition(definition, pq);
    }
}

void parse_drop_table(const std::string& query, ParsedQuery& pq) {
    std::string upper = to_upper(query);
    size_t table_pos = upper.find("TABLE");
    if (table_pos == kNpos) {
        return;
    }
    std::string table_name = query.substr(table_pos + 5);
    pq.target_table = trim(table_name);
}

}  // namespace mdbms::qo

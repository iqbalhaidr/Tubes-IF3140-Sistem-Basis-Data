#include "optimizer_helper.h"

namespace mdbms::qo {

std::string trim_outer(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string get_table_from_column(const std::string& column) {
    size_t dot_pos = column.find('.');
    if (dot_pos != std::string::npos) {
        return column.substr(0, dot_pos);
    }
    return "";
}

std::string get_table_alias(const std::string& table_str) {
    size_t space_pos = table_str.rfind(' ');
    if (space_pos != std::string::npos) {
        return trim_outer(table_str.substr(space_pos + 1));
    }
    return table_str;
}

void get_subtree_tables(QueryTree* node, std::set<std::string>& tables) {
    if (!node) return;
    
    if (node->type == "SCAN") {
        tables.insert(get_table_alias(node->value));
    }
    
    for (auto* child : node->children) {
        get_subtree_tables(child, tables);
    }
}

int estimate_selectivity(const Condition& cond) {
    if (cond.operation == "=") return 100;
    if (cond.operation == "!=") return 10;
    if (cond.operation == "<" || cond.operation == ">") return 50;
    if (cond.operation == "<=" || cond.operation == ">=") return 40;
    if (cond.operation == "LIKE") return 20;
    return 30;
}

std::vector<std::string> parse_project_columns(const std::string& value) {
    std::vector<std::string> columns;
    size_t colon_pos = value.find(':');
    if (colon_pos == std::string::npos) {
        return columns;
    }

    std::string cols_str = trim_outer(value.substr(colon_pos + 1));
    
    size_t start = 0;
    size_t comma_pos;
    while ((comma_pos = cols_str.find(',', start)) != std::string::npos) {
        columns.push_back(trim_outer(cols_str.substr(start, comma_pos - start)));
        start = comma_pos + 1;
    }
    if (start < cols_str.length()) {
        columns.push_back(trim_outer(cols_str.substr(start)));
    }
    
    return columns;
}

bool columns_are_identical(const std::vector<std::string>& cols1,
                           const std::vector<std::string>& cols2) {
    if (cols1.size() != cols2.size()) return false;
    
    for (size_t i = 0; i < cols1.size(); ++i) {
        if (cols1[i] != cols2[i]) return false;
    }
    
    return true;
}

void get_subtree_columns(QueryTree* node, std::set<std::string>& columns) {
    if (!node) return;
    
    if (node->type == "SELECT") {
        std::string value = node->value;
        size_t dot_pos = value.find('.');
        if (dot_pos != std::string::npos) {
            size_t space_pos = value.find(' ', dot_pos);
            if (space_pos != std::string::npos) {
                columns.insert(trim_outer(value.substr(0, space_pos)));
            }
        }
    } else if (node->type == "JOIN") {
        std::string value = node->value;
        size_t on_pos = value.find("ON:");
        if (on_pos != std::string::npos) {
            std::string condition = trim_outer(value.substr(on_pos + 3));
            size_t eq_pos = condition.find('=');
            if (eq_pos != std::string::npos) {
                columns.insert(trim_outer(condition.substr(0, eq_pos)));
                columns.insert(trim_outer(condition.substr(eq_pos + 1)));
            }
        }
    } else if (node->type == "PROJECT") {
        std::vector<std::string> proj_cols = parse_project_columns(node->value);
        for (const auto& col : proj_cols) {
            columns.insert(col);
        }
    }
    
    for (auto* child : node->children) {
        get_subtree_columns(child, columns);
    }
}

} // namespace mdbms::qo
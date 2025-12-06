#include "query_optimizer.h"

namespace mdbms::qo {

namespace {

std::vector<TableReference> collect_tables(const ParsedQuery& parsed) {
    if (!parsed.table_references.empty()) {
        return parsed.table_references;
    }

    std::vector<TableReference> refs;
    refs.reserve(parsed.from_tables.size());
    for (const auto& table : parsed.from_tables) {
        refs.emplace_back(table, "");
    }
    return refs;
}

std::string format_scan_value(const TableReference& ref) {
    if (!ref.alias.empty()) {
        return ref.table_name + " AS " + ref.alias;
    }
    return ref.table_name;
}

std::string build_join_label(size_t join_index,
                             const TableReference& ref,
                             const ParsedQuery& parsed) {
    static const std::string kCartesian = "CARTESIAN";
    if (!ref.introduced_by_join || join_index >= parsed.join_clauses.size()) {
        return kCartesian;
    }

    const JoinClause& clause = parsed.join_clauses[join_index];
    if (clause.is_natural) {
        return "NATURAL";
    }

    if (!clause.left_expression.empty() && !clause.right_expression.empty()) {
        return clause.join_type + ": " + clause.left_expression + " = " + clause.right_expression;
    }
    return clause.join_type;
}

std::string build_condition_string(const Condition& condition) {
    std::string cond_str = condition.column;
    if (condition.operation.empty()) {
        return cond_str;
    }

    cond_str += " " + condition.operation;
    if (!condition.operand.has_value()) {
        return cond_str;
    }

    try {
        if (condition.operand.type() == typeid(std::string)) {
            cond_str += " " + std::any_cast<std::string>(condition.operand);
        } else if (condition.operand.type() == typeid(int)) {
            cond_str += " " + std::to_string(std::any_cast<int>(condition.operand));
        } else if (condition.operand.type() == typeid(double)) {
            cond_str += " " + std::to_string(std::any_cast<double>(condition.operand));
        } else if (condition.operand.type() == typeid(float)) {
            cond_str += " " + std::to_string(std::any_cast<float>(condition.operand));
        }
    } catch (...) {
    }
    return cond_str;
}

} // namespace

QueryTree* plan_tree(const ParsedQuery& parsed) {
    if (parsed.query_type != "SELECT") {
        QueryTree* root = new QueryTree();
        root->type = parsed.query_type;
        return root;
    }

    std::vector<TableReference> tables = collect_tables(parsed);
    if (tables.empty()) {
        return nullptr;
    }

    QueryTree* current = new QueryTree();
    current->type = "SCAN";
    current->value = format_scan_value(tables.front());

    size_t join_clause_index = 0;
    for (size_t i = 1; i < tables.size(); ++i) {
        QueryTree* right_scan = new QueryTree();
        right_scan->type = "SCAN";
        right_scan->value = format_scan_value(tables[i]);

        QueryTree* join_node = new QueryTree();
        join_node->type = "JOIN";
        if (tables[i].introduced_by_join) {
            join_node->value = build_join_label(join_clause_index, tables[i], parsed);
            join_clause_index++;
        } else {
            join_node->value = "CARTESIAN";
        }

        join_node->add_child(current);
        join_node->add_child(right_scan);
        current = join_node;
    }

    if (!parsed.where_conditions.empty()) {
        for (const auto& condition : parsed.where_conditions) {
            QueryTree* select_node = new QueryTree();
            select_node->type = "SELECT";
            select_node->value = build_condition_string(condition);
            select_node->add_child(current);
            current = select_node;
        }
    }

    QueryTree* project_node = new QueryTree();
    project_node->type = "PROJECT";

    std::string proj_list;
    for (size_t i = 0; i < parsed.select_columns.size(); ++i) {
        if (i > 0) {
            proj_list += ", ";
        }
        proj_list += parsed.select_columns[i];
    }
    project_node->value = proj_list;

    if (current) {
        project_node->add_child(current);
    }

    return project_node;
}

} // namespace mdbms::qo

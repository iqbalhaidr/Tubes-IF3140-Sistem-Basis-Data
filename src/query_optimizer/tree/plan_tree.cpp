#include "query_optimizer.h"

namespace mdbms::qo {

QueryTree* plan_tree(const ParsedQuery& parsed) {
    
    if (parsed.query_type != "SELECT") {
        QueryTree* root = new QueryTree();
        root->type = parsed.query_type;
        return root;
    }
    
    // Build tree bottom-up
    QueryTree* current = nullptr;
    
    // Bikin scan nodes
    if (parsed.from_tables.empty()) {
        return nullptr;
    }
    
    if (parsed.from_tables.size() == 1 && parsed.join_pairs.empty()) {
        // Single table scan
        current = new QueryTree();
        current->type = "SCAN";
        current->value = parsed.from_tables[0];
    } else {
        // Bikin join nodes
        current = new QueryTree();
        current->type = "SCAN";
        current->value = parsed.from_tables[0];
        
        // Build joins
        for (size_t i = 1; i < parsed.from_tables.size(); ++i) {
            QueryTree* right_scan = new QueryTree();
            right_scan->type = "SCAN";
            right_scan->value = parsed.from_tables[i];
            
            QueryTree* join_node = new QueryTree();
            join_node->type = "JOIN";
            
            // Find matching join condition
            if (i - 1 < parsed.join_pairs.size()) {
                join_node->value = parsed.join_pairs[i - 1].first + " = " + parsed.join_pairs[i - 1].second;
            }
            
            join_node->add_child(current);
            join_node->add_child(right_scan);
            current = join_node;
        }
    }
    
    // Add WHERE node sebagai selection
    if (!parsed.where_conditions.empty()) {
        for (const auto& condition : parsed.where_conditions) {
            QueryTree* select_node = new QueryTree();
            select_node->type = "SELECT";
            
            // Build condition string
            std::string cond_str = condition.column;
            if (!condition.operation.empty()) {
                cond_str += " " + condition.operation;
                if (condition.operand.has_value()) {
                    try {
                        if (condition.operand.type() == typeid(std::string)) {
                            cond_str += " " + std::any_cast<std::string>(condition.operand);
                        } else if (condition.operand.type() == typeid(int)) {
                            cond_str += " " + std::to_string(std::any_cast<int>(condition.operand));
                        } else if (condition.operand.type() == typeid(double)) {
                            cond_str += " " + std::to_string(std::any_cast<double>(condition.operand));
                        }
                    } catch (...) {
                        // If casting fails, just use the column + operation
                    }
                }
            }
            
            select_node->value = cond_str;
            select_node->add_child(current);
            current = select_node;
        }
    }
    
    // tambah project node
    QueryTree* project_node = new QueryTree();
    project_node->type = "PROJECT";
    
    // Build projection list
    std::string proj_list;
    for (size_t i = 0; i < parsed.select_columns.size(); ++i) {
        if (i > 0) proj_list += ", ";
        proj_list += parsed.select_columns[i];
    }
    project_node->value = proj_list;
    
    if (current) {
        project_node->add_child(current);
    }
    
    return project_node;
}

} // namespace mdbms::qo

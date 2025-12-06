#include "query_optimizer.h"
#include "optimizer_helper.h"

#include <algorithm>

namespace mdbms::qo {
namespace {

QueryTree* passthrough(QueryTree* plan) {
    return plan;
}

QueryTree* splitting_conjunction(QueryTree* plan, 
                                 const std::vector<Condition>& where_conditions) {
    if (!plan) return nullptr;

    for (auto& child : plan->children) {
        child = splitting_conjunction(child, where_conditions);
    }

    if (plan->type == "SELECT") {
        std::string v = plan->value;
        size_t pos = v.find(" AND ");

        if (pos != std::string::npos) {
            std::string left = trim_outer(v.substr(0, pos));
            std::string right = trim_outer(v.substr(pos + 5));

            QueryTree* rightNode = new QueryTree();
            rightNode->type = "SELECT";
            rightNode->value = right;
            rightNode->children = plan->children; 

            plan->value = left;
            plan->children.clear();
            plan->add_child(rightNode);
            
            return splitting_conjunction(plan, where_conditions);
        }
    }

    return plan;
}

QueryTree* reorder_selections(QueryTree* plan, 
                              const std::vector<Condition>& where_conditions) {
    if (!plan) return nullptr;

    for (auto& child : plan->children) {
        child = reorder_selections(child, where_conditions);
    }

    std::vector<std::pair<std::string, const Condition*>> select_values;
    QueryTree* current = plan;
    QueryTree* deepest_non_select = nullptr;
    
    while (current && current->type == "SELECT") {
        const Condition* matching_cond = nullptr;
        for (const auto& cond : where_conditions) {
            std::string cond_str = cond.column + " " + cond.operation;
            if (current->value.find(cond_str) != std::string::npos) {
                matching_cond = &cond;
                break;
            }
        }
        
        if (matching_cond) {
            select_values.push_back({current->value, matching_cond});
        } else {
            Condition dummy;
            dummy.operation = "default";
            select_values.push_back({current->value, nullptr});
        }
        
        if (current->children.size() == 1) {
            current = current->children[0];
        } else {
            break;
        }
    }
    
    deepest_non_select = current;

    if (select_values.size() > 1) {
        std::stable_sort(select_values.begin(), select_values.end(),
            [](const auto& a, const auto& b) {
                if (a.second && b.second) {
                    return estimate_selectivity(*a.second) > estimate_selectivity(*b.second);
                }
                if (a.second) return true;
                if (b.second) return false;
                return false;
            });

        QueryTree* new_chain = deepest_non_select;
        for (int i = select_values.size() - 1; i >= 0; --i) {
            QueryTree* new_node = new QueryTree();
            new_node->type = "SELECT";
            new_node->value = select_values[i].first;
            new_node->add_child(new_chain);
            new_chain = new_node;
        }
        
        return new_chain;
    }

    return plan;
}

QueryTree* pushdown_selection(QueryTree* plan,
                              const std::vector<Condition>& where_conditions,
                              const std::vector<std::string>& from_tables) {
    if (!plan) return nullptr;

    for (auto& child : plan->children) {
        child = pushdown_selection(child, where_conditions, from_tables);
    }

    if (plan->type == "SELECT" && plan->children.size() == 1) {
        QueryTree* child = plan->children[0];

        if (child->type == "JOIN" && child->children.size() == 2) {
            QueryTree* left_child = child->children[0];
            QueryTree* right_child = child->children[1];

            std::set<std::string> left_tables, right_tables;
            get_subtree_tables(left_child, left_tables);
            get_subtree_tables(right_child, right_tables);

            const Condition* matching_cond = nullptr;
            for (const auto& cond : where_conditions) {
                std::string cond_str = cond.column + " " + cond.operation;
                if (plan->value.find(cond_str) != std::string::npos) {
                    matching_cond = &cond;
                    break;
                }
            }

            if (matching_cond) {
                std::string cond_table = get_table_from_column(matching_cond->column);
                
                bool only_left = left_tables.count(cond_table) > 0 && 
                                right_tables.count(cond_table) == 0;
                
                bool only_right = right_tables.count(cond_table) > 0 && 
                                 left_tables.count(cond_table) == 0;

                if (only_left) {
                    QueryTree* new_select = new QueryTree();
                    new_select->type = "SELECT";
                    new_select->value = plan->value;
                    new_select->add_child(left_child);
                    
                    child->children[0] = new_select;
                    child->children[0] = pushdown_selection(child->children[0], 
                                                            where_conditions, 
                                                            from_tables);
                    return child;
                }

                if (only_right) {
                    QueryTree* new_select = new QueryTree();
                    new_select->type = "SELECT";
                    new_select->value = plan->value;
                    new_select->add_child(right_child);
                    
                    child->children[1] = new_select;
                    child->children[1] = pushdown_selection(child->children[1], 
                                                            where_conditions, 
                                                            from_tables);
                    return child;
                }
            }
        }
    }

    return plan;
}

QueryTree* redundant_projection(QueryTree* plan,
                               const std::vector<std::string>& select_columns) {
    if (!plan) return nullptr;

    for (auto& child : plan->children) {
        child = redundant_projection(child, select_columns);
    }

    if (plan->type != "PROJECT" || plan->children.size() != 1) {
        return plan;
    }

    QueryTree* child = plan->children[0];
    std::vector<std::string> plan_cols = parse_project_columns(plan->value);

    if (child->type == "PROJECT") {
        std::vector<std::string> child_cols = parse_project_columns(child->value);
        
        if (columns_are_identical(plan_cols, child_cols)) {
            plan->children.clear(); 
            delete plan;
            return child;
        }
        
        std::set<std::string> parent_set(plan_cols.begin(), plan_cols.end());
        std::set<std::string> child_set(child_cols.begin(), child_cols.end());
        
        bool parent_is_subset = true;
        for (const auto& col : plan_cols) {
            if (child_set.find(col) == child_set.end()) {
                parent_is_subset = false;
                break;
            }
        }
        
        if (parent_is_subset && parent_set.size() <= child_set.size()) {
            if (child->children.size() == 1) {
                QueryTree* grandchild = child->children[0];
                plan->children[0] = grandchild;
                child->children.clear();
                delete child;
                return plan;
            }
        }
    }

    std::set<std::string> used_below;
    get_subtree_columns(child, used_below);
    
    bool projects_all = true;
    for (const auto& used_col : used_below) {
        bool found = false;
        for (const auto& proj_col : plan_cols) {
            if (used_col == proj_col) {
                found = true;
                break;
            }
        }
        if (!found) {
            projects_all = false;
            break;
        }
    }
    
    bool all_used_below = true;
    for (const auto& proj_col : plan_cols) {
        if (used_below.find(proj_col) == used_below.end()) {
            all_used_below = false;
            break;
        }
    }
    
    if (projects_all && all_used_below && plan_cols.size() == used_below.size()) {
        plan->children.clear();
        delete plan;
        return child;
    }

    return plan;
}

QueryTree* apply_optimizer_rules(QueryTree* plan,
                                 const std::vector<Condition>& where_conditions,
                                 const std::vector<std::string>& from_tables,
                                 const std::vector<std::string>& select_columns) {
    if (!plan) {
        return nullptr;
    }
    
    plan = splitting_conjunction(plan, where_conditions);
    plan = reorder_selections(plan, where_conditions);
    plan = pushdown_selection(plan, where_conditions, from_tables);
    plan = redundant_projection(plan, select_columns);
    
    return passthrough(plan);
}

} // namespace

QueryTree* optimize_tree(QueryTree* plan,
                        const std::vector<Condition>& where_conditions,
                        const std::vector<std::string>& from_tables,
                        const std::vector<std::string>& select_columns) {

    return apply_optimizer_rules(plan, where_conditions, from_tables, select_columns);
}

} // namespace mdbms::qo
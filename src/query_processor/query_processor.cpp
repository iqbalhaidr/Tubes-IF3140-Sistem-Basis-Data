#include "query_processor.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <utility>
#include <unordered_map>
#include "concurrency_control.h"
#include "failure_recovery.h"
#include "query_optimizer.h"
#include "storage_manager.h"

namespace mdbms::qp {

QueryProcessor::QueryProcessor()
    : current_transaction_id(-1), explicit_transaction_started(false) {
    std::cout << "QP: Query Processor initialized" << std::endl;
}

ExecutionResult QueryProcessor::execute_query(const std::string& query) {
    std::cout << "QP: Menerima query: " << query << std::endl;
    
    ExecutionResult result;
    result.query = query;
    result.timestamp = std::time(nullptr);
    result.success = false;
    result.affected_rows = 0;

    try {
        std::string query_type = parse_query_type(query);
        std::cout << "Query type: " << query_type << std::endl;

        // Handle Transaction 
        if (query_type == "BEGIN") {
            int tid = begin_transaction();
            current_transaction_id = tid;  // Update current_transaction_id to track the active transaction
            explicit_transaction_started = true;  // Mark as explicitly started
            result.transaction_id = tid;
            result.message = "Transaction " + std::to_string(tid) + " started";
            result.success = true;
            return result;
        } else if (query_type == "COMMIT") {
            bool success = commit_transaction(current_transaction_id);
            result.transaction_id = current_transaction_id;
            result.message = success ? "Transaction committed" : "Commit failed";
            result.success = success;
            current_transaction_id = -1;
            explicit_transaction_started = false;  // Reset flag after commit
            return result;
        } else if (query_type == "ROLLBACK" || query_type == "ABORT") {
            bool success = abort_transaction(current_transaction_id);
            result.transaction_id = current_transaction_id;
            result.message = success ? "Transaction aborted" : "Abort failed";
            result.success = success;
            current_transaction_id = -1;
            explicit_transaction_started = false;  // Reset flag after abort
            return result;
        }

        // Ensure have transaction 
        if (current_transaction_id == -1) {
            current_transaction_id = begin_transaction();
            explicit_transaction_started = false;  // Auto-started transaction, not explicit
        }
        result.transaction_id = current_transaction_id;

        // Parse and optimize query using optimizer (with storage stats)
        mdbms::qo::ParsedQuery optimized_query = mdbms::qo::OptimizationEngine::get_instance().analyze_query(
            query, &mdbms::sm::StorageEngine::get_instance());

        // Execute based on query type
        if (query_type == "SELECT") {
            Rows<Row> rows = execute_select(optimized_query, current_transaction_id);
            result.data = rows;
            result.affected_rows = rows.rows_count;
            result.message = "Retrieved " + std::to_string(rows.rows_count) + " rows";
            result.success = true;
        } else if (query_type == "UPDATE") {
            int affected = execute_update(optimized_query, current_transaction_id);
            result.affected_rows = affected;
            result.message = "Updated " + std::to_string(affected) + " rows";
            result.success = true;
        } else if (query_type == "INSERT") {
            int affected = execute_insert(optimized_query, current_transaction_id);
            result.affected_rows = affected;
            result.message = "Inserted " + std::to_string(affected) + " rows";
            result.success = true;
        } else if (query_type == "DELETE") {
            int affected = execute_delete(optimized_query, current_transaction_id);
            result.affected_rows = affected;
            result.message = "Deleted " + std::to_string(affected) + " rows";
            result.success = true;
        } else if (query_type == "CREATE") {
            bool success = execute_create_table(optimized_query, current_transaction_id);
            result.success = success;
            result.message = success ? "Table created successfully" : "Failed to create table";
            result.affected_rows = 0;
        } else if (query_type == "DROP") {
            bool success = execute_drop_table(optimized_query, current_transaction_id);
            result.success = success;
            result.message = success ? "Table dropped successfully" : "Failed to drop table";
            result.affected_rows = 0;
        } else {
            throw std::runtime_error("Unsupported query type: " + query_type);
        }

        // Auto-commit queries only if transaction was auto-started (not explicitly started with BEGIN)
        if (!explicit_transaction_started) {
            mdbms::fr::FailureRecoveryManager::get_instance().commit_transaction(result.transaction_id);
            mdbms::ccm::ConcurrencyControlManager::get_instance().end_transaction(result.transaction_id);
            current_transaction_id = -1;
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.message = "Error: " + std::string(e.what());
        std::cerr << "QP Error: " << e.what() << std::endl;

        // Abort transaction on error
        if (current_transaction_id != -1) {
            abort_transaction(current_transaction_id);
            current_transaction_id = -1;
            explicit_transaction_started = false;  // Reset flag after abort
        }
    }
    return result;
}

Rows<Row> QueryProcessor::execute_select(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id) {
    Rows<Row> result;

    try {
        std::vector<Rows<Row>> table_data;
        std::vector<std::string> table_names_processed;

        for (const auto& table_name : parsed_query.from_tables) {
            try {
                mdbms::sm::StorageEngine::get_instance().get_table_schema(table_name);
            } catch (const std::runtime_error& e) {
                throw std::runtime_error("Table '" + table_name + "' does not exist");
            }

            DataRetrieval retrieval;
            retrieval.table = table_name;
            
            std::vector<std::string> resolved_columns;
            for (const auto& col : parsed_query.select_columns) {
                resolved_columns.push_back(resolve_aliased_column(col, parsed_query.table_aliases));
            }
            retrieval.columns = resolved_columns;
            retrieval.search_type = SearchType::LINEAR;

            {
                Row request;
                request.table_name = table_name;
                request.row_id = -1;
                mdbms::ccm::ConcurrencyControlManager::get_instance().log_object(request, transaction_id);
                Response access = mdbms::ccm::ConcurrencyControlManager::get_instance().validate_object(request, transaction_id, Action::READ);
                if (!access.allowed) {
                    throw std::runtime_error("Concurrency control denied READ access for table " + table_name);
                }
            }

            // Check if table has index
            // if (!parsed_query.where_conditions.empty()) {
            //     const auto& first_condition = parsed_query.where_conditions[0];
            //     if (mdbms::sm::StorageEngine::get_instance().has_index(table_name, first_condition.column)) {
            //         retrieval.search_type = SearchType::INDEX_SCAN;
            //         retrieval.index_column = first_condition.column;
            //     }
            // }

            // TODO:
            // Request access permission from Concurrency Control Manager
            // For SELECT, we need READ permission on all rows

            // Read data from storage
            Rows<Row> table_rows = mdbms::sm::StorageEngine::get_instance().read_block(retrieval);
            if (!parsed_query.table_aliases.empty()) {
                table_rows = apply_table_aliases(table_rows, table_name, parsed_query.table_aliases);
            }
            
            table_data.push_back(table_rows);
            table_names_processed.push_back(table_name);

            std::cout << "QP Retrieved " << table_rows.rows_count << " rows from " << table_name << std::endl;
        }

        // Handle JOIN if multiple table
        Rows<Row> joined_data;
        if (table_data.size() == 1) {
            joined_data = table_data[0];
        } else if (table_data.size() > 1) {
            joined_data = table_data[0];
            for (size_t i = 1; i < table_data.size(); i++) {
                Condition join_condition;
                std::string join_type = "INNER"; 
                
                // Get join type from join_clauses if available
                if (i - 1 < parsed_query.join_clauses.size()) {
                    const auto& join_clause = parsed_query.join_clauses[i - 1];
                    join_type = join_clause.is_natural ? "NATURAL" : join_clause.join_type;
                    
                    if (!join_clause.is_natural) {
                        join_condition.column = join_clause.left_expression;
                        join_condition.operation = "=";
                        join_condition.operand = join_clause.right_expression;
                    }
                } else if (i - 1 < parsed_query.join_pairs.size()) {
                    // Fallback to join_pairs for backward compatibility
                    join_condition.column = parsed_query.join_pairs[i - 1].first;
                    join_condition.operation = "=";
                    join_condition.operand = parsed_query.join_pairs[i - 1].second;
                }

                joined_data = execute_join(joined_data, table_data[i], join_condition, join_type);
            }
        }

        // Apply WHERE clause
        if (!parsed_query.where_conditions.empty()) {
            joined_data = apply_where_clause(joined_data, parsed_query.where_conditions);
        }

        // Apply ORDER BY
        if (!parsed_query.order_by_column.empty()) {
            joined_data = apply_order_by(joined_data, parsed_query.order_by_column, parsed_query.order_ascending);
        }

        // Apply LIMIT
        if (parsed_query.limit_value > 0) {
            joined_data = apply_limit(joined_data, parsed_query.limit_value);
        }

        if (!parsed_query.select_aliases.empty()) {
            bool is_select_all = false;
            if (parsed_query.select_columns.size() == 1 && parsed_query.select_columns[0] == "*") {
                is_select_all = true;
            }

            if (is_select_all) {
                result = joined_data;
            } else {
                Rows<Row> projected_data;
                projected_data.column_names = parsed_query.select_aliases;
                
                for (const auto& row : joined_data.data) {
                    Row new_row;
                    new_row.row_id = row.row_id;
                    new_row.table_name = row.table_name;
                    
                    for (size_t i = 0; i < parsed_query.select_columns.size(); ++i) {
                        if (i >= parsed_query.select_aliases.size()) break;

                        std::string source_col = parsed_query.select_columns[i];
                        std::string target_alias = parsed_query.select_aliases[i];
                        
                        bool found = false;
                        
                        if (row.columns.count(source_col)) {
                            new_row.columns[target_alias] = row.columns.at(source_col);
                            found = true;
                        }
                        else {
                            for (const auto& [key, val] : row.columns) {
                                if (key.size() > source_col.size() && 
                                    key.substr(key.size() - source_col.size()) == source_col &&
                                    key[key.size() - source_col.size() - 1] == '.') {
                                    new_row.columns[target_alias] = val;
                                    found = true;
                                    break;
                                }
                            }
                        }

                        if (!found) {
                            std::cerr << "QP: Warning: Column '" << source_col << "' not found for alias '" << target_alias << "'" << std::endl;
                        }
                    }
                    projected_data.data.push_back(new_row);
                }
                projected_data.rows_count = static_cast<int>(projected_data.data.size());
                result = projected_data;
            }
        } else {
             result = joined_data;
        }

    } catch (const std::exception& e) {
        std::cerr << "QP SELECT error: " << e.what() << std::endl;
        throw;
    }

    return result;
}

Rows<Row> QueryProcessor::apply_where_clause(const Rows<Row>& rows, const std::vector<Condition>& conditions) {
    Rows<Row> result;
    result.column_names = rows.column_names;

    // Helper function to find column value in a row (handles prefixed/aliased columns)
    auto find_column_value = [](const Row& row, const std::string& col_name) -> std::pair<bool, std::any> {
        // Direct match
        auto it = row.columns.find(col_name);
        if (it != row.columns.end()) {
            return {true, it->second};
        }

        // Try exact suffix match (e.g., "Item.CatID" looking for "CatID")
        for (const auto& col_pair : row.columns) {
            // Check if col_name is a suffix (after a dot)
            size_t dot_pos = col_pair.first.rfind('.');
            if (dot_pos != std::string::npos) {
                std::string suffix = col_pair.first.substr(dot_pos + 1);
                if (suffix == col_name) {
                    return {true, col_pair.second};
                }
            }
        }

        // Try prefix match (e.g., looking for "Item.CatID" when row has "CatID")
        size_t dot_pos = col_name.rfind('.');
        if (dot_pos != std::string::npos) {
            std::string col_suffix = col_name.substr(dot_pos + 1);
            std::string col_prefix = col_name.substr(0, dot_pos);

            // First try to find with full qualified name in row
            for (const auto& col_pair : row.columns) {
                if (col_pair.first == col_name) {
                    return {true, col_pair.second};
                }
            }

            // Then try just the suffix, but verify table name matches
            auto suffix_it = row.columns.find(col_suffix);
            if (suffix_it != row.columns.end()) {
                // Check if this could be from the right table (by checking table_name or prefix)
                return {true, suffix_it->second};
            }
        }

        return {false, std::any{}};
    };

    // Helper to check if operand is a column reference (contains "." with table prefix)
    auto is_column_reference = [](const std::any& operand) -> std::pair<bool, std::string> {
        if (operand.type() != typeid(std::string)) {
            return {false, ""};
        }
        std::string str = std::any_cast<std::string>(operand);
        size_t dot_pos = str.find('.');
        if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < str.size() - 1) {
            // Check if it looks like Table.Column (starts with uppercase or is a valid identifier)
            return {true, str};
        }
        return {false, ""};
    };

    for (const auto& row : rows.data) {
        bool matches_all = true;

        for (const auto& cond : conditions) {
            // Find left side column value
            auto [left_found, left_val] = find_column_value(row, cond.column);
            if (!left_found) {
                matches_all = false;
                break;
            }

            // Check if operand is a column reference (for column-to-column comparison)
            auto [is_col_ref, col_ref_name] = is_column_reference(cond.operand);

            std::any compare_val;
            if (is_col_ref) {
                // Column-to-column comparison (e.g., WHERE Item.CatID = Category.CatID)
                auto [right_found, right_val] = find_column_value(row, col_ref_name);
                if (!right_found) {
                    matches_all = false;
                    break;
                }
                compare_val = right_val;
            } else {
                // Regular literal comparison
                compare_val = cond.operand;
            }

            bool condition_met = false;

            try {
                // Compare integer
                if (left_val.type() == typeid(int)) {
                    int row_val = std::any_cast<int>(left_val);
                    int cond_val = 0;

                    if (compare_val.type() == typeid(int)) {
                        cond_val = std::any_cast<int>(compare_val);
                    } else if (compare_val.type() == typeid(float)) {
                        cond_val = static_cast<int>(std::any_cast<float>(compare_val));
                    } else if (compare_val.type() == typeid(double)) {
                        cond_val = static_cast<int>(std::any_cast<double>(compare_val));
                    }

                    if (cond.operation == "=") condition_met = (row_val == cond_val);
                    else if (cond.operation == "<>" || cond.operation == "!=") condition_met = (row_val != cond_val);
                    else if (cond.operation == ">") condition_met = (row_val > cond_val);
                    else if (cond.operation == ">=") condition_met = (row_val >= cond_val);
                    else if (cond.operation == "<") condition_met = (row_val < cond_val);
                    else if (cond.operation == "<=") condition_met = (row_val <= cond_val);
                }
                // Compare float
                else if (left_val.type() == typeid(float)) {
                    float row_val = std::any_cast<float>(left_val);
                    float cond_val = 0.0f;

                    if (compare_val.type() == typeid(float)) {
                        cond_val = std::any_cast<float>(compare_val);
                    } else if (compare_val.type() == typeid(int)) {
                        cond_val = static_cast<float>(std::any_cast<int>(compare_val));
                    } else if (compare_val.type() == typeid(double)) {
                        cond_val = static_cast<float>(std::any_cast<double>(compare_val));
                    }

                    if (cond.operation == "=") condition_met = (row_val == cond_val);
                    else if (cond.operation == "<>" || cond.operation == "!=") condition_met = (row_val != cond_val);
                    else if (cond.operation == ">") condition_met = (row_val > cond_val);
                    else if (cond.operation == ">=") condition_met = (row_val >= cond_val);
                    else if (cond.operation == "<") condition_met = (row_val < cond_val);
                    else if (cond.operation == "<=") condition_met = (row_val <= cond_val);
                }
                // Compare string
                else if (left_val.type() == typeid(std::string)) {
                    std::string row_val = std::any_cast<std::string>(left_val);
                    std::string cond_val;

                    if (compare_val.type() == typeid(std::string)) {
                        cond_val = std::any_cast<std::string>(compare_val);
                    }

                    if (cond.operation == "=") condition_met = (row_val == cond_val);
                    else if (cond.operation == "<>" || cond.operation == "!=") condition_met = (row_val != cond_val);
                    else if (cond.operation == ">") condition_met = (row_val > cond_val);
                    else if (cond.operation == ">=") condition_met = (row_val >= cond_val);
                    else if (cond.operation == "<") condition_met = (row_val < cond_val);
                    else if (cond.operation == "<=") condition_met = (row_val <= cond_val);
                }
            } catch (const std::bad_any_cast& e) {
                std::cerr << "QP: Type mismatch in WHERE clause for column " << cond.column << std::endl;
                condition_met = false;
            }

            if (!condition_met) {
                matches_all = false;
                break;
            }
        }

        if (matches_all) {
            result.data.push_back(row);
        }
    }

    result.rows_count = static_cast<int>(result.data.size());
    return result;
}

Rows<Row> QueryProcessor::apply_order_by(const Rows<Row>& rows, const std::string& column, bool ascending) {
    Rows<Row> result = rows;

    if (result.data.empty() || column.empty()) {
        return result;
    }

    if (result.data[0].columns.find(column) == result.data[0].columns.end()) {
        std::cerr << "QP: ORDER BY column '" << column << "' not found" << std::endl;
        return result;
    }

    const std::any& first_val = result.data[0].columns.at(column);

    std::sort(result.data.begin(), result.data.end(),
        [&column, ascending, &first_val](const Row& a, const Row& b) {
            auto it_a = a.columns.find(column);
            auto it_b = b.columns.find(column);
            if (it_a == a.columns.end() || it_b == b.columns.end()) {
                return false;
            }

            const std::any& val_a = it_a->second;
            const std::any& val_b = it_b->second;

            try {
                // Compare integer
                if (first_val.type() == typeid(int)) {
                    int a_val = std::any_cast<int>(val_a);
                    int b_val = std::any_cast<int>(val_b);
                    return ascending ? (a_val < b_val) : (a_val > b_val);
                }
                // Compare float
                else if (first_val.type() == typeid(float)) {
                    float a_val = std::any_cast<float>(val_a);
                    float b_val = std::any_cast<float>(val_b);
                    return ascending ? (a_val < b_val) : (a_val > b_val);
                }
                // Compare string
                else if (first_val.type() == typeid(std::string)) {
                    std::string a_val = std::any_cast<std::string>(val_a);
                    std::string b_val = std::any_cast<std::string>(val_b);
                    return ascending ? (a_val < b_val) : (a_val > b_val);
                }
            } catch (const std::bad_any_cast&) {
                return false;
            }

            return false;
        });

    return result;
}

Rows<Row> QueryProcessor::apply_limit(const Rows<Row>& rows, int limit) {
    Rows<Row> result;
    result.column_names = rows.column_names;

    if (limit <= 0) {
        return rows;
    }

    int count = 0;
    for (const auto& row : rows.data) {
        if (count >= limit) {
            break;
        }
        result.data.push_back(row);
        count++;
    }

    result.rows_count = static_cast<int>(result.data.size());
    return result;
}

int QueryProcessor::execute_update(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id) {
    int affected_rows = 0;

    try {
        if (parsed_query.from_tables.empty()) {
            throw std::runtime_error("UPDATE requires a table name");
        }

        std::string table_name = parsed_query.from_tables[0];

        try {
            mdbms::sm::StorageEngine::get_instance().get_table_schema(table_name);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("Table '" + table_name + "' does not exist");
        }

        // Request WRITE access ke CCM
        {
            Row request;
            request.table_name = table_name;
            request.row_id = -1;
            mdbms::ccm::ConcurrencyControlManager::get_instance().log_object(request, transaction_id);
            Response access = mdbms::ccm::ConcurrencyControlManager::get_instance().validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
            }
        }

        DataRetrieval retrieval;
        retrieval.table = table_name;
        retrieval.columns = {"*"};
        retrieval.search_type = SearchType::LINEAR;
        Rows<Row> all_rows = mdbms::sm::StorageEngine::get_instance().read_block(retrieval);

        Rows<Row> rows_to_update;
        if (!parsed_query.where_conditions.empty()) {
            rows_to_update = apply_where_clause(all_rows, parsed_query.where_conditions);
        } else {
            rows_to_update = all_rows;
        }

        std::cout << "QP: Found " << rows_to_update.rows_count << " rows to update in " << table_name << std::endl;

        auto evaluate_expression = [](const std::string& expr, const Row& row) -> std::any {
            std::string trimmed_expr = expr;
            size_t start = trimmed_expr.find_first_not_of(" \t\n\r");
            size_t end = trimmed_expr.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                trimmed_expr = trimmed_expr.substr(start, end - start + 1);
            }

            // Check for arithmetic operators
            std::vector<char> operators = {'+', '-', '*', '/'};
            char found_op = '\0';
            size_t op_pos = std::string::npos;

            for (char op : {'*', '/'}) {
                size_t pos = trimmed_expr.rfind(op);
                if (pos != std::string::npos && pos > 0 && pos < trimmed_expr.size() - 1) {
                    found_op = op;
                    op_pos = pos;
                    break;
                }
            }
            if (found_op == '\0') {
                for (char op : {'+', '-'}) {
                    size_t pos = trimmed_expr.rfind(op);
                    // skip if at position 0
                    if (pos != std::string::npos && pos > 0 && pos < trimmed_expr.size() - 1) {
                        found_op = op;
                        op_pos = pos;
                        break;
                    }
                }
            }

            if (found_op == '\0') {
                return std::any(trimmed_expr);
            }

            // Split into left and right operands
            std::string left_str = trimmed_expr.substr(0, op_pos);
            std::string right_str = trimmed_expr.substr(op_pos + 1);

            // Trim operands
            auto trim_str = [](std::string& s) {
                size_t start = s.find_first_not_of(" \t\n\r");
                size_t end = s.find_last_not_of(" \t\n\r");
                if (start != std::string::npos && end != std::string::npos) {
                    s = s.substr(start, end - start + 1);
                }
            };
            trim_str(left_str);
            trim_str(right_str);

            auto get_value = [&row](const std::string& operand) -> double {
                // Check if it's a column reference
                if (row.columns.find(operand) != row.columns.end()) {
                    const auto& val = row.columns.at(operand);
                    if (val.type() == typeid(int)) {
                        return static_cast<double>(std::any_cast<int>(val));
                    } else if (val.type() == typeid(float)) {
                        return static_cast<double>(std::any_cast<float>(val));
                    } else if (val.type() == typeid(double)) {
                        return std::any_cast<double>(val);
                    }
                }
                // Try to parse as numeric literal
                try {
                    return std::stod(operand);
                } catch (...) {
                    return 0.0;
                }
            };

            double left_val = get_value(left_str);
            double right_val = get_value(right_str);
            double result = 0.0;

            switch (found_op) {
                case '+': result = left_val + right_val; break;
                case '-': result = left_val - right_val; break;
                case '*': result = left_val * right_val; break;
                case '/':
                    if (right_val != 0.0) {
                        result = left_val / right_val;
                    }
                    break;
            }

            // Return as int if result is a whole number, otherwise as float
            if (result == static_cast<int>(result)) {
                return std::any(static_cast<int>(result));
            }
            return std::any(static_cast<float>(result));
        };

        // Check if any value is an expression that needs per-row evaluation
        bool has_expression = false;
        for (const auto& [col_name, new_value] : parsed_query.set_values) {
            if (new_value.type() == typeid(std::string)) {
                std::string value_str = std::any_cast<std::string>(new_value);
                // Check if it contains arithmetic operators
                if (value_str.find('+') != std::string::npos ||
                    value_str.find('-') != std::string::npos ||
                    value_str.find('*') != std::string::npos ||
                    value_str.find('/') != std::string::npos) {
                    has_expression = true;
                    break;
                }
            }
        }

        if (has_expression && !rows_to_update.data.empty()) {
            // Get table schema to find primary key
            TableSchema schema = mdbms::sm::StorageEngine::get_instance().get_table_schema(table_name);
            std::string primary_key = schema.primary_key;

            if (primary_key.empty()) {
                throw std::runtime_error("Cannot update with expressions: table '" + table_name +
                                       "' has no primary key. Expressions require a primary key to identify rows.");
            }

            // Update each row individually with evaluated expressions
            affected_rows = 0;
            for (const auto& row : rows_to_update.data) {
                DataWrite<Row> update_data;
                update_data.table = table_name;
                update_data.is_insert = false;

                // Create condition to match this specific row using primary key
                Condition pk_condition;
                pk_condition.column = primary_key;
                pk_condition.operation = "=";

                // Get the primary key value from this row
                if (row.columns.find(primary_key) != row.columns.end()) {
                    pk_condition.operand = row.columns.at(primary_key);
                } else {
                    std::cerr << "QP: Warning: Primary key '" << primary_key << "' not found in row" << std::endl;
                    continue;
                }

                update_data.conditions.push_back(pk_condition);

                for (const auto& [col_name, new_value] : parsed_query.set_values) {
                    update_data.columns.push_back(col_name);

                    // Check if new_value is a string expression that needs evaluation
                    std::any processed_value = new_value;
                    if (new_value.type() == typeid(std::string)) {
                        std::string value_str = std::any_cast<std::string>(new_value);
                        processed_value = evaluate_expression(value_str, row);
                    }

                    update_data.new_value.columns[col_name] = processed_value;
                }

                int rows_updated = mdbms::sm::StorageEngine::get_instance().write_block(update_data);
                affected_rows += rows_updated;
            }
        } else {
            DataWrite<Row> update_data;
            update_data.table = table_name;
            update_data.conditions = parsed_query.where_conditions;
            update_data.is_insert = false;

            for (const auto& [col_name, new_value] : parsed_query.set_values) {
                update_data.columns.push_back(col_name);
                update_data.new_value.columns[col_name] = new_value;
            }

            affected_rows = mdbms::sm::StorageEngine::get_instance().write_block(update_data);
        }

        std::cout << "QP: Updated " << affected_rows << " rows in " << table_name << std::endl;

        // Log to Failure Recovery Manager
        if (affected_rows > 0) {
            ExecutionResult log_result;
            log_result.transaction_id = transaction_id;
            log_result.timestamp = std::time(nullptr);
            // Use original_query if available, otherwise construct a query string
            if (!parsed_query.original_query.empty()) {
                log_result.query = parsed_query.original_query;
            } else {
                log_result.query = "UPDATE " + table_name;
            }
            log_result.success = true;
            log_result.affected_rows = affected_rows;
            
            // Store old and new values for rollback in ExecutionResult.data
            // FRM will extract old_value and new_value from ExecutionResult
            if (!rows_to_update.data.empty()) {
                // Store old value as first row in data
                log_result.data.data.push_back(rows_to_update.data[0]);
                // Store new value as second row (if needed, FRM will use it)
                Row new_row = rows_to_update.data[0];
                for (const auto& [col_name, new_value] : parsed_query.set_values) {
                    new_row.columns[col_name] = new_value;
                }
                log_result.data.data.push_back(new_row);
            }
            log_result.data.rows_count = static_cast<int>(log_result.data.data.size());
            
            mdbms::fr::FailureRecoveryManager::get_instance().write_log(log_result);
        }

    } catch (const std::exception& e) {
        std::cerr << "QP UPDATE error: " << e.what() << std::endl;
        throw;
    }

    return affected_rows;
}

int QueryProcessor::execute_insert(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id) {
    int affected_rows = 0;

    try {
        if (parsed_query.from_tables.empty()) {
            throw std::runtime_error("INSERT requires a table name");
        }

        std::string table_name = parsed_query.from_tables[0];

        try {
            mdbms::sm::StorageEngine::get_instance().get_table_schema(table_name);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("Table '" + table_name + "' does not exist");
        }
        TableSchema schema = mdbms::sm::StorageEngine::get_instance().get_table_schema(table_name);
        std::unordered_map<std::string, DataType> column_type_map;
        for (size_t i = 0; i < schema.column_names.size(); ++i) {
            column_type_map[schema.column_names[i]] = schema.column_types[i];
        }

        // Request WRITE access from CCM
        {
            Row request;
            request.table_name = table_name;
            request.row_id = -1;
            mdbms::ccm::ConcurrencyControlManager::get_instance().log_object(request, transaction_id);
            Response access = mdbms::ccm::ConcurrencyControlManager::get_instance().validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
            }
        }
        // Check primary key uniqueness
        if (!schema.primary_key.empty()) {
            // Find which index in insert_columns/insert_values corresponds to primary key
            std::vector<std::string> temp_column_names;
            if (!parsed_query.insert_columns.empty()) {
                temp_column_names = parsed_query.insert_columns;
            } else {
                temp_column_names = mdbms::sm::StorageEngine::get_instance().get_column_names(table_name);
            }
            
            // Find primary key value in the values being inserted
            std::any pk_value;
            bool pk_found = false;
            for (size_t i = 0; i < temp_column_names.size() && i < parsed_query.insert_values.size(); ++i) {
                if (temp_column_names[i] == schema.primary_key) {
                    pk_value = parsed_query.insert_values[i];
                    pk_found = true;
                    break;
                }
            }
            
            if (pk_found && pk_value.has_value()) {
                // Read existing rows to check for duplicate primary key
                DataRetrieval retrieval;
                retrieval.table = table_name;
                retrieval.columns = {schema.primary_key};
                retrieval.search_type = SearchType::LINEAR;
                Rows<Row> existing_rows = mdbms::sm::StorageEngine::get_instance().read_block(retrieval);
                
                for (const auto& row : existing_rows.data) {
                    auto it = row.columns.find(schema.primary_key);
                    if (it != row.columns.end()) {
                        bool is_duplicate = false;
                        try {
                            // Compare based on type
                            if (pk_value.type() == typeid(int) && it->second.type() == typeid(int)) {
                                is_duplicate = (std::any_cast<int>(pk_value) == std::any_cast<int>(it->second));
                            } else if (pk_value.type() == typeid(std::string) && it->second.type() == typeid(std::string)) {
                                is_duplicate = (std::any_cast<std::string>(pk_value) == std::any_cast<std::string>(it->second));
                            } else if (pk_value.type() == typeid(float) && it->second.type() == typeid(float)) {
                                is_duplicate = (std::any_cast<float>(pk_value) == std::any_cast<float>(it->second));
                            }
                        } catch (const std::bad_any_cast&) {
                            // Type mismatch, not a duplicate
                        }
                        
                        if (is_duplicate) {
                            throw std::runtime_error("Duplicate entry for primary key '" + schema.primary_key + 
                                                   "'. Primary key constraint violated.");
                        }
                    }
                }
                std::cout << "[DEBUG] QP INSERT: Primary key uniqueness check passed" << std::endl;
            }
        }

        // Get column names for mapping values
        std::vector<std::string> column_names;
        
        // INSERT specifies columns
        if (!parsed_query.insert_columns.empty()) {
            std::cout << "[DEBUG] QP INSERT: Using explicit column list: ";
            for (const auto& col : parsed_query.insert_columns) {
                std::cout << col << " ";
            }
            std::cout << std::endl;
            column_names = parsed_query.insert_columns;
        } else {
            // Get column names from schema (in correct order)
            std::cout << "[DEBUG] QP INSERT: No explicit columns, getting column names from schema..." << std::endl;
            column_names = mdbms::sm::StorageEngine::get_instance().get_column_names(table_name);
            
            if (column_names.empty()) {
                std::cerr << "[DEBUG] QP INSERT ERROR: Cannot determine column names for table: " << table_name << std::endl;
                std::cerr << "[DEBUG] QP INSERT ERROR: Table schema not found. Use CREATE TABLE first or specify columns in INSERT." << std::endl;
                throw std::runtime_error("Cannot determine column names for table: " + table_name + 
                                       ". Table schema not found. Use CREATE TABLE first or specify columns in INSERT statement.");
            }
            
            std::cout << "[DEBUG] QP INSERT: Got " << column_names.size() << " columns from schema" << std::endl;
        }

        std::cout << "[DEBUG] QP INSERT: Final column_names (" << column_names.size() << "): ";
        for (const auto& col : column_names) {
            std::cout << col << " ";
        }
        std::cout << std::endl;
        std::cout << "[DEBUG] QP INSERT: insert_values.size() = " << parsed_query.insert_values.size() << std::endl;

        // Validate value count matches column count
        if (parsed_query.insert_values.size() != column_names.size()) {
            throw std::runtime_error("Column count mismatch: expected " + 
                                   std::to_string(column_names.size()) + 
                                   " values, got " + 
                                   std::to_string(parsed_query.insert_values.size()));
        }

        DataWrite<Row> single_insert;
        single_insert.table = table_name;
        single_insert.is_insert = true;
        single_insert.new_value.table_name = table_name;
        
        // Map values to columns
        std::cout << "[DEBUG] QP INSERT: Mapping values to columns..." << std::endl;
        for (size_t i = 0; i < parsed_query.insert_values.size() && i < column_names.size(); ++i) {
            std::string col_name = column_names[i];
            std::cout << "[DEBUG] QP INSERT: Mapping value[" << i << "] to column: " << col_name << std::endl;
            std::any mapped_value = parsed_query.insert_values[i];

            auto type_it = column_type_map.find(col_name);
            if (type_it != column_type_map.end()) {
                if (type_it->second == DataType::FLOAT) {
                    if (mapped_value.type() == typeid(double)) {
                        mapped_value = static_cast<float>(std::any_cast<double>(mapped_value));
                    } else if (mapped_value.type() == typeid(int)) {
                        mapped_value = static_cast<float>(std::any_cast<int>(mapped_value));
                    }
                } else if (type_it->second == DataType::INTEGER) {
                    if (mapped_value.type() == typeid(double)) {
                        mapped_value = static_cast<int>(std::any_cast<double>(mapped_value));
                    } else if (mapped_value.type() == typeid(float)) {
                        mapped_value = static_cast<int>(std::any_cast<float>(mapped_value));
                    }
                }
            }

            single_insert.new_value.columns[col_name] = mapped_value;
            single_insert.columns.push_back(col_name);
        }
        
        std::cout << "[DEBUG] QP INSERT: Row columns after mapping (" << single_insert.new_value.columns.size() << "): ";
        // Print in schema order, not map iteration order
        for (const auto& col_name : column_names) {
            if (single_insert.new_value.columns.count(col_name)) {
                std::cout << col_name << " ";
            }
        }
        std::cout << std::endl;
        std::cout << "[DEBUG] QP INSERT: Row column values: ";
        for (const auto& col_name : column_names) {
            if (single_insert.new_value.columns.count(col_name)) {
                const auto& val = single_insert.new_value.columns.at(col_name);
                if (val.type() == typeid(int)) {
                    std::cout << col_name << "=" << std::any_cast<int>(val) << " ";
                } else if (val.type() == typeid(float)) {
                    std::cout << col_name << "=" << std::any_cast<float>(val) << " ";
                } else if (val.type() == typeid(double)) {
                    std::cout << col_name << "=" << std::any_cast<double>(val) << "(double) ";
                } else if (val.type() == typeid(std::string)) {
                    std::cout << col_name << "='" << std::any_cast<std::string>(val) << "' ";
                }
            }
        }
        std::cout << std::endl;

        int res = mdbms::sm::StorageEngine::get_instance().write_block(single_insert);
        if (res > 0) affected_rows = res;

        std::cout << "QP: Inserted " << affected_rows << " rows into " << table_name << std::endl;

        // Log to FRM
        if (affected_rows > 0) {
             ExecutionResult log_result;
            log_result.transaction_id = transaction_id;
            log_result.timestamp = std::time(nullptr);
            if (!parsed_query.original_query.empty()) {
                log_result.query = parsed_query.original_query;
            } else {
                log_result.query = "INSERT INTO " + table_name;
            }
            log_result.success = true;
            log_result.affected_rows = affected_rows;
            
            // Log inserted data for UNDO
            Rows<Row> inserted_data;
            inserted_data.column_names = column_names;
            inserted_data.data.push_back(single_insert.new_value);
            inserted_data.rows_count = 1;
            log_result.data = inserted_data;

            mdbms::fr::FailureRecoveryManager::get_instance().write_log(log_result);
        }

    } catch (const std::exception& e) {
        std::cerr << "QP INSERT error: " << e.what() << std::endl;
        throw;
    }

    return affected_rows;
}

int QueryProcessor::execute_delete(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id) {
    int affected_rows = 0;

    try {
        if (parsed_query.from_tables.empty()) {
            throw std::runtime_error("DELETE requires a table name");
        }

        std::string table_name = parsed_query.from_tables[0];

        try {
            mdbms::sm::StorageEngine::get_instance().get_table_schema(table_name);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("Table '" + table_name + "' does not exist");
        }

        // Request WRITE access
        {
            Row request;
            request.table_name = table_name;
            request.row_id = -1; 
            mdbms::ccm::ConcurrencyControlManager::get_instance().log_object(request, transaction_id);
            Response access = mdbms::ccm::ConcurrencyControlManager::get_instance().validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
            }
        }

        // Find rows to delete first (for logging and accurate count)
        DataRetrieval retrieval;
        retrieval.table = table_name;
        retrieval.columns = {"*"};
        retrieval.search_type = SearchType::LINEAR;
        Rows<Row> all_rows = mdbms::sm::StorageEngine::get_instance().read_block(retrieval);

        Rows<Row> rows_to_delete;
        if (!parsed_query.where_conditions.empty()) {
            rows_to_delete = apply_where_clause(all_rows, parsed_query.where_conditions);
        } else {
            rows_to_delete = all_rows;
        }

        std::cout << "QP: Found " << rows_to_delete.rows_count << " rows to delete from " << table_name << std::endl;

        DataDeletion deletion;
        deletion.table = table_name;
        deletion.conditions = parsed_query.where_conditions;

        affected_rows = mdbms::sm::StorageEngine::get_instance().delete_block(deletion);

        std::cout << "QP: Deleted " << affected_rows << " rows from " << table_name << std::endl;

        // Log to FRM
        if (affected_rows > 0) {
            ExecutionResult log_result;
            log_result.transaction_id = transaction_id;
            log_result.timestamp = std::time(nullptr);
            if (!parsed_query.original_query.empty()) {
                log_result.query = parsed_query.original_query;
            } else {
                log_result.query = "DELETE FROM " + table_name;
            }
            log_result.success = true;
            log_result.affected_rows = affected_rows;
            
            // Store deleted rows for UNDO (insert them back)
            log_result.data = rows_to_delete;
            
            mdbms::fr::FailureRecoveryManager::get_instance().write_log(log_result);
        }

    } catch (const std::exception& e) {
        std::cerr << "QP DELETE error: " << e.what() << std::endl;
        throw;
    }

    return affected_rows;
}

bool QueryProcessor::execute_create_table(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id) {
    try {
        if (parsed_query.target_table.empty()) {
            throw std::runtime_error("CREATE TABLE requires a table name");
        }

        if (parsed_query.column_definitions.empty()) {
            throw std::runtime_error("CREATE TABLE requires at least one column");
        }

        std::string table_name = parsed_query.target_table;

        {
            Row request;
            request.table_name = table_name;
            request.row_id = -1;
            mdbms::ccm::ConcurrencyControlManager::get_instance().log_object(request, transaction_id);
            Response access = mdbms::ccm::ConcurrencyControlManager::get_instance().validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
            }
        }

        TableSchema schema;
        schema.table_name = table_name;

        auto convert_data_type = [](const std::string& type_str) -> DataType {
            std::string upper_type = type_str;
            std::transform(upper_type.begin(), upper_type.end(), upper_type.begin(), ::toupper);
            
            if (upper_type == "INT" || upper_type == "INTEGER") {
                return DataType::INTEGER;
            } else if (upper_type == "FLOAT" || upper_type == "REAL") {
                return DataType::FLOAT;
            } else if (upper_type == "CHAR") {
                return DataType::CHAR;
            } else if (upper_type == "VARCHAR") {
                return DataType::VARCHAR;
            } else {
                throw std::runtime_error("Unsupported data type: " + type_str);
            }
        };

        for (const auto& col_def : parsed_query.column_definitions) {
            schema.column_names.push_back(col_def.name);
            schema.column_types.push_back(convert_data_type(col_def.data_type));
            schema.column_sizes.push_back(col_def.length);

            if (col_def.is_primary_key) {
                if (!schema.primary_key.empty()) {
                    throw std::runtime_error("Multiple primary keys not supported");
                }
                schema.primary_key = col_def.name;
            }

            if (col_def.is_foreign_key && !col_def.references_table.empty()) {
                std::string ref = col_def.references_table;
                if (!col_def.references_column.empty()) {
                    ref += "." + col_def.references_column;
                }
                schema.foreign_keys[col_def.name] = ref;
            }
        }

        bool success = mdbms::sm::StorageEngine::get_instance().create_table(schema);

        if (success) {
            std::cout << "QP: Created table " << table_name << std::endl;
            {
                ExecutionResult log_result;
                log_result.transaction_id = transaction_id;
                log_result.timestamp = std::time(nullptr);
                if (!parsed_query.original_query.empty()) {
                    log_result.query = parsed_query.original_query;
                } else {
                    log_result.query = "CREATE TABLE " + table_name;
                }
                log_result.table_name = table_name;
                log_result.success = true;
                log_result.affected_rows = 0;
                mdbms::fr::FailureRecoveryManager::get_instance().write_log(log_result);
            }
        }

        return success;

    } catch (const std::exception& e) {
        std::cerr << "QP CREATE TABLE error: " << e.what() << std::endl;
        throw;
    }
}

bool QueryProcessor::execute_drop_table(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id) {
    try {
        if (parsed_query.target_table.empty()) {
            throw std::runtime_error("DROP TABLE requires a table name");
        }

        std::string table_name = parsed_query.target_table;
        
        mdbms::fr::FailureRecoveryManager::get_instance().prepare_ddl_operation(table_name, Operation::DROP_TABLE);

        {
            Row request;
            request.table_name = table_name;
            request.row_id = -1;
            mdbms::ccm::ConcurrencyControlManager::get_instance().log_object(request, transaction_id);
            Response access = mdbms::ccm::ConcurrencyControlManager::get_instance().validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
            }
        }

        bool success = mdbms::sm::StorageEngine::get_instance().drop_table(table_name);

        if (success) {
            std::cout << "QP: Dropped table " << table_name << std::endl;
            {
                ExecutionResult log_result;
                log_result.transaction_id = transaction_id;
                log_result.timestamp = std::time(nullptr);
                if (!parsed_query.original_query.empty()) {
                    log_result.query = parsed_query.original_query;
                } else {
                    log_result.query = "DROP TABLE " + table_name;
                }
                log_result.table_name = table_name;
                log_result.success = true;
                log_result.affected_rows = 0;
                mdbms::fr::FailureRecoveryManager::get_instance().write_log(log_result);
            }
        }

        return success;

    } catch (const std::exception& e) {
        std::cerr << "QP DROP TABLE error: " << e.what() << std::endl;
        throw;
    }
}

int QueryProcessor::begin_transaction() {
    int tid = mdbms::ccm::ConcurrencyControlManager::get_instance().begin_transaction();
    std::cout << "QP: Transaction " << tid << " started" << std::endl;

    // Log to recovery manager
    {
        ExecutionResult log_entry;
        log_entry.transaction_id = tid;
        log_entry.query = "BEGIN TRANSACTION";
        log_entry.timestamp = std::time(nullptr);
        log_entry.success = true;
        mdbms::fr::FailureRecoveryManager::get_instance().write_log(log_entry);
    }

    return tid;
}

bool QueryProcessor::commit_transaction(int transaction_id) {
    if (transaction_id == -1) {
        std::cerr << "QP: No active transaction to commit" << std::endl;
        return false;
    }

    std::cout << "QP: Committing transaction " << transaction_id << std::endl;

    // End transaction in CCM
    mdbms::ccm::ConcurrencyControlManager::get_instance().end_transaction(transaction_id);

    // Use FRM commit with proper WAL protocol
    mdbms::fr::FailureRecoveryManager::get_instance().commit_transaction(transaction_id);

    return true;
}

bool QueryProcessor::abort_transaction(int transaction_id) {
    if (transaction_id == -1) {
        std::cerr << "QP: No active transaction to abort" << std::endl;
        return false;
    }

    std::cout << "QP: Aborting transaction " << transaction_id << std::endl;

    // End transaction in CCM
    mdbms::ccm::ConcurrencyControlManager::get_instance().end_transaction(transaction_id);

    // Use FRM abort (efficient: discard buffer + UNDO from disk if needed)
    mdbms::fr::FailureRecoveryManager::get_instance().abort_transaction(transaction_id);

    return true;
}

// Old abort implementation (for reference, can be removed)
bool QueryProcessor::abort_transaction_old(int transaction_id) {
    if (transaction_id == -1) {
        std::cerr << "QP: No active transaction to abort" << std::endl;
        return false;
    }

    std::cout << "QP: Aborting transaction (old method) " << transaction_id << std::endl;

    // End transaction in CCM
    mdbms::ccm::ConcurrencyControlManager::get_instance().end_transaction(transaction_id);

    // Request recovery manager to UNDO changes
    {
        RecoverCriteria criteria;
        criteria.transaction_id = transaction_id;
        criteria.use_timestamp = false;
        // Note: This old method always UNDOs from disk, even if data is in buffer
        // mdbms::fr::FailureRecoveryManager::get_instance().recover(criteria);

        // Log abort
        ExecutionResult log_entry;
        log_entry.transaction_id = transaction_id;
        log_entry.query = "ABORT";
        log_entry.timestamp = std::time(nullptr);
        log_entry.success = true;
        mdbms::fr::FailureRecoveryManager::get_instance().write_log(log_entry);
    }

    return true;
}

std::string QueryProcessor::parse_query_type(const std::string& query) {
    std::istringstream iss(query);
    std::string first_word;
    iss >> first_word;
    std::transform(first_word.begin(), first_word.end(), first_word.begin(), ::toupper);

    return first_word;
}

std::string QueryProcessor::resolve_aliased_column(const std::string& column, 
                                                    const std::map<std::string, std::string>& table_aliases) {
    size_t dot_pos = column.find('.');
    if (dot_pos != std::string::npos) {
        std::string prefix = column.substr(0, dot_pos);
        std::string col_name = column.substr(dot_pos + 1);
        
        if (table_aliases.find(prefix) != table_aliases.end()) {
            return col_name;  // Return just the column name
        }
    }
    return column;  // Return original if no alias prefix
}

std::string QueryProcessor::get_table_from_alias(const std::string& alias,
                                                  const std::map<std::string, std::string>& table_aliases) {
    auto it = table_aliases.find(alias);
    if (it != table_aliases.end()) {
        return it->second;  // Return actual table name
    }
    return alias;  // Return original if not an alias
}

Rows<Row> QueryProcessor::apply_table_aliases(const Rows<Row>& rows, const std::string& table_name,
                                               const std::map<std::string, std::string>& table_aliases) {
    Rows<Row> result;
    
    std::string alias = "";
    for (const auto& pair : table_aliases) {
        if (pair.second == table_name) {
            // Prefer explicit alias over table name itself
            if (pair.first != table_name) {
                alias = pair.first;
                break;
            }
        }
    }
    
    // If alias is same as table name, no aliasing needed
    if (alias.empty() || alias == table_name) {
        return rows;
    }
    
    // Create new column names with alias prefix
    for (const auto& col : rows.column_names) {
        // Add alias prefix to column names (e.g., "StudentID" -> "s.StudentID")
        result.column_names.push_back(alias + "." + col);
    }
    
    // Copy rows with aliased column names
    for (const auto& row : rows.data) {
        Row new_row;
        new_row.row_id = row.row_id;
        new_row.table_name = row.table_name;
        
        for (const auto& col_pair : row.columns) {
            std::string aliased_col = alias + "." + col_pair.first;
            new_row.columns[aliased_col] = col_pair.second;
        }
        
        result.data.push_back(new_row);
    }
    
    result.rows_count = result.data.size();
    return result;
}

Rows<Row> QueryProcessor::execute_join(const Rows<Row>& left_table, const Rows<Row>& right_table,
                                        const Condition& join_condition, const std::string& join_type) {
    Rows<Row> result;

    // Determine if this is cartesian product 
    bool is_cartesian = join_condition.column.empty() && join_condition.operation.empty() && join_type != "NATURAL";

    // Get table names for prefixing 
    std::string left_table_name = !left_table.data.empty() ? left_table.data[0].table_name : "left";
    std::string right_table_name = !right_table.data.empty() ? right_table.data[0].table_name : "right";

    // NATURAL JOIN: Find common columns and join on them
    if (join_type == "NATURAL") {
        // Find common columns between left and right tables
        std::vector<std::string> common_columns;

        if (!left_table.data.empty() && !right_table.data.empty()) {
            const auto& left_row = left_table.data[0];
            const auto& right_row = right_table.data[0];

            // Extract base column names 
            auto get_base_column = [](const std::string& col) -> std::string {
                size_t dot_pos = col.rfind('.');
                if (dot_pos != std::string::npos) {
                    return col.substr(dot_pos + 1);
                }
                return col;
            };

            // Find common columns
            for (const auto& left_col : left_row.columns) {
                std::string left_base = get_base_column(left_col.first);
                
                for (const auto& right_col : right_row.columns) {
                    std::string right_base = get_base_column(right_col.first);
                    
                    if (left_base == right_base) {
                        // Check if not already added
                        if (std::find(common_columns.begin(), common_columns.end(), left_base) == common_columns.end()) {
                            common_columns.push_back(left_base);
                        }
                    }
                }
            }
        }

        if (common_columns.empty()) {
            // No common columns, return empty result
            std::cout << "QP: NATURAL JOIN - No common columns found" << std::endl;
            result.rows_count = 0;
            return result;
        }

        std::cout << "QP: NATURAL JOIN on columns: ";
        for (const auto& col : common_columns) {
            std::cout << col << " ";
        }
        std::cout << std::endl;

        // Add all columns from left table first
        for (const auto& col : left_table.column_names) {
            std::string base_col = col;
            size_t dot_pos = col.rfind('.');
            if (dot_pos != std::string::npos) {
                base_col = col.substr(dot_pos + 1);
            }
            result.column_names.push_back(base_col);
        }
        
        // Add columns from right table, skip common columns
        for (const auto& col : right_table.column_names) {
            std::string base_col = col;
            size_t dot_pos = col.rfind('.');
            if (dot_pos != std::string::npos) {
                base_col = col.substr(dot_pos + 1);
            }
            
            // Only add if not a common column
            if (std::find(common_columns.begin(), common_columns.end(), base_col) == common_columns.end()) {
                result.column_names.push_back(base_col);
            }
        }

        // Helper to find column value with or without prefix
        auto find_column_value = [](const Row& row, const std::string& col_name) -> std::pair<bool, std::any> {
            // Try direct match first
            auto it = row.columns.find(col_name);
            if (it != row.columns.end()) {
                return {true, it->second};
            }

            // Try to find by suffix 
            for (const auto& col_pair : row.columns) {
                size_t dot_pos = col_pair.first.rfind('.');
                if (dot_pos != std::string::npos) {
                    std::string suffix = col_pair.first.substr(dot_pos + 1);
                    if (suffix == col_name) {
                        return {true, col_pair.second};
                    }
                } else if (col_pair.first == col_name) {
                    return {true, col_pair.second};
                }
            }

            return {false, std::any{}};
        };

        // Perform natural join
        for (const auto& left_row : left_table.data) {
            for (const auto& right_row : right_table.data) {
                bool all_match = true;

                // Check if all common columns have matching values
                for (const auto& common_col : common_columns) {
                    auto [left_found, left_val] = find_column_value(left_row, common_col);
                    auto [right_found, right_val] = find_column_value(right_row, common_col);

                    if (!left_found || !right_found) {
                        all_match = false;
                        break;
                    }

                    // Compare values
                    bool values_match = false;
                    try {
                        if (left_val.type() == typeid(int) && right_val.type() == typeid(int)) {
                            values_match = (std::any_cast<int>(left_val) == std::any_cast<int>(right_val));
                        } else if (left_val.type() == typeid(float) && right_val.type() == typeid(float)) {
                            values_match = (std::any_cast<float>(left_val) == std::any_cast<float>(right_val));
                        } else if (left_val.type() == typeid(std::string) && right_val.type() == typeid(std::string)) {
                            values_match = (std::any_cast<std::string>(left_val) == std::any_cast<std::string>(right_val));
                        } else if (left_val.type() == typeid(int) && right_val.type() == typeid(float)) {
                            values_match = (std::any_cast<int>(left_val) == static_cast<int>(std::any_cast<float>(right_val)));
                        } else if (left_val.type() == typeid(float) && right_val.type() == typeid(int)) {
                            values_match = (static_cast<int>(std::any_cast<float>(left_val)) == std::any_cast<int>(right_val));
                        }
                    } catch (const std::bad_any_cast&) {
                        values_match = false;
                    }

                    if (!values_match) {
                        all_match = false;
                        break;
                    }
                }

                if (all_match) {
                    Row joined_row;
                    joined_row.row_id = left_row.row_id;
                    joined_row.table_name = left_row.table_name;

                    // Add all columns from left table first
                    for (const auto& col : left_row.columns) {
                        std::string base_col = col.first;
                        size_t dot_pos = col.first.rfind('.');
                        if (dot_pos != std::string::npos) {
                            base_col = col.first.substr(dot_pos + 1);
                        }
                        
                        // Add column with base name
                        joined_row.columns[base_col] = col.second;
                    }

                    // Add columns from right table, skip common columns 
                    for (const auto& col : right_row.columns) {
                        std::string base_col = col.first;
                        size_t dot_pos = col.first.rfind('.');
                        if (dot_pos != std::string::npos) {
                            base_col = col.first.substr(dot_pos + 1);
                        }
                        
                        if (joined_row.columns.find(base_col) == joined_row.columns.end()) {
                            joined_row.columns[base_col] = col.second;
                        }
                    }

                    result.data.push_back(joined_row);
                }
            }
        }

        result.rows_count = static_cast<int>(result.data.size());
        std::cout << "QP: NATURAL JOIN produced " << result.rows_count << " rows" << std::endl;
        return result;
    }

    // Merge column names from both tables for non-NATURAL joins
    for (const auto& col : left_table.column_names) {
        if (is_cartesian && col.find('.') == std::string::npos) {
            result.column_names.push_back(left_table_name + "." + col);
        } else {
            result.column_names.push_back(col);
        }
    }
    for (const auto& col : right_table.column_names) {
        if (is_cartesian && col.find('.') == std::string::npos) {
            result.column_names.push_back(right_table_name + "." + col);
        } else {
            result.column_names.push_back(col);
        }
    }

    // INNER JOIN with ON condition or Cartesian Product (CROSS JOIN)
    if (is_cartesian) {
        // Cartesian Product (CROSS JOIN)
        std::cout << "QP: Performing Cartesian Product" << std::endl;

        for (const auto& left_row : left_table.data) {
            for (const auto& right_row : right_table.data) {
                Row joined_row;
                joined_row.row_id = left_row.row_id;
                joined_row.table_name = left_row.table_name;

                // Copy all columns from left table (prefix with table name to avoid collision)
                for (const auto& col : left_row.columns) {
                    // If column already has a prefix (contains '.'), keep it otherwise add table name
                    if (col.first.find('.') != std::string::npos) {
                        joined_row.columns[col.first] = col.second;
                    } else {
                        std::string prefixed_col = left_row.table_name + "." + col.first;
                        joined_row.columns[prefixed_col] = col.second;
                    }
                }

                // Copy all columns from right table (prefix with table name to avoid collision)
                for (const auto& col : right_row.columns) {
                    // If column already has a prefix (contains '.'), keep it otherwise add table name
                    if (col.first.find('.') != std::string::npos) {
                        joined_row.columns[col.first] = col.second;
                    } else {
                        std::string prefixed_col = right_row.table_name + "." + col.first;
                        joined_row.columns[prefixed_col] = col.second;
                    }
                }

                result.data.push_back(joined_row);
            }
        }

        result.rows_count = static_cast<int>(result.data.size());
        std::cout << "QP: Cartesian Product produced " << result.rows_count << " rows" << std::endl;
        return result;
    }

    // JOIN ON condition
    std::string left_col = join_condition.column;
    std::string right_col;

    // The operand contains the right column name as a string
    try {
        right_col = std::any_cast<std::string>(join_condition.operand);
    } catch (const std::bad_any_cast&) {
        std::cerr << "QP: JOIN condition operand is not a column name" << std::endl;
        result.rows_count = 0;
        return result;
    }

    // Helper function to find column value (handles aliased/prefixed columns)
    auto find_column_value = [](const Row& row, const std::string& col_name) -> std::pair<bool, std::any> {
        // Direct match
        auto it = row.columns.find(col_name);
        if (it != row.columns.end()) {
            return {true, it->second};
        }

        // If col_name has a prefix try to find just the suffix 
        size_t col_dot_pos = col_name.rfind('.');
        if (col_dot_pos != std::string::npos) {
            std::string col_suffix = col_name.substr(col_dot_pos + 1);
            auto suffix_it = row.columns.find(col_suffix);
            if (suffix_it != row.columns.end()) {
                return {true, suffix_it->second};
            }
        }

        // Try to find by suffix 
        for (const auto& col_pair : row.columns) {
            size_t dot_pos = col_pair.first.rfind('.');
            if (dot_pos != std::string::npos) {
                std::string suffix = col_pair.first.substr(dot_pos + 1);
                if (suffix == col_name) {
                    return {true, col_pair.second};
                }
            }
        }

        return {false, std::any{}};
    };

    // Perform join
    for (const auto& left_row : left_table.data) {
        auto [left_found, left_val] = find_column_value(left_row, left_col);
        if (!left_found) {
            continue;
        }

        for (const auto& right_row : right_table.data) {
            auto [right_found, right_val] = find_column_value(right_row, right_col);
            if (!right_found) {
                continue;
            }

            // Compare values based on the operation
            bool condition_met = false;

            try {
                // Handle different type combinations
                if (left_val.type() == typeid(int)) {
                    int left_int = std::any_cast<int>(left_val);
                    int right_int = 0;

                    if (right_val.type() == typeid(int)) {
                        right_int = std::any_cast<int>(right_val);
                    } else if (right_val.type() == typeid(float)) {
                        right_int = static_cast<int>(std::any_cast<float>(right_val));
                    } else if (right_val.type() == typeid(double)) {
                        right_int = static_cast<int>(std::any_cast<double>(right_val));
                    }

                    if (join_condition.operation == "=") condition_met = (left_int == right_int);
                    else if (join_condition.operation == "<>") condition_met = (left_int != right_int);
                    else if (join_condition.operation == ">") condition_met = (left_int > right_int);
                    else if (join_condition.operation == ">=") condition_met = (left_int >= right_int);
                    else if (join_condition.operation == "<") condition_met = (left_int < right_int);
                    else if (join_condition.operation == "<=") condition_met = (left_int <= right_int);
                } else if (left_val.type() == typeid(float)) {
                    float left_float = std::any_cast<float>(left_val);
                    float right_float = 0.0f;

                    if (right_val.type() == typeid(float)) {
                        right_float = std::any_cast<float>(right_val);
                    } else if (right_val.type() == typeid(int)) {
                        right_float = static_cast<float>(std::any_cast<int>(right_val));
                    } else if (right_val.type() == typeid(double)) {
                        right_float = static_cast<float>(std::any_cast<double>(right_val));
                    }

                    if (join_condition.operation == "=") condition_met = (left_float == right_float);
                    else if (join_condition.operation == "<>") condition_met = (left_float != right_float);
                    else if (join_condition.operation == ">") condition_met = (left_float > right_float);
                    else if (join_condition.operation == ">=") condition_met = (left_float >= right_float);
                    else if (join_condition.operation == "<") condition_met = (left_float < right_float);
                    else if (join_condition.operation == "<=") condition_met = (left_float <= right_float);
                } else if (left_val.type() == typeid(std::string)) {
                    std::string left_str = std::any_cast<std::string>(left_val);
                    std::string right_str = std::any_cast<std::string>(right_val);

                    if (join_condition.operation == "=") condition_met = (left_str == right_str);
                    else if (join_condition.operation == "<>") condition_met = (left_str != right_str);
                    else if (join_condition.operation == ">") condition_met = (left_str > right_str);
                    else if (join_condition.operation == ">=") condition_met = (left_str >= right_str);
                    else if (join_condition.operation == "<") condition_met = (left_str < right_str);
                    else if (join_condition.operation == "<=") condition_met = (left_str <= right_str);
                }
            } catch (const std::bad_any_cast& e) {
                std::cerr << "QP: Type mismatch in JOIN condition" << std::endl;
                continue;
            }

            if (condition_met) {
                Row joined_row;
                joined_row.row_id = left_row.row_id;
                joined_row.table_name = left_row.table_name;

                // Copy all columns from left table
                for (const auto& col : left_row.columns) {
                    joined_row.columns[col.first] = col.second;
                }

                // Copy all columns from right table
                for (const auto& col : right_row.columns) {
                    joined_row.columns[col.first] = col.second;
                }

                result.data.push_back(joined_row);
            }
        }
    }

    result.rows_count = static_cast<int>(result.data.size());
    std::cout << "QP: JOIN produced " << result.rows_count << " rows" << std::endl;
    return result;
}

} // namespace mdbms::qp

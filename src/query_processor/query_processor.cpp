#include "query_processor.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <utility>
#include "concurrency_control.h"
#include "failure_recovery.h"
#include "query_optimizer.h"
#include "storage_manager.h"

namespace mdbms::qp {

QueryProcessor::QueryProcessor()
    : current_transaction_id(-1), explicit_transaction_started(false) {
    // All components are singletons, always use get_instance()
    qo_engine = &mdbms::qo::OptimizationEngine::get_instance();
    sm_engine = &mdbms::sm::StorageEngine::get_instance();
    ccm_manager = &mdbms::ccm::ConcurrencyControlManager::get_instance();
    frm_manager = &mdbms::fr::FailureRecoveryManager::get_instance();

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
        mdbms::qo::ParsedQuery optimized_query = qo_engine->analyze_query(query, sm_engine);

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

        // Log to Failure Recovery Manager
        if (result.success) {
            frm_manager->write_log(result);
        }

        // Auto-commit queries only if transaction was auto-started (not explicitly started with BEGIN)
        if (ccm_manager && !explicit_transaction_started) {
            ccm_manager->end_transaction(result.transaction_id);
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

        for (const auto& table_name : parsed_query.from_tables) {
            try {
                sm_engine->get_table_schema(table_name);
            } catch (const std::runtime_error& e) {
                throw std::runtime_error("Table '" + table_name + "' does not exist");
            }

            DataRetrieval retrieval;
            retrieval.table = table_name;
            retrieval.columns = parsed_query.select_columns;
            retrieval.search_type = SearchType::LINEAR;

            if (ccm_manager) {
                Row request;
                request.table_name = table_name;
                request.row_id = -1;
                ccm_manager->log_object(request, transaction_id);
                Response access = ccm_manager->validate_object(request, transaction_id, Action::READ);
                if (!access.allowed) {
                    throw std::runtime_error("Concurrency control denied READ access for table " + table_name);
                }
            }

            // Check if table has index
            // if (!parsed_query.where_conditions.empty()) {
            //     const auto& first_condition = parsed_query.where_conditions[0];
            //     if (sm_engine->has_index(table_name, first_condition.column)) {
            //         retrieval.search_type = SearchType::INDEX_SCAN;
            //         retrieval.index_column = first_condition.column;
            //     }
            // }

            // TODO:
            // Request access permission from Concurrency Control Manager
            // For SELECT, we need READ permission on all rows

            // Read data from storage
            Rows<Row> table_rows = sm_engine->read_block(retrieval);
            table_data.push_back(table_rows);

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
                if (i - 1 < parsed_query.join_pairs.size()) {
                    join_condition.column = parsed_query.join_pairs[i - 1].first;
                    join_condition.operation = "=";
                    join_condition.operand = parsed_query.join_pairs[i - 1].second;
                }

                joined_data = execute_join(joined_data, table_data[i], join_condition, "INNER");
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

        result = joined_data;

    } catch (const std::exception& e) {
        std::cerr << "QP SELECT error: " << e.what() << std::endl;
        throw;
    }

    return result;
}

Rows<Row> QueryProcessor::apply_where_clause(const Rows<Row>& rows, const std::vector<Condition>& conditions) {
    Rows<Row> result;
    result.column_names = rows.column_names;

    for (const auto& row : rows.data) {
        bool matches_all = true;

        for (const auto& cond : conditions) {
            if (row.columns.find(cond.column) == row.columns.end()) {
                matches_all = false;
                break;
            }

            const std::any& value = row.columns.at(cond.column);
            bool condition_met = false;

            try {
                // Compare integer
                if (value.type() == typeid(int)) {
                    int row_val = std::any_cast<int>(value);
                    int cond_val = 0;

                    if (cond.operand.type() == typeid(int)) {
                        cond_val = std::any_cast<int>(cond.operand);
                    } else if (cond.operand.type() == typeid(float)) {
                        cond_val = static_cast<int>(std::any_cast<float>(cond.operand));
                    } else if (cond.operand.type() == typeid(double)) {
                        cond_val = static_cast<int>(std::any_cast<double>(cond.operand));
                    }

                    if (cond.operation == "=") condition_met = (row_val == cond_val);
                    else if (cond.operation == "<>") condition_met = (row_val != cond_val);
                    else if (cond.operation == ">") condition_met = (row_val > cond_val);
                    else if (cond.operation == ">=") condition_met = (row_val >= cond_val);
                    else if (cond.operation == "<") condition_met = (row_val < cond_val);
                    else if (cond.operation == "<=") condition_met = (row_val <= cond_val);
                }
                // Compare float
                else if (value.type() == typeid(float)) {
                    float row_val = std::any_cast<float>(value);
                    float cond_val = 0.0f;

                    if (cond.operand.type() == typeid(float)) {
                        cond_val = std::any_cast<float>(cond.operand);
                    } else if (cond.operand.type() == typeid(int)) {
                        cond_val = static_cast<float>(std::any_cast<int>(cond.operand));
                    } else if (cond.operand.type() == typeid(double)) {
                        cond_val = static_cast<float>(std::any_cast<double>(cond.operand));
                    }

                    if (cond.operation == "=") condition_met = (row_val == cond_val);
                    else if (cond.operation == "<>") condition_met = (row_val != cond_val);
                    else if (cond.operation == ">") condition_met = (row_val > cond_val);
                    else if (cond.operation == ">=") condition_met = (row_val >= cond_val);
                    else if (cond.operation == "<") condition_met = (row_val < cond_val);
                    else if (cond.operation == "<=") condition_met = (row_val <= cond_val);
                }
                // Compare string
                else if (value.type() == typeid(std::string)) {
                    std::string row_val = std::any_cast<std::string>(value);
                    std::string cond_val = std::any_cast<std::string>(cond.operand);

                    if (cond.operation == "=") condition_met = (row_val == cond_val);
                    else if (cond.operation == "<>") condition_met = (row_val != cond_val);
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

Rows<Row> QueryProcessor::execute_join(const Rows<Row>& left_table, const Rows<Row>& right_table, const Condition& join_condition, const std::string& join_type) {
    Rows<Row> result;

    result.column_names = left_table.column_names;
    for (const auto& col : right_table.column_names) {
        result.column_names.push_back(col);
    }

    std::string left_col = join_condition.column;
    std::string right_col;

    if (join_type == "NATURAL") {
        std::vector<std::string> common_columns;

        if (!left_table.data.empty() && !right_table.data.empty()) {
            for (const auto& left_col_name : left_table.column_names) {
                if (std::find(right_table.column_names.begin(), right_table.column_names.end(), left_col_name) != right_table.column_names.end()) {
                    if (right_table.data[0].columns.find(left_col_name) != right_table.data[0].columns.end()) {
                        common_columns.push_back(left_col_name);
                    }
                }
            }
        }

        // Kalau tidak ada common columns, dilakukan cartesian product
        if (common_columns.empty()) {
            for (const auto& left_row : left_table.data) {
                for (const auto& right_row : right_table.data) {
                    Row merged;
                    merged.table_name = left_row.table_name + "_" + right_row.table_name;
                    merged.columns = left_row.columns;
                    for (const auto& key : right_table.column_names) {
                        if (right_row.columns.count(key)) {
                            merged.columns[key] = right_row.columns.at(key);
                        }
                    }
                    result.data.push_back(merged);
                }
            }
            result.rows_count = static_cast<int>(result.data.size());
            return result;
        }

        // Natural join on common columns
        for (const auto& left_row : left_table.data) {
            for (const auto& right_row : right_table.data) {
                bool all_match = true;

                // Check all common columns match
                for (const auto& col_name : common_columns) {
                    auto left_it = left_row.columns.find(col_name);
                    auto right_it = right_row.columns.find(col_name);

                    if (left_it == left_row.columns.end() || right_it == right_row.columns.end()) {
                        all_match = false;
                        break;
                    }

                    const std::any& left_val = left_it->second;
                    const std::any& right_val = right_it->second;

                    bool values_match = false;
                    if (left_val.type() == typeid(int) && right_val.type() == typeid(int)) {
                        values_match = (std::any_cast<int>(left_val) == std::any_cast<int>(right_val));
                    } else if (left_val.type() == typeid(float) && right_val.type() == typeid(float)) {
                        values_match = (std::any_cast<float>(left_val) == std::any_cast<float>(right_val));
                    } else if (left_val.type() == typeid(std::string) && right_val.type() == typeid(std::string)) {
                        values_match = (std::any_cast<std::string>(left_val) == std::any_cast<std::string>(right_val));
                    }

                    if (!values_match) {
                        all_match = false;
                        break;
                    }
                }

                if (all_match) {
                    Row merged;
                    merged.table_name = left_row.table_name + "_" + right_row.table_name;
                    merged.columns = left_row.columns;

                    for (const auto& key : right_table.column_names) {
                        if (std::find(common_columns.begin(), common_columns.end(), key) == common_columns.end()) {
                            if (right_row.columns.count(key)) {
                                merged.columns[key] = right_row.columns.at(key);
                            }
                        }
                    }
                    result.data.push_back(merged);
                }
            }
        }

        result.rows_count = static_cast<int>(result.data.size());
        std::cout << "QP: NATURAL JOIN result: " << result.rows_count << " rows" << std::endl;
        return result;
    }

    // Cek apakah join_condition memiliki operand
    bool has_join_condition = join_condition.operand.has_value() && join_condition.operand.type() == typeid(std::string) && !join_condition.column.empty();

    if (has_join_condition) {
        right_col = std::any_cast<std::string>(join_condition.operand);
    } else {
        // Kalau tidak ada operand, dilakukan cartesian product
        for (const auto& left_row : left_table.data) {
            for (const auto& right_row : right_table.data) {
                Row merged;
                merged.table_name = left_row.table_name + "_" + right_row.table_name;
                merged.columns = left_row.columns;
                for (const auto& key : right_table.column_names) {
                    if (right_row.columns.count(key)) {
                        merged.columns[key] = right_row.columns.at(key);
                    }
                }
                result.data.push_back(merged);
            }
        }
        result.rows_count = static_cast<int>(result.data.size());
        return result;
    }

    // Nested loop join 
    for (const auto& left_row : left_table.data) {
        bool matched = false;

        for (const auto& right_row : right_table.data) {
            auto left_it = left_row.columns.find(left_col);
            auto right_it = right_row.columns.find(right_col);

            if (left_it == left_row.columns.end() || right_it == right_row.columns.end()) {
                continue;
            }

            bool join_match = false;
            const std::any& left_val = left_it->second;
            const std::any& right_val = right_it->second;

            try {
                if (left_val.type() == typeid(int) && right_val.type() == typeid(int)) {
                    join_match = (std::any_cast<int>(left_val) == std::any_cast<int>(right_val));
                } else if (left_val.type() == typeid(float) && right_val.type() == typeid(float)) {
                    join_match = (std::any_cast<float>(left_val) == std::any_cast<float>(right_val));
                } else if (left_val.type() == typeid(std::string) && right_val.type() == typeid(std::string)) {
                    join_match = (std::any_cast<std::string>(left_val) == std::any_cast<std::string>(right_val));
                }
                else if (left_val.type() == typeid(int) && right_val.type() == typeid(float)) {
                    join_match = (static_cast<float>(std::any_cast<int>(left_val)) == std::any_cast<float>(right_val));
                } else if (left_val.type() == typeid(float) && right_val.type() == typeid(int)) {
                    join_match = (std::any_cast<float>(left_val) == static_cast<float>(std::any_cast<int>(right_val)));
                }
            } catch (const std::bad_any_cast&) {
                continue;
            }

            if (join_match) {
                matched = true;
                Row merged;
                merged.table_name = left_row.table_name + "_" + right_row.table_name;
                merged.columns = left_row.columns;

                for (const auto& key : right_table.column_names) {
                    if (right_row.columns.count(key)) {
                        if (merged.columns.find(key) != merged.columns.end()) {
                            merged.columns[right_row.table_name + "." + key] = right_row.columns.at(key);
                        } else {
                            merged.columns[key] = right_row.columns.at(key);
                        }
                    }
                }

                result.data.push_back(merged);
            }
        }
    }

    result.rows_count = static_cast<int>(result.data.size());
    std::cout << "QP: JOIN result: " << result.rows_count << " rows" << std::endl;
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
            sm_engine->get_table_schema(table_name);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("Table '" + table_name + "' does not exist");
        }

        // Request WRITE access ke CCM (nunggu integrasi ccm)
        if (ccm_manager) {
            Row request;
            request.table_name = table_name;
            request.row_id = -1;
            ccm_manager->log_object(request, transaction_id);
            Response access = ccm_manager->validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
            }
        }

        DataRetrieval retrieval;
        retrieval.table = table_name;
        retrieval.columns = {"*"};
        retrieval.search_type = SearchType::LINEAR;
        Rows<Row> all_rows = sm_engine->read_block(retrieval);

        Rows<Row> rows_to_update;
        if (!parsed_query.where_conditions.empty()) {
            rows_to_update = apply_where_clause(all_rows, parsed_query.where_conditions);
        } else {
            rows_to_update = all_rows;
        }

        std::cout << "QP: Found " << rows_to_update.rows_count << " rows to update in " << table_name << std::endl;

        DataWrite<Row> update_data;
        update_data.table = table_name;
        update_data.conditions = parsed_query.where_conditions;
        update_data.is_insert = false;

        for (const auto& [col_name, new_value] : parsed_query.set_values) {
            update_data.columns.push_back(col_name);
            update_data.new_value.columns[col_name] = new_value;
        }

        affected_rows = sm_engine->write_block(update_data);

        std::cout << "QP: Updated " << affected_rows << " rows in " << table_name << std::endl;

        // Log to Failure Recovery Manager
        if (frm_manager && affected_rows > 0) {
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
            
            frm_manager->write_log(log_result);
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
            sm_engine->get_table_schema(table_name);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("Table '" + table_name + "' does not exist");
        }

        // Request WRITE access from CCM
        if (ccm_manager) {
            Row request;
            request.table_name = table_name;
            request.row_id = -1;
            ccm_manager->log_object(request, transaction_id);
            Response access = ccm_manager->validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
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
            column_names = sm_engine->get_column_names(table_name);
            
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
            single_insert.new_value.columns[col_name] = parsed_query.insert_values[i];
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

        int res = sm_engine->write_block(single_insert);
        if (res > 0) affected_rows = res;

        std::cout << "QP: Inserted " << affected_rows << " rows into " << table_name << std::endl;

        // Log to FRM
        if (frm_manager && affected_rows > 0) {
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

            frm_manager->write_log(log_result);
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
            sm_engine->get_table_schema(table_name);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("Table '" + table_name + "' does not exist");
        }

        // Request WRITE access
        if (ccm_manager) {
            Row request;
            request.table_name = table_name;
            request.row_id = -1; 
            ccm_manager->log_object(request, transaction_id);
            Response access = ccm_manager->validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
            }
        }

        // Find rows to delete first (for logging and accurate count)
        DataRetrieval retrieval;
        retrieval.table = table_name;
        retrieval.columns = {"*"};
        retrieval.search_type = SearchType::LINEAR;
        Rows<Row> all_rows = sm_engine->read_block(retrieval);

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

        affected_rows = sm_engine->delete_block(deletion);

        std::cout << "QP: Deleted " << affected_rows << " rows from " << table_name << std::endl;

        // Log to FRM
        if (frm_manager && affected_rows > 0) {
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
            
            frm_manager->write_log(log_result);
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

        if (ccm_manager) {
            Row request;
            request.table_name = table_name;
            request.row_id = -1;
            ccm_manager->log_object(request, transaction_id);
            Response access = ccm_manager->validate_object(request, transaction_id, Action::WRITE);
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

        bool success = sm_engine->create_table(schema);

        if (success) {
            std::cout << "QP: Created table " << table_name << std::endl;

            if (frm_manager) {
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
                frm_manager->write_log(log_result);
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

        if (ccm_manager) {
            Row request;
            request.table_name = table_name;
            request.row_id = -1;
            ccm_manager->log_object(request, transaction_id);
            Response access = ccm_manager->validate_object(request, transaction_id, Action::WRITE);
            if (!access.allowed) {
                throw std::runtime_error("Concurrency control denied WRITE access for table " + table_name);
            }
        }

        bool success = sm_engine->drop_table(table_name);

        if (success) {
            std::cout << "QP: Dropped table " << table_name << std::endl;

            if (frm_manager) {
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
                frm_manager->write_log(log_result);
            }
        }

        return success;

    } catch (const std::exception& e) {
        std::cerr << "QP DROP TABLE error: " << e.what() << std::endl;
        throw;
    }
}

int QueryProcessor::begin_transaction() {
    int tid = ccm_manager->begin_transaction();
    std::cout << "QP: Transaction " << tid << " started" << std::endl;

    // Log to recovery manager
    if (frm_manager) {
        ExecutionResult log_entry;
        log_entry.transaction_id = tid;
        log_entry.query = "BEGIN TRANSACTION";
        log_entry.timestamp = std::time(nullptr);
        log_entry.success = true;
        frm_manager->write_log(log_entry);
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
    if (ccm_manager) {
        ccm_manager->end_transaction(transaction_id);
    }

    // Use FRM commit with proper WAL protocol
    if (frm_manager) {
        frm_manager->commit_transaction(transaction_id);  // ✅ WAL: Log flush → Data flush
    }

    return true;
}

bool QueryProcessor::abort_transaction(int transaction_id) {
    if (transaction_id == -1) {
        std::cerr << "QP: No active transaction to abort" << std::endl;
        return false;
    }

    std::cout << "QP: Aborting transaction " << transaction_id << std::endl;

    // End transaction in CCM
    if (ccm_manager) {
        ccm_manager->end_transaction(transaction_id);
    }

    // Use FRM abort (efficient: discard buffer + UNDO from disk if needed)
    if (frm_manager) {
        frm_manager->abort_transaction(transaction_id);  // ✅ Efficient ABORT
    }

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
    if (ccm_manager) {
        ccm_manager->end_transaction(transaction_id);
    }

    // Request recovery manager to UNDO changes
    if (frm_manager) {
        RecoverCriteria criteria;
        criteria.transaction_id = transaction_id;
        criteria.use_timestamp = false;
        // Note: This old method always UNDOs from disk, even if data is in buffer
        // frm_manager->recover(criteria);

        // Log abort
        ExecutionResult log_entry;
        log_entry.transaction_id = transaction_id;
        log_entry.query = "ABORT";
        log_entry.timestamp = std::time(nullptr);
        log_entry.success = true;
        frm_manager->write_log(log_entry);
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

} // namespace mdbms::qp

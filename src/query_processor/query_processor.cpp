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

QueryProcessor::QueryProcessor(std::shared_ptr<mdbms::qo::OptimizationEngine> optimizer,
                               std::shared_ptr<mdbms::sm::StorageEngine> storage,
                               std::shared_ptr<mdbms::ccm::ConcurrencyControlManager> concurrency,
                               std::shared_ptr<mdbms::fr::FailureRecoveryManager> recovery)
    : current_transaction_id(-1) {
    if (optimizer) {
        qo_engine = std::move(optimizer);
    } else {
        qo_engine = std::make_shared<mdbms::qo::OptimizationEngine>();
    }
    if (storage) {
        sm_engine = std::move(storage);
    } else {
        sm_engine = std::make_shared<mdbms::sm::StorageEngine>();
    }
    if (concurrency) {
        ccm_manager = std::move(concurrency);
    } else {
        ccm_manager = std::make_shared<mdbms::ccm::ConcurrencyControlManager>();
    }
    if (recovery) {
        frm_manager = std::move(recovery);
    } else {
        frm_manager = std::make_shared<mdbms::fr::FailureRecoveryManager>();
    }

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
        // Get query type
        std::string query_type = parse_query_type(query);
        std::cout << "Query type: " << query_type << std::endl;

        // Handle Transaction 
        if (query_type == "BEGIN") {
            int tid = begin_transaction();
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
            return result;
        } else if (query_type == "ROLLBACK" || query_type == "ABORT") {
            bool success = abort_transaction(current_transaction_id);
            result.transaction_id = current_transaction_id;
            result.message = success ? "Transaction aborted" : "Abort failed";
            result.success = success;
            current_transaction_id = -1;
            return result;
        }

        // Ensure have transaction 
        if (current_transaction_id == -1) {
            current_transaction_id = begin_transaction();
        }
        result.transaction_id = current_transaction_id;

        // Parse and optimize query
        mdbms::qo::ParsedQuery parsed_query = qo_engine->parse_query(query);
        mdbms::qo::ParsedQuery optimized_query = qo_engine->optimize_query(parsed_query);

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
            // Next milestone aja
        } else if (query_type == "DROP") {
            // Next milestone aja
        } else {
            throw std::runtime_error("Unsupported query type: " + query_type);
        }

        // Log to Failure Recovery Manager
        if (result.success) {
            frm_manager->write_log(result);
        }

        if (result.success && ccm_manager && query_type == "SELECT") {
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
        }
    }
    return result;
}

Rows<Row> QueryProcessor::execute_select(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id) {
    Rows<Row> result;

    try {
        std::vector<Rows<Row>> table_data;

        for (const auto& table_name : parsed_query.from_tables) {
            DataRetrieval retrieval;
            retrieval.table = table_name;
            retrieval.columns = parsed_query.select_columns;
            retrieval.search_type = SearchType::LINEAR;

            if (ccm_manager) {
                Row request;
                request.table_name = table_name;
                request.row_id = -1;
                Response access = ccm_manager->validate_object(request, transaction_id, Action::READ);
                if (!access.allowed) {
                    throw std::runtime_error("Concurrency control denied READ access for table " + table_name);
                }
                ccm_manager->log_object(request, transaction_id);
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
        if (parsed_query.limit_value > 0 && parsed_query.limit_value < joined_data.rows_count) {
            joined_data.data.resize(parsed_query.limit_value);
            joined_data.rows_count = parsed_query.limit_value;
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
            for (const auto& [left_col_name, _] : left_table.data[0].columns) {
                if (right_table.data[0].columns.find(left_col_name) != right_table.data[0].columns.end()) {
                    common_columns.push_back(left_col_name);
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
                    for (const auto& [key, val] : right_row.columns) {
                        merged.columns[key] = val;
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

                    for (const auto& [key, val] : right_row.columns) {
                        if (std::find(common_columns.begin(), common_columns.end(), key) == common_columns.end()) {
                            merged.columns[key] = val;
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
                for (const auto& [key, val] : right_row.columns) {
                    merged.columns[key] = val;
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

                for (const auto& [key, val] : right_row.columns) {
                    if (merged.columns.find(key) != merged.columns.end()) {
                        merged.columns[right_row.table_name + "." + key] = val;
                    } else {
                        merged.columns[key] = val;
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

// TODO
int QueryProcessor::execute_update(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id){
    return 0;
}

// TODO
int QueryProcessor::execute_insert(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id){
    return 0;
}

// TODO
int QueryProcessor::execute_delete(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id){
    return 0;
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

// TODO
bool QueryProcessor::commit_transaction(int transaction_id) {
    // if (transaction_id == -1) {
    //     std::cerr << "[QueryProcessor] No active transaction to commit" << std::endl;
    //     return false;
    // }

    // std::cout << "[QueryProcessor] Committing transaction " << transaction_id << std::endl;

    // // End transaction in CCM
    // if (ccm_manager) {
    //     ccm_manager->end_transaction(transaction_id, true);  // true = commit
    // }

    // // Log to recovery manager
    // if (frm_manager) {
    //     ExecutionResult log_entry;
    //     log_entry.transaction_id = transaction_id;
    //     log_entry.query = "COMMIT";
    //     log_entry.timestamp = std::time(nullptr);
    //     log_entry.success = true;
    //     frm_manager->write_log(log_entry);
    // }

    return true;
}

// TODO
bool QueryProcessor::abort_transaction(int transaction_id) {
    // if (transaction_id == -1) {
    //     std::cerr << "[QueryProcessor] No active transaction to abort" << std::endl;
    //     return false;
    // }

    // std::cout << "[QueryProcessor] Aborting transaction " << transaction_id << std::endl;

    // // End transaction in CCM
    // if (ccm_manager) {
    //     ccm_manager->end_transaction(transaction_id, false);  // false = abort
    // }

    // // Request recovery manager to UNDO changes
    // if (frm_manager) {
    //     RecoverCriteria criteria;
    //     criteria.transaction_id = transaction_id;
    //     criteria.use_timestamp = false;
    //     frm_manager->recover(criteria);

    //     // Log abort
    //     ExecutionResult log_entry;
    //     log_entry.transaction_id = transaction_id;
    //     log_entry.query = "ABORT";
    //     log_entry.timestamp = std::time(nullptr);
    //     log_entry.success = true;
    //     frm_manager->write_log(log_entry);
    // }

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
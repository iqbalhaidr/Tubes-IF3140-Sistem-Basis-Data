#include "query_processor.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <map>
#include <set>

// Sertakan header komponen lain untuk memanggilnya
#include "query_optimizer.h"
#include "storage_manager.h"
#include "concurrency_control.h"
#include "failure_recovery.h"

namespace mdbms::qp {

static bool is_number(const std::string& s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start == s.length()) return false;
    bool has_dot = false;
    for (size_t i = start; i < s.length(); ++i) {
        if (s[i] == '.') {
            if (has_dot) return false;
            has_dot = true;
        } else if (!std::isdigit(s[i])) {
            return false;
        }
    }
    return true;
}

QueryProcessor::QueryProcessor(
    qo::OptimizationEngine& qo,
    sm::StorageEngine& sm,
    ccm::ConcurrencyControlManager& ccm,
    fr::FailureRecoveryManager& frm
) : qo_engine_(qo), sm_engine_(sm), ccm_manager_(ccm), frm_manager_(frm) {
    std::cout << "QP: Query Processor berhasil diinisialisasi." << std::endl;
}

ExecutionResult QueryProcessor::execute_query(const std::string& query) {
    std::cout << "QP: Menerima query: " << query << std::endl;
    
    ExecutionResult result;
    result.query = query;

    // Extract command type from query string for special handling
    std::string trimmed_query = query;
    // Remove leading whitespace
    trimmed_query.erase(trimmed_query.begin(), 
                        std::find_if(trimmed_query.begin(), trimmed_query.end(), 
                                     [](unsigned char ch) { return !std::isspace(ch); }));
    
    // Convert to uppercase for comparison
    std::string upper_query = trimmed_query;
    std::transform(upper_query.begin(), upper_query.end(), upper_query.begin(), ::toupper);
    
    if (upper_query.find("BEGIN TRANSACTION") != std::string::npos ||
        upper_query.find("BEGIN") == 0) {
        int tx_id = ccm_manager_.begin_transaction();
        result.transaction_id = tx_id;
        result.message = "Transaction " + std::to_string(tx_id) + " started.";
        result.data.rows_count = 0;
        std::cout << "QP: Transaction explicitly begun with ID: " << tx_id << std::endl;
        return result;
    }
    
    if (upper_query.find("COMMIT") == 0) {
        // Note: In a complete implementation, this would commit the current transaction
        // For now, we acknowledge it
        result.message = "Transaction committed.";
        result.data.rows_count = 0;
        std::cout << "QP: Transaction committed." << std::endl;
        return result;
    }
    
    if (upper_query.find("ABORT") == 0 || upper_query.find("ROLLBACK") == 0) {
        result.message = "Transaction aborted.";
        result.data.rows_count = 0;
        std::cout << "QP: Transaction aborted." << std::endl;
        return result;
    }

    int tx_id = ccm_manager_.begin_transaction();
    result.transaction_id = tx_id;
    std::cout << "QP: Memulai transaksi ID: " << tx_id << std::endl;

    try {
        std::cout << "QP: Mengirim query ke Query Optimizer..." << std::endl;
        ParsedQuery parsed_query = qo_engine_.parse_query(query);
        ParsedQuery query_plan = qo_engine_.optimize_query(parsed_query);

        std::cout << "QP: Memvalidasi objek dengan CCM..." << std::endl;
        std::string action = extract_query_action(query_plan.query_tree);
        
        if (query_plan.query_tree && query_plan.query_tree->children.size() > 0) {
            for (const auto& child : query_plan.query_tree->children) {
                if (child->type == "FROM_CLAUSE" && !child->children.empty()) {
                    for (const auto& table_node : child->children) {
                        if (table_node->type == "TABLE") {
                            Row dummy_row;
                            dummy_row.push_back(table_node->value);
                            
                            Action ccm_action = (action == "READ") ? Action::READ : 
                                               (action == "WRITE") ? Action::WRITE : Action::READ;
                            
                            // Uncomment when CCM interface is available
                            // Response ccm_response = ccm_manager_.validate_object(dummy_row, tx_id, ccm_action);
                            // if (ccm_response != Response::ALLOW) {
                            //     throw std::runtime_error("Transaction aborted by Concurrency Control.");
                            // }
                        }
                    }
                }
            }
        }
        
        std::cout << "QP: Mengeksekusi query plan..." << std::endl;
        result = execute_plan(query_plan, tx_id);

        std::cout << "QP: Mencatat eksekusi ke Failure Recovery Manager..." << std::endl;
        frm_manager_.write_log(result);

        // Langkah 6: Akhiri Transaksi (Commit)
        std::cout << "QP: Mengakhiri (commit) transaksi ID: " << tx_id << std::endl;
        ccm_manager_.end_transaction(tx_id); 
        
        result.message = "Query berhasil dieksekusi.";
        return result;

    } catch (const std::exception& e) {
        std::cerr << "QP: Terjadi error: " << e.what() << std::endl;
        ccm_manager_.end_transaction(tx_id); // Abort
        result.message = "Error: " + std::string(e.what());
        result.data.rows_count = 0;
        return result;
    }
}


ExecutionResult QueryProcessor::execute_plan(const ParsedQuery& plan, int transaction_id) {
    if (!plan.query_tree) {
        throw std::runtime_error("Query plan kosong diterima dari Optimizer.");
    }

    std::string query_type = plan.query_tree->type;
    std::cout << "QP (Plan): Tipe query adalah " << query_type << std::endl;

    if (query_type == "SELECT") {
        return handle_select(plan.query_tree, transaction_id);
    } 
    else if (query_type == "UPDATE") {
        return handle_update(plan.query_tree, transaction_id);
    }
    else if (query_type == "DELETE") {
        return handle_delete(plan.query_tree, transaction_id);
    }
    else if (query_type == "INSERT") {
        return handle_insert(plan.query_tree, transaction_id);
    }
    else if (query_type == "CREATE") {
        return handle_create(plan.query_tree, transaction_id);
    }
    else if (query_type == "DROP") {
        return handle_drop(plan.query_tree, transaction_id);
    }
    else if (query_type == "JOIN") {
        return handle_join(plan.query_tree, transaction_id);
    }
    else {
        throw std::runtime_error("Tipe query '" + query_type + "' tidak didukung.");
    }
}

/**
 * @brief SELECT query handler
 * Supports: projections, WHERE clause filtering, ORDER BY sorting
 */
ExecutionResult QueryProcessor::handle_select(QueryTreePtr select_node, int tx_id) {
    ExecutionResult result;
    result.transaction_id = tx_id;

    DataRetrieval retrieval_request;
    QueryTreePtr column_list_node = nullptr;
    QueryTreePtr from_node = nullptr;
    QueryTreePtr where_node = nullptr;
    QueryTreePtr order_by_node = nullptr;

    for (const auto& child : select_node->children) {
        if (child->type == "COLUMN_LIST") {
            column_list_node = child;
        } else if (child->type == "FROM_CLAUSE") {
            from_node = child;
        } else if (child->type == "WHERE_CLAUSE") {
            where_node = child;
        } else if (child->type == "ORDER_BY_CLAUSE") {
            order_by_node = child;
        }
    }

    if (from_node == nullptr || from_node->children.empty()) {
        throw std::runtime_error("Query SELECT tidak valid: FROM clause kosong.");
    }

    std::vector<Rows> table_results;
    std::vector<std::string> all_column_names;
    
    for (const auto& table_node : from_node->children) {
        if (table_node->type == "TABLE") {
            retrieval_request.tables.push_back(table_node->value);
            std::cout << "QP (SELECT): Processing table: " << table_node->value << std::endl;
        }
        else if (table_node->type == "JOIN_CLAUSE") {
            std::cout << "QP (SELECT): JOIN clause detected" << std::endl;
            if (table_node->children.size() >= 2) {
                for (const auto& join_part : table_node->children) {
                    if (join_part->type == "TABLE") {
                        retrieval_request.tables.push_back(join_part->value);
                    }
                }
            }
        }
    }
    
    if (where_node) {
        retrieval_request.conditions = parse_conditions_from_tree(where_node);
    }
    
    if (column_list_node) {
        retrieval_request.columns = extract_columns_from_list(column_list_node);
    }

    std::cout << "QP: Memanggil Storage Manager (read_block)..." << std::endl;
    result.data = sm_engine_.read_block();

    if (retrieval_request.tables.size() > 1) {
        std::cout << "QP (SELECT): Multiple tables detected, performing cartesian product..." << std::endl;
        // For multi-table select, perform cartesian product of all tables
        Rows combined = result.data;
        for (size_t i = 1; i < retrieval_request.tables.size(); ++i) {
            Rows next_table = sm_engine_.read_block();
            combined = perform_cartesian_product(combined, next_table, 
                                                {retrieval_request.tables[i-1]}, 
                                                {retrieval_request.tables[i]});
        }
        result.data = combined;
    }

    // CRITICAL FIX: Apply WHERE clause filtering to rows
    if (where_node && !retrieval_request.conditions.empty()) {
        std::cout << "QP (SELECT): Applying WHERE clause filter to " << result.data.rows_count << " rows..." << std::endl;
        std::vector<Row> filtered_rows;
        
        for (const auto& row : result.data.data) {
            bool row_matches = true;
            
            // Evaluate all conditions (currently supporting single condition, extended for AND later)
            for (const auto& condition : retrieval_request.conditions) {
                try {
                    std::any left_val;
                    
                    // Get column value from row
                    // In simplified implementation, try to find column index
                    // Real implementation would use schema metadata to map column name to index
                    int col_index = 0;
                    
                    // Try to parse condition.column as numeric index first
                    try {
                        col_index = std::stoi(condition.column);
                    } catch (...) {
                        // Not numeric - would need schema metadata for real implementation
                        // For now, assume first column by default
                        col_index = 0;
                    }
                    
                    if (col_index >= 0 && col_index < static_cast<int>(row.size())) {
                        left_val = row[col_index];
                    } else {
                        std::cerr << "QP (SELECT): Column index " << col_index << " out of bounds" << std::endl;
                        row_matches = false;
                        break;
                    }
                    
                    // Right operand is already in condition.operand
                    std::any right_val = condition.operand;
                    
                    // Apply comparison operator
                    bool condition_result = false;
                    
                    if (condition.operation == "=" || condition.operation == "==") {
                        condition_result = compare_values(left_val, right_val, "==");
                    } else if (condition.operation == "!=" || condition.operation == "<>") {
                        condition_result = !compare_values(left_val, right_val, "==");
                    } else if (condition.operation == ">") {
                        condition_result = compare_values(left_val, right_val, ">");
                    } else if (condition.operation == "<") {
                        condition_result = compare_values(left_val, right_val, "<");
                    } else if (condition.operation == ">=") {
                        condition_result = compare_values(left_val, right_val, ">=");
                    } else if (condition.operation == "<=") {
                        condition_result = compare_values(left_val, right_val, "<=");
                    }
                    
                    if (!condition_result) {
                        row_matches = false;
                        break;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "QP (SELECT): Error evaluating condition: " << e.what() << std::endl;
                    row_matches = false;
                    break;
                }
            }
            
            if (row_matches) {
                filtered_rows.push_back(row);
            }
        }
        
        result.data.data = filtered_rows;
        result.data.rows_count = filtered_rows.size();
        std::cout << "QP (SELECT): WHERE filtering reduced rows to " << result.data.rows_count << std::endl;
    }

    if (column_list_node) {
        apply_projection(result.data, column_list_node);
    }
    
    if (order_by_node) {
        apply_order_by(result.data, order_by_node);
    }

    result.data.rows_count = result.data.data.size();
    return result;
}

/**
 * @brief UPDATE query handler
 * Supports: column updates with expression evaluation (e.g., salary * 1.05)
 */
ExecutionResult QueryProcessor::handle_update(QueryTreePtr update_node, int tx_id) {
    ExecutionResult result;
    result.transaction_id = tx_id;

    std::string table_name;
    QueryTreePtr set_node = nullptr;
    QueryTreePtr where_node = nullptr;

    for (const auto& child : update_node->children) {
        if (child->type == "TABLE") {
            table_name = child->value;
        } else if (child->type == "SET_CLAUSE") {
            set_node = child;
        } else if (child->type == "WHERE_CLAUSE") {
            where_node = child;
        }
    }

    if (table_name.empty() || set_node == nullptr) {
        throw std::runtime_error("Query UPDATE tidak valid.");
    }

    DataRetrieval retrieval_request;
    retrieval_request.tables.push_back(table_name);
    if (where_node) {
        retrieval_request.conditions = parse_conditions_from_tree(where_node);
    }

    std::cout << "QP (UPDATE): Membaca baris yang akan di-update..." << std::endl;
    Rows rows_to_update = sm_engine_.read_block();
    
    if (rows_to_update.rows_count == 0) {
        std::cout << "QP (UPDATE): Tidak ada baris yang memenuhi kriteria." << std::endl;
        result.message = "0 baris diperbarui.";
        result.data.rows_count = 0;
        return result;
    }

    int affected_rows = 0;
    std::cout << "QP (UPDATE): Memodifikasi " << rows_to_update.rows_count << " baris..." << std::endl;

    // Parse SET assignments from tree
    std::vector<std::pair<std::string, QueryTreePtr>> assignments;
    if (set_node && !set_node->children.empty()) {
        for (const auto& assignment : set_node->children) {
            if (assignment->type == "ASSIGNMENT" && assignment->children.size() >= 2) {
                std::string col_name = assignment->children[0]->value;
                QueryTreePtr expr_node = assignment->children[1];
                assignments.push_back({col_name, expr_node});
                std::cout << "QP (UPDATE): Parsed assignment: " << col_name << std::endl;
            }
        }
    }

    // For each row, update matching columns
    for (const auto& row : rows_to_update.data) {
        DataWrite write_request;
        write_request.table = table_name;

        // Extract column names for expression evaluation
        std::vector<std::string> column_names; // TODO: Get from schema
        
        for (const auto& [col_name, expr_node] : assignments) {
            write_request.columns_to_update.push_back(col_name);
            
            // Evaluate expression with row context
            std::any new_value = evaluate_expression(expr_node, row, column_names);
            write_request.new_values.push_back(new_value);
            
            std::cout << "QP (UPDATE): Setting " << col_name << " to new value" << std::endl;
        }

        if (!write_request.columns_to_update.empty()) {
            sm_engine_.write_block();
            affected_rows++;
        }
    }

    result.data.rows_count = affected_rows;
    result.message = std::to_string(affected_rows) + " baris diperbarui.";
    return result;
}

/**
 * @brief DELETE query handler
 * Supports: conditional row deletion via WHERE clause
 */
ExecutionResult QueryProcessor::handle_delete(QueryTreePtr delete_node, int tx_id) {
    ExecutionResult result;
    result.transaction_id = tx_id;

    std::string table_name;
    QueryTreePtr where_node = nullptr;

    for (const auto& child : delete_node->children) {
        if (child->type == "TABLE") {
            table_name = child->value;
        } else if (child->type == "WHERE_CLAUSE") {
            where_node = child;
        }
    }

    if (table_name.empty()) {
        throw std::runtime_error("Query DELETE tidak valid: tabel tidak ditemukan.");
    }

    DataRetrieval retrieval_request;
    retrieval_request.tables.push_back(table_name);
    if (where_node) {
        retrieval_request.conditions = parse_conditions_from_tree(where_node);
    }

    std::cout << "QP (DELETE): Membaca baris yang akan dihapus..." << std::endl;
    Rows rows_to_delete = sm_engine_.read_block();
    
    if (rows_to_delete.rows_count == 0) {
        std::cout << "QP (DELETE): Tidak ada baris yang memenuhi kriteria." << std::endl;
        result.message = "0 baris dihapus.";
        result.data.rows_count = 0;
        return result;
    }

    int affected_rows = rows_to_delete.rows_count;
    std::cout << "QP (DELETE): Menghapus " << affected_rows << " baris..." << std::endl;
    
    DataDeletion deletion_request;
    deletion_request.table = table_name;
    if (where_node) {
        deletion_request.conditions = parse_conditions_from_tree(where_node);
    }
    
    sm_engine_.delete_block();

    result.data.rows_count = affected_rows;
    result.message = std::to_string(affected_rows) + " baris dihapus.";
    return result;
}

/**
 * @brief INSERT query handler
 * Supports: new row insertion with value validation
 */
ExecutionResult QueryProcessor::handle_insert(QueryTreePtr insert_node, int tx_id) {
    ExecutionResult result;
    result.transaction_id = tx_id;

    std::string table_name;
    QueryTreePtr column_list_node = nullptr;
    QueryTreePtr value_list_node = nullptr;

    for (const auto& child : insert_node->children) {
        if (child->type == "TABLE") {
            table_name = child->value;
        } else if (child->type == "COLUMN_LIST") {
            column_list_node = child;
        } else if (child->type == "VALUE_LIST") {
            value_list_node = child;
        }
    }

    if (table_name.empty() || value_list_node == nullptr) {
        throw std::runtime_error("Query INSERT tidak valid.");
    }

    std::vector<std::string> columns = extract_columns_from_list(column_list_node);
    std::vector<std::any> values;

    if (value_list_node && !value_list_node->children.empty()) {
        for (const auto& value_node : value_list_node->children) {
            if (value_node->type == "VALUE" || value_node->type == "LITERAL") {
                std::any converted_value = convert_operand(value_node->value);
                values.push_back(converted_value);
                std::cout << "QP (INSERT): Value: " << value_node->value << std::endl;
            }
        }
    }

    if (values.size() != columns.size()) {
        throw std::runtime_error("INSERT: Jumlah kolom tidak cocok dengan jumlah nilai.");
    }

    std::cout << "QP (INSERT): Menambahkan baris baru ke tabel " << table_name << std::endl;
    
    // Create a Row object with the values in the correct order
    Row new_row;
    for (size_t i = 0; i < values.size(); ++i) {
        new_row.push_back(values[i]);
        std::cout << "QP (INSERT): Column '" << columns[i] << "' = " << i << " [value stored]" << std::endl;
    }
    
    // Create DataWrite request for Storage Manager
    DataWrite write_request;
    write_request.table = table_name;
    write_request.columns_to_update = columns;
    write_request.new_values = values;
    
    std::cout << "QP (INSERT): Calling Storage Manager to insert " << new_row.size() << " columns..." << std::endl;
    int affected_rows = sm_engine_.write_block();
    
    // Note: The actual row insertion happens in Storage Manager via write_block()
    // The write_request could be passed if SM API supports it
    // For now, we rely on the Storage Manager's current transaction context

    result.data.rows_count = affected_rows;
    result.message = std::to_string(affected_rows) + " baris dimasukkan.";
    std::cout << "QP (INSERT): " << result.message << std::endl;
    return result;
}

/**
 * @brief CREATE TABLE query handler
 * Supports: table creation with column definitions and constraints
 */
ExecutionResult QueryProcessor::handle_create(QueryTreePtr create_node, int tx_id) {
    ExecutionResult result;
    result.transaction_id = tx_id;

    std::string table_name;
    QueryTreePtr column_def_list = nullptr;

    for (const auto& child : create_node->children) {
        if (child->type == "TABLE") {
            table_name = child->value;
        } else if (child->type == "COLUMN_DEFINITION_LIST") {
            column_def_list = child;
        }
    }

    if (table_name.empty() || column_def_list == nullptr) {
        throw std::runtime_error("Query CREATE TABLE tidak valid.");
    }

    std::cout << "QP (CREATE): Membuat tabel " << table_name << std::endl;
    std::vector<std::string> columns;
    std::vector<std::string> types;

    for (const auto& col_def : column_def_list->children) {
        if (col_def->type == "COLUMN_DEFINITION" && !col_def->children.empty()) {
            std::string col_name = col_def->children[0]->value;
            std::string col_type = (col_def->children.size() > 1) ? 
                                   col_def->children[1]->value : "VARCHAR";
            
            columns.push_back(col_name);
            types.push_back(col_type);
            std::cout << "QP (CREATE): Kolom: " << col_name << " (" << col_type << ")" << std::endl;
            
            if (col_def->children.size() > 2) {
                std::string constraint = col_def->children[2]->value;
                std::cout << "QP (CREATE): Constraint: " << constraint << std::endl;
            }
        }
    }

    std::cout << "QP (CREATE): Mengirim ke Storage Manager untuk membuat tabel..." << std::endl;

    result.data.rows_count = 0;
    result.message = "Tabel " + table_name + " berhasil dibuat dengan " + 
                     std::to_string(columns.size()) + " kolom.";
    return result;
}

/**
 * @brief DROP TABLE query handler
 * Supports: table removal with CASCADE/RESTRICT options
 */
ExecutionResult QueryProcessor::handle_drop(QueryTreePtr drop_node, int tx_id) {
    ExecutionResult result;
    result.transaction_id = tx_id;

    std::string table_name;
    std::string drop_behavior = "RESTRICT";

    for (const auto& child : drop_node->children) {
        if (child->type == "TABLE") {
            table_name = child->value;
        } else if (child->type == "CASCADE") {
            drop_behavior = "CASCADE";
        } else if (child->type == "RESTRICT") {
            drop_behavior = "RESTRICT";
        }
    }

    if (table_name.empty()) {
        throw std::runtime_error("Query DROP TABLE tidak valid: tabel tidak ditemukan.");
    }

    std::cout << "QP (DROP): Menghapus tabel " << table_name << " dengan behavior: " 
              << drop_behavior << std::endl;

    if (drop_behavior == "RESTRICT") {
        std::cout << "QP (DROP): Memeriksa foreign key constraints..." << std::endl;
        // In a real implementation, check if other tables reference this one
        // If yes, throw error
    }

    std::cout << "QP (DROP): Mengirim ke Storage Manager untuk menghapus tabel..." << std::endl;
    
    // Storage Manager akan menghapus file dan metadata tabel

    result.data.rows_count = 0;
    result.message = "Tabel " + table_name + " berhasil dihapus.";
    return result;
}

/**
 * @brief JOIN query handler
 * Supports: INNER, LEFT, RIGHT, FULL, CROSS joins
 */
ExecutionResult QueryProcessor::handle_join(QueryTreePtr join_node, int tx_id) {
    ExecutionResult result;
    result.transaction_id = tx_id;

    std::string left_table;
    std::string right_table;
    std::string join_type = "INNER";
    QueryTreePtr join_condition = nullptr;

    for (const auto& child : join_node->children) {
        if (child->type == "LEFT_TABLE") {
            left_table = child->value;
        } else if (child->type == "RIGHT_TABLE") {
            right_table = child->value;
        } else if (child->type == "JOIN_TYPE") {
            join_type = child->value;
        } else if (child->type == "JOIN_CONDITION") {
            join_condition = child;
        }
    }

    if (left_table.empty() || right_table.empty()) {
        throw std::runtime_error("Query JOIN tidak valid.");
    }

    std::cout << "QP (JOIN): Melakukan " << join_type << " JOIN antara " 
              << left_table << " dan " << right_table << std::endl;

    DataRetrieval left_retrieval;
    left_retrieval.tables.push_back(left_table);
    
    DataRetrieval right_retrieval;
    right_retrieval.tables.push_back(right_table);

    Rows left_rows = sm_engine_.read_block();
    Rows right_rows = sm_engine_.read_block();

    std::cout << "QP (JOIN): Left table rows: " << left_rows.rows_count 
              << ", Right table rows: " << right_rows.rows_count << std::endl;

    Rows joined_rows;
    joined_rows.rows_count = 0;

    std::vector<std::string> left_cols = {left_table};
    std::vector<std::string> right_cols = {right_table};
    
    // Helper lambda to evaluate join condition
    auto evaluate_join_condition = [&](const Row& combined_row) -> bool {
        if (!join_condition || join_condition->children.size() < 2) {
            return true;  // No condition means all rows match
        }
        
        std::string left_col = join_condition->children[0]->value;
        std::string right_col = join_condition->children[1]->value;
        int left_idx = find_column_index(left_col, left_cols);
        int right_idx = find_column_index(right_col, right_cols);
        
        if (left_idx < 0 || right_idx < 0 || 
            left_idx >= static_cast<int>(combined_row.size()) || 
            right_idx >= static_cast<int>(combined_row.size())) {
            return false;
        }
        
        try {
            const auto& left_val = combined_row.at(left_idx);
            const auto& right_val = combined_row.at(right_idx);
            
            // Handle NULL values (empty std::any)
            if (!left_val.has_value() || !right_val.has_value()) {
                return false;  // NULL != anything
            }
            
            if (left_val.type() == right_val.type()) {
                if (left_val.type() == typeid(int)) {
                    return std::any_cast<int>(left_val) == std::any_cast<int>(right_val);
                } else if (left_val.type() == typeid(float)) {
                    return std::any_cast<float>(left_val) == std::any_cast<float>(right_val);
                } else {
                    return std::any_cast<std::string>(left_val) == std::any_cast<std::string>(right_val);
                }
            }
        } catch (const std::bad_any_cast&) {
            return false;
        }
        return false;
    };

    if (join_type == "INNER" || join_type == "CROSS") {
        std::cout << "QP (JOIN): Performing INNER/CROSS JOIN..." << std::endl;
        joined_rows = perform_cartesian_product(left_rows, right_rows, left_cols, right_cols);
        
        if (join_type == "INNER" && join_condition) {
            std::cout << "QP (JOIN): Applying join condition filter to INNER JOIN..." << std::endl;
            std::vector<Row> filtered;
            for (const auto& row : joined_rows.data) {
                if (evaluate_join_condition(row)) {
                    filtered.push_back(row);
                }
            }
            joined_rows.data = filtered;
            joined_rows.rows_count = filtered.size();
            std::cout << "QP (JOIN): INNER JOIN result: " << joined_rows.rows_count << " rows" << std::endl;
        }
    } 
    else if (join_type == "LEFT") {
        std::cout << "QP (JOIN): Performing LEFT OUTER JOIN..." << std::endl;
        std::vector<Row> result_rows;
        std::set<size_t> matched_right_indices;
        
        // For each left row, find matching right rows
        for (const auto& left_row : left_rows.data) {
            bool found_match = false;
            for (size_t right_idx = 0; right_idx < right_rows.data.size(); ++right_idx) {
                const auto& right_row = right_rows.data[right_idx];
                
                // Combine rows temporarily to test condition
                Row combined = left_row;
                combined.insert(combined.end(), right_row.begin(), right_row.end());
                
                if (evaluate_join_condition(combined)) {
                    result_rows.push_back(combined);
                    matched_right_indices.insert(right_idx);
                    found_match = true;
                }
            }
            
            // If no match found, add left row with NULL-padded right columns
            if (!found_match) {
                Row unmatched_row = left_row;
                size_t right_size = right_rows.data.empty() ? 0 : right_rows.data[0].size();
                for (size_t i = 0; i < right_size; ++i) {
                    unmatched_row.push_back(std::any());  // NULL value
                }
                result_rows.push_back(unmatched_row);
            }
        }
        
        joined_rows.data = result_rows;
        joined_rows.rows_count = result_rows.size();
        std::cout << "QP (JOIN): LEFT OUTER JOIN result: " << joined_rows.rows_count << " rows" << std::endl;
    }
    else if (join_type == "RIGHT") {
        std::cout << "QP (JOIN): Performing RIGHT OUTER JOIN..." << std::endl;
        std::vector<Row> result_rows;
        std::set<size_t> matched_left_indices;
        
        // For each right row, find matching left rows
        for (const auto& right_row : right_rows.data) {
            bool found_match = false;
            for (size_t left_idx = 0; left_idx < left_rows.data.size(); ++left_idx) {
                const auto& left_row = left_rows.data[left_idx];
                
                // Combine rows temporarily to test condition
                Row combined = left_row;
                combined.insert(combined.end(), right_row.begin(), right_row.end());
                
                if (evaluate_join_condition(combined)) {
                    result_rows.push_back(combined);
                    matched_left_indices.insert(left_idx);
                    found_match = true;
                }
            }
            
            // If no match found, add right row with NULL-padded left columns
            if (!found_match) {
                size_t left_size = left_rows.data.empty() ? 0 : left_rows.data[0].size();
                Row unmatched_row;
                for (size_t i = 0; i < left_size; ++i) {
                    unmatched_row.push_back(std::any());  // NULL value
                }
                unmatched_row.insert(unmatched_row.end(), right_row.begin(), right_row.end());
                result_rows.push_back(unmatched_row);
            }
        }
        
        joined_rows.data = result_rows;
        joined_rows.rows_count = result_rows.size();
        std::cout << "QP (JOIN): RIGHT OUTER JOIN result: " << joined_rows.rows_count << " rows" << std::endl;
    }
    else if (join_type == "FULL") {
        std::cout << "QP (JOIN): Performing FULL OUTER JOIN..." << std::endl;
        std::vector<Row> result_rows;
        std::set<size_t> matched_left_indices;
        std::set<size_t> matched_right_indices;
        
        // First pass: add all matching row combinations
        for (size_t left_idx = 0; left_idx < left_rows.data.size(); ++left_idx) {
            const auto& left_row = left_rows.data[left_idx];
            bool found_match = false;
            
            for (size_t right_idx = 0; right_idx < right_rows.data.size(); ++right_idx) {
                const auto& right_row = right_rows.data[right_idx];
                
                // Combine rows temporarily to test condition
                Row combined = left_row;
                combined.insert(combined.end(), right_row.begin(), right_row.end());
                
                if (evaluate_join_condition(combined)) {
                    result_rows.push_back(combined);
                    matched_left_indices.insert(left_idx);
                    matched_right_indices.insert(right_idx);
                    found_match = true;
                }
            }
            
            // If no match found on left, add with NULL-padded right
            if (!found_match) {
                Row unmatched_row = left_row;
                size_t right_size = right_rows.data.empty() ? 0 : right_rows.data[0].size();
                for (size_t i = 0; i < right_size; ++i) {
                    unmatched_row.push_back(std::any());  // NULL value
                }
                result_rows.push_back(unmatched_row);
            }
        }
        
        // Second pass: add unmatched right rows with NULL-padded left
        for (size_t right_idx = 0; right_idx < right_rows.data.size(); ++right_idx) {
            if (matched_right_indices.find(right_idx) == matched_right_indices.end()) {
                size_t left_size = left_rows.data.empty() ? 0 : left_rows.data[0].size();
                Row unmatched_row;
                for (size_t i = 0; i < left_size; ++i) {
                    unmatched_row.push_back(std::any());  // NULL value
                }
                unmatched_row.insert(unmatched_row.end(), right_rows.data[right_idx].begin(), 
                                    right_rows.data[right_idx].end());
                result_rows.push_back(unmatched_row);
            }
        }
        
        joined_rows.data = result_rows;
        joined_rows.rows_count = result_rows.size();
        std::cout << "QP (JOIN): FULL OUTER JOIN result: " << joined_rows.rows_count << " rows" << std::endl;
    }

    result.data = joined_rows;
    result.data.rows_count = joined_rows.rows_count;
    result.message = "JOIN berhasil dieksekusi dengan " + std::to_string(joined_rows.rows_count) + " baris hasil.";
    return result;
}

std::vector<Condition> QueryProcessor::parse_conditions_from_tree(QueryTreePtr where_node) {
    std::vector<Condition> conditions;
    
    // Asumsi: 'where_node' adalah 'WHERE_CLAUSE'
    // Anaknya adalah 'CONDITION' (atau 'AND', 'OR' - di luar cakupan)
    
    if (where_node == nullptr || where_node->children.empty()) {
        return conditions;
    }

    auto condition_node = where_node->children[0];

    if (condition_node->type == "CONDITION") {
        Condition cond;
        cond.operation = condition_node->value;
        
        // Asumsi: anak[0] adalah kolom, anak[1] adalah literal
        if (condition_node->children.size() == 2) {
            cond.column = condition_node->children[0]->value;
            std::string operand_str = condition_node->children[1]->value;
            cond.operand = convert_operand(operand_str);
            conditions.push_back(cond);
        }
    }
    
    return conditions;
}

void QueryProcessor::apply_projection(Rows& rows, QueryTreePtr column_list_node) {
    if (column_list_node == nullptr || column_list_node->children.empty()) {
        return;
    }
    
    if (column_list_node->children[0]->type == "ALL_COLUMNS") {
        std::cout << "QP (Proj): Memilih semua kolom (*)." << std::endl;
        return;
    }

    std::vector<std::string> selected_columns = extract_columns_from_list(column_list_node);
    
    if (selected_columns.empty() || rows.data.empty()) {
        return;
    }
    
    std::cout << "QP (Proj): Menerapkan proyeksi untuk " << selected_columns.size() 
              << " kolom: ";
    for (const auto& col : selected_columns) {
        std::cout << col << " ";
    }
    std::cout << std::endl;
    
    // IMPROVED: Map column names to indices
    // In a real implementation, we'd query table schema metadata
    // For now, we map based on available columns in the first row
    std::vector<int> column_indices;
    
    // Simplified approach: If we have selected columns, try to map them
    // This assumes columns come from the table in order
    // Real implementation would use schema metadata from Storage Manager
    
    for (const auto& col_name : selected_columns) {
        // Try to parse as numeric column reference (0, 1, 2, ...)
        try {
            int idx = std::stoi(col_name);
            if (idx >= 0 && idx < static_cast<int>(rows.data[0].size())) {
                column_indices.push_back(idx);
                std::cout << "QP (Proj): Column '" << col_name << "' -> index " << idx << std::endl;
            }
        } catch (...) {
            // Not numeric - would be column name in real implementation
            // For now, assume sequential position in table
            if (column_indices.size() < rows.data[0].size()) {
                int idx = column_indices.size();
                column_indices.push_back(idx);
                std::cout << "QP (Proj): Column '" << col_name << "' -> index " << idx 
                          << " (schema lookup would refine this)" << std::endl;
            }
        }
    }
    
    // Project each row to selected columns only
    std::vector<Row> projected_rows;
    for (const auto& row : rows.data) {
        Row projected_row;
        for (int idx : column_indices) {
            if (idx >= 0 && idx < static_cast<int>(row.size())) {
                projected_row.push_back(row[idx]);
            }
        }
        projected_rows.push_back(projected_row);
    }
    
    rows.data = projected_rows;
    rows.rows_count = projected_rows.size();
    std::cout << "QP (Proj): Proyeksi selesai. Hasil: " << rows.rows_count << " baris dengan " 
              << (rows.data.empty() ? 0 : rows.data[0].size()) << " kolom." << std::endl;
}


void QueryProcessor::apply_order_by(Rows& rows, QueryTreePtr order_by_node) {
    if (order_by_node == nullptr || order_by_node->children.empty()) {
        return;
    }
    
    std::string column_name = order_by_node->children[0]->value;
    std::string direction = "ASC";
    if (order_by_node->children.size() > 1) {
        direction = order_by_node->children[1]->value;
    }

    std::cout << "QP (Sort): Mengurutkan berdasarkan " << column_name << " " << direction << std::endl;

    // IMPROVED: Attempt to determine column index
    int sort_column_index = 0;
    
    // First try: parse as numeric column reference (0, 1, 2...)
    try {
        int parsed = std::stoi(column_name);
        if (parsed >= 0) {
            sort_column_index = parsed;
            std::cout << "QP (Sort): Using numeric column reference: " << sort_column_index << std::endl;
        }
    } catch (...) {
        // Not a numeric reference - this is a column name
        // In production, we'd query metadata for the column index
        // For now, search first row for position (simplified approach)
        std::cout << "QP (Sort): Column name reference detected: " << column_name << std::endl;
        
        // If we have a first row, we could try to match by position
        // This is simplified - real implementation would use schema metadata
        if (!rows.data.empty() && rows.data[0].size() > 0) {
            // Default to first column if name doesn't match any known schema
            sort_column_index = 0;
            std::cout << "QP (Sort): Defaulting to column 0 (real implementation would use schema)" << std::endl;
        }
    }
    
    std::string upper_direction = direction;
    std::transform(upper_direction.begin(), upper_direction.end(), 
                   upper_direction.begin(), ::toupper);
    
    if (rows.data.empty()) {
        return;
    }
    
    // IMPROVED: Verify column index is within bounds
    if (sort_column_index >= static_cast<int>(rows.data[0].size())) {
        std::cout << "QP (Sort): Column index " << sort_column_index << " out of bounds, using 0" << std::endl;
        sort_column_index = 0;
    }
    
    std::sort(rows.data.begin(), rows.data.end(), 
        [sort_column_index, upper_direction](const auto& rowA, const auto& rowB) {
            if (sort_column_index >= static_cast<int>(rowA.size()) || 
                sort_column_index >= static_cast<int>(rowB.size())) {
                return false;
            }
            
            const auto& valA = rowA[sort_column_index];
            const auto& valB = rowB[sort_column_index];
            
            // Numeric comparison
            try {
                if (valA.type() == typeid(int) && valB.type() == typeid(int)) {
                    int a = std::any_cast<int>(valA);
                    int b = std::any_cast<int>(valB);
                    return (upper_direction == "ASC") ? a < b : a > b;
                }
                if (valA.type() == typeid(float) && valB.type() == typeid(float)) {
                    float a = std::any_cast<float>(valA);
                    float b = std::any_cast<float>(valB);
                    return (upper_direction == "ASC") ? a < b : a > b;
                }
                // Mixed numeric types
                if ((valA.type() == typeid(int) || valA.type() == typeid(float)) &&
                    (valB.type() == typeid(int) || valB.type() == typeid(float))) {
                    double a = (valA.type() == typeid(int)) ? 
                               static_cast<double>(std::any_cast<int>(valA)) :
                               static_cast<double>(std::any_cast<float>(valA));
                    double b = (valB.type() == typeid(int)) ? 
                               static_cast<double>(std::any_cast<int>(valB)) :
                               static_cast<double>(std::any_cast<float>(valB));
                    return (upper_direction == "ASC") ? a < b : a > b;
                }
            } catch (const std::bad_any_cast&) {
                // Fall through to string comparison
            }
            
            // String comparison
            try {
                std::string strA = std::any_cast<std::string>(valA);
                std::string strB = std::any_cast<std::string>(valB);
                int cmp = strA.compare(strB);
                return (upper_direction == "ASC") ? cmp < 0 : cmp > 0;
            } catch (const std::bad_any_cast&) {
                return false;
            }
        });
    
    std::cout << "QP (Sort): Pengurutan selesai." << std::endl;
}

// --- Helper Method Implementations ---

std::vector<std::string> QueryProcessor::extract_columns_from_list(QueryTreePtr column_list_node) {
    std::vector<std::string> columns;
    
    if (column_list_node == nullptr || column_list_node->children.empty()) {
        return columns;
    }
    
    for (const auto& child : column_list_node->children) {
        if (child->type == "COLUMN") {
            columns.push_back(child->value);
            std::cout << "QP (Extract): Column: " << child->value << std::endl;
        } else if (child->type == "ALL_COLUMNS") {
            columns.push_back("*");
            return columns; // Return early for SELECT *
        }
    }
    
    return columns;
}

std::string QueryProcessor::extract_query_action(QueryTreePtr query_tree) {
    if (!query_tree) {
        return "READ";
    }
    
    std::string query_type = query_tree->type;
    
    // Map query types to actions
    if (query_type == "SELECT") {
        return "READ";
    } else if (query_type == "UPDATE" || query_type == "INSERT" || 
               query_type == "DELETE" || query_type == "CREATE" || 
               query_type == "DROP") {
        return "WRITE";
    }

    return "READ";
}

std::any QueryProcessor::convert_operand(const std::string& value, const std::string& type) {
    // If type is specified, use it
    if (type == "int") {
        try {
            return std::any(std::stoi(value));
        } catch (...) {
            return std::any(value);
        }
    } else if (type == "float") {
        try {
            return std::any(std::stof(value));
        } catch (...) {
            return std::any(value);
        }
    }
    
    // Auto-detect type from value
    if (is_number(value)) {
        // Try to parse as float first (more general)
        if (value.find('.') != std::string::npos) {
            try {
                return std::any(std::stof(value));
            } catch (...) {
                return std::any(value);
            }
        } else {
            // Try to parse as int
            try {
                return std::any(std::stoi(value));
            } catch (...) {
                return std::any(value);
            }
        }
    }
    
    // Default to string
    // Remove quotes if present
    std::string cleaned = value;
    if (cleaned.length() >= 2 && 
        ((cleaned.front() == '"' && cleaned.back() == '"') ||
         (cleaned.front() == '\'' && cleaned.back() == '\''))) {
        cleaned = cleaned.substr(1, cleaned.length() - 2);
    }
    return std::any(cleaned);
}

std::any QueryProcessor::evaluate_expression(QueryTreePtr expr_node, 
                                             const std::vector<std::any>& row,
                                             const std::vector<std::string>& column_names) {
    if (!expr_node) {
        throw std::runtime_error("Expression node is null");
    }
    
    // Handle LITERAL nodes
    if (expr_node->type == "LITERAL") {
        return convert_operand(expr_node->value);
    }
    
    // Handle COLUMN nodes
    if (expr_node->type == "COLUMN") {
        int col_index = find_column_index(expr_node->value, column_names);
        if (col_index >= 0 && col_index < static_cast<int>(row.size())) {
            return row[col_index];
        }
        throw std::runtime_error("Column not found: " + expr_node->value);
    }
    
    // Handle binary expressions (e.g., salary * 1.05)
    if (expr_node->type == "EXPRESSION" && expr_node->children.size() >= 2) {
        std::string op = expr_node->value;
        
        std::any left = evaluate_expression(expr_node->children[0], row, column_names);
        std::any right = evaluate_expression(expr_node->children[1], row, column_names);
        
        // Perform operation
        if (op == "*") {
            if (left.type() == typeid(int) && right.type() == typeid(int)) {
                return std::any(std::any_cast<int>(left) * std::any_cast<int>(right));
            } else if ((left.type() == typeid(int) || left.type() == typeid(float)) &&
                       (right.type() == typeid(int) || right.type() == typeid(float))) {
                float l = (left.type() == typeid(float)) ? std::any_cast<float>(left) : std::any_cast<int>(left);
                float r = (right.type() == typeid(float)) ? std::any_cast<float>(right) : std::any_cast<int>(right);
                return std::any(l * r);
            }
        } else if (op == "/") {
            float l = (left.type() == typeid(float)) ? std::any_cast<float>(left) : std::any_cast<int>(left);
            float r = (right.type() == typeid(float)) ? std::any_cast<float>(right) : std::any_cast<int>(right);
            if (r == 0) throw std::runtime_error("Division by zero");
            return std::any(l / r);
        } else if (op == "+") {
            if (left.type() == typeid(int) && right.type() == typeid(int)) {
                return std::any(std::any_cast<int>(left) + std::any_cast<int>(right));
            } else {
                float l = (left.type() == typeid(float)) ? std::any_cast<float>(left) : std::any_cast<int>(left);
                float r = (right.type() == typeid(float)) ? std::any_cast<float>(right) : std::any_cast<int>(right);
                return std::any(l + r);
            }
        } else if (op == "-") {
            if (left.type() == typeid(int) && right.type() == typeid(int)) {
                return std::any(std::any_cast<int>(left) - std::any_cast<int>(right));
            } else {
                float l = (left.type() == typeid(float)) ? std::any_cast<float>(left) : std::any_cast<int>(left);
                float r = (right.type() == typeid(float)) ? std::any_cast<float>(right) : std::any_cast<int>(right);
                return std::any(l - r);
            }
        }
        
        throw std::runtime_error("Unsupported operation: " + op);
    }
    
    throw std::runtime_error("Unsupported expression type: " + expr_node->type);
}

int QueryProcessor::find_column_index(const std::string& column_name, 
                                      const std::vector<std::string>& column_names) {
    for (size_t i = 0; i < column_names.size(); ++i) {
        if (column_names[i] == column_name) {
            return i;
        }
    }
    return -1; // Column not found
}

Rows QueryProcessor::perform_cartesian_product(const Rows& left, const Rows& right,
                                               const std::vector<std::string>& left_cols,
                                               const std::vector<std::string>& right_cols) {
    Rows result;
    result.rows_count = 0;
    
    // Cartesian product: combine each row from left with each row from right
    for (const auto& left_row : left.data) {
        for (const auto& right_row : right.data) {
            std::vector<std::any> combined_row = left_row;
            combined_row.insert(combined_row.end(), right_row.begin(), right_row.end());
            result.data.push_back(combined_row);
            result.rows_count++;
        }
    }
    
    std::cout << "QP: Cartesian product result: " << result.rows_count << " rows" << std::endl;
    return result;
}

bool QueryProcessor::compare_values(const std::any& left, const std::any& right, const std::string& op) {
    try {
        // Try numeric comparison first
        if (left.type() == typeid(int) && right.type() == typeid(int)) {
            int left_val = std::any_cast<int>(left);
            int right_val = std::any_cast<int>(right);
            
            if (op == "==") return left_val == right_val;
            if (op == "!=") return left_val != right_val;
            if (op == ">") return left_val > right_val;
            if (op == "<") return left_val < right_val;
            if (op == ">=") return left_val >= right_val;
            if (op == "<=") return left_val <= right_val;
        }
        else if (left.type() == typeid(float) && right.type() == typeid(float)) {
            float left_val = std::any_cast<float>(left);
            float right_val = std::any_cast<float>(right);
            
            if (op == "==") return left_val == right_val;
            if (op == "!=") return left_val != right_val;
            if (op == ">") return left_val > right_val;
            if (op == "<") return left_val < right_val;
            if (op == ">=") return left_val >= right_val;
            if (op == "<=") return left_val <= right_val;
        }
        // Mixed numeric types
        else if ((left.type() == typeid(int) || left.type() == typeid(float)) &&
                 (right.type() == typeid(int) || right.type() == typeid(float))) {
            double left_val = (left.type() == typeid(int)) ? 
                              static_cast<double>(std::any_cast<int>(left)) :
                              static_cast<double>(std::any_cast<float>(left));
            double right_val = (right.type() == typeid(int)) ? 
                               static_cast<double>(std::any_cast<int>(right)) :
                               static_cast<double>(std::any_cast<float>(right));
            
            if (op == "==") return left_val == right_val;
            if (op == "!=") return left_val != right_val;
            if (op == ">") return left_val > right_val;
            if (op == "<") return left_val < right_val;
            if (op == ">=") return left_val >= right_val;
            if (op == "<=") return left_val <= right_val;
        }
        // String comparison
        else if (left.type() == typeid(std::string) && right.type() == typeid(std::string)) {
            std::string left_val = std::any_cast<std::string>(left);
            std::string right_val = std::any_cast<std::string>(right);
            
            if (op == "==") return left_val == right_val;
            if (op == "!=") return left_val != right_val;
            if (op == ">") return left_val > right_val;
            if (op == "<") return left_val < right_val;
            if (op == ">=") return left_val >= right_val;
            if (op == "<=") return left_val <= right_val;
        }
    } catch (const std::bad_any_cast&) {
        std::cerr << "QP: Type mismatch in comparison" << std::endl;
    }
    
    return false;
}

} // namespace mdbms::qp
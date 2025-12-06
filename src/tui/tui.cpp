#include "tui.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <map>

namespace mdbms::tui {

TUI::TUI() {}

void TUI::run() {
    std::cout << "\n========================================\n";
    std::cout << "                 mDBMS\n";
    std::cout << "========================================\n";
    std::cout << "Enter SQL commands (type 'QUIT' or 'EXIT' to exit)\n";
    std::cout << "Type 'HELP' for available commands\n\n";

    mdbms::qp::QueryProcessor query_processor;

    while (true) {
        std::cout << "mDBMS> ";
        
        std::string query;
        std::getline(std::cin, query);
        query = trim(query);

        if (query.empty()) {
            continue;
        }

        // Convert to uppercase for command checking
        std::string upper_query = query;
        std::transform(upper_query.begin(), upper_query.end(), upper_query.begin(), ::toupper);

        if (upper_query == "QUIT" || upper_query == "EXIT") {
            std::cout << "\nExiting...\n";
            break;
        } else if (upper_query == "HELP") {
            std::cout << "\nAvailable SQL Commands:\n";
            std::cout << "  SELECT * FROM table_name [WHERE conditions];\n";
            std::cout << "  INSERT INTO table_name VALUES (value1, value2, ...);\n";
            std::cout << "  UPDATE table_name SET column=value [WHERE conditions];\n";
            std::cout << "  DELETE FROM table_name [WHERE conditions];\n";
            std::cout << "  CREATE INDEX index_name ON table_name(column) [HASH|BTREE];\n";
            std::cout << "  BEGIN TRANSACTION;\n";
            std::cout << "  COMMIT;\n";
            std::cout << "  ROLLBACK;\n";
            std::cout << "  QUIT or EXIT - Exit the program\n\n";
            continue;
        }

        handle_sql_command(query, query_processor);
    }
}

void TUI::handle_sql_command(const std::string& query, mdbms::qp::QueryProcessor& query_processor) {
    try {
        ExecutionResult result = query_processor.execute_query(query);
        display_execution_result(result);
    } catch (const std::exception& e) {
        std::cout << "\n[ERROR] " << e.what() << "\n\n";
    }
}

void TUI::display_execution_result(const ExecutionResult& result) {
    std::cout << "\n";
    
    if (!result.message.empty()) {
        if (result.success) {
            std::cout << "[SUCCESS] " << result.message << "\n";
        } else {
            std::cout << "[ERROR] " << result.message << "\n";
        }
    }

    if (result.transaction_id != -1) {
        std::cout << "Transaction ID: " << result.transaction_id << "\n";
    }

    // Display data if available
    if (result.data.rows_count > 0) {
        std::cout << "\n";
        display_results(result.data);
        std::cout << "\nTotal rows: " << result.data.rows_count << "\n";
    } else if (result.affected_rows > 0) {
        std::cout << "Affected rows: " << result.affected_rows << "\n";
    }

    std::cout << "\n";
}

void TUI::display_results(const Rows<Row>& rows) {
    if (rows.data.empty()) {
        std::cout << "No rows found.\n";
        return;
    }

    std::vector<std::string> all_columns;
    if (!rows.column_names.empty()) {
        all_columns = rows.column_names;
    } else {
        std::map<std::string, bool> column_seen;
        for (const auto& row : rows.data) {
            for (const auto& pair : row.columns) {
                if (!column_seen[pair.first]) {
                    all_columns.push_back(pair.first);
                    column_seen[pair.first] = true;
                }
            }
        }
    }

    if (all_columns.empty()) {
        std::cout << "No columns to display.\n";
        return;
    }

    // Calculate column widths
    std::map<std::string, int> column_widths;
    for (const auto& col : all_columns) {
        column_widths[col] = col.length();
    }

    for (const auto& row : rows.data) {
        for (const auto& col : all_columns) {
            if (row.columns.count(col)) {
                std::string value_str;
                const auto& value = row.columns.at(col);
                
                if (value.type() == typeid(int)) {
                    value_str = std::to_string(std::any_cast<int>(value));
                } else if (value.type() == typeid(float)) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(2) << std::any_cast<float>(value);
                    value_str = oss.str();
                } else if (value.type() == typeid(std::string)) {
                    value_str = std::any_cast<std::string>(value);
                } else {
                    value_str = "[unknown]";
                }
                
                if (value_str.length() > static_cast<size_t>(column_widths[col])) {
                    column_widths[col] = value_str.length();
                }
            }
        }
    }

    // Print header
    int total_width = 1;
    for (const auto& col : all_columns) {
        total_width += column_widths[col] + 3;
    }
    
    print_separator(total_width);
    std::cout << "|";
    for (const auto& col : all_columns) {
        std::cout << " " << std::setw(column_widths[col]) << std::left << col << " |";
    }
    std::cout << "\n";
    print_separator(total_width);

    // Print rows
    for (const auto& row : rows.data) {
        std::cout << "|";
        for (const auto& col : all_columns) {
            std::string value_str = "NULL";
            if (row.columns.count(col)) {
                const auto& value = row.columns.at(col);
                
                if (value.type() == typeid(int)) {
                    value_str = std::to_string(std::any_cast<int>(value));
                } else if (value.type() == typeid(float)) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(2) << std::any_cast<float>(value);
                    value_str = oss.str();
                } else if (value.type() == typeid(std::string)) {
                    value_str = std::any_cast<std::string>(value);
                }
            }
            std::cout << " " << std::setw(column_widths[col]) << std::left << value_str << " |";
        }
        std::cout << "\n";
    }
    print_separator(total_width);
}

void TUI::print_separator(int width) {
    std::cout << "+";
    for (int i = 0; i < width - 2; i++) {
        std::cout << "-";
    }
    std::cout << "+\n";
}

std::string TUI::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

} // namespace mdbms::tui

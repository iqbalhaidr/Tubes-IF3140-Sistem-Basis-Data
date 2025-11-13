#include "query_optimizer.h"

#include <iostream>
#include <iomanip>

// Di build dulu:
// 1.) Ubuntu / WSL:
//   cmake -S . -B build && cmake --build build --target test_query_optimizer
// 2.) macOS (Makefiles/Ninja):
//   cmake -S . -B build && cmake --build build --target test_query_optimizer
// 3.) macOS (Xcode generator):
//   cmake -S . -B build -GXcode && cmake --build build --config Debug --target test_query_optimizer


// Setelah build: ./build/src/test_query_optimizer

void print_tree(const mdbms::qo::QueryTree* node, const std::string& prefix = "", bool is_last = true, int level = 0) {
    if (!node) return;
    
    std::cout << prefix;
    if (level > 0) {
        std::cout << (is_last ? "└── " : "├── ");
    }
    
    std::cout << node->type;
    
    if (!node->value.empty()) {
        if (node->type == "PROJECT") {
            std::cout << " [Columns: " << node->value << "]";
        } else if (node->type == "SELECT") {
            std::cout << " [Condition: " << node->value << "]";
        } else if (node->type == "JOIN") {
            std::cout << " [ON: " << node->value << "]";
        } else if (node->type == "SCAN") {
            std::cout << " [Table: " << node->value << "]";
        } else {
            std::cout << " [" << node->value << "]";
        }
    }
    
    std::cout << "\n";
    
    std::string new_prefix = prefix;
    if (level > 0) {
        new_prefix += (is_last ? "    " : "│   ");
    }
    
    for (size_t i = 0; i < node->children.size(); ++i) {
        bool child_is_last = (i == node->children.size() - 1);
        print_tree(node->children[i], new_prefix, child_is_last, level + 1);
    }
}
int main() {
    mdbms::qo::OptimizationEngine opt;

    const std::string query =
        "SELECT s.name, d.nama FROM student s JOIN dept d ON s.dept_id = d.id JOIN apt a ON a_id = s.dept_id WHERE s.age > 20 AND d.size >= 10 AND a.name = 'bebek' s.name";

    std::cout << "Original Query:\n";
    std::cout << "  " << query << "\n\n";

    mdbms::qo::ParsedQuery parsed_query = opt.parse_query(query);
    std::cout << "SELECT columns: ";
    for (const auto& column : parsed_query.select_columns) {
        std::cout << column << ' ';
    }
    std::cout << "\n\nFROM tables: ";
    for (const auto& table : parsed_query.from_tables) {
        std::cout << table << ' ';
    }
    std::cout << "\n\nJOIN pairs:\n";
    for (size_t i = 0; i < parsed_query.join_pairs.size(); ++i) {
        const auto& join = parsed_query.join_pairs[i];
        std::cout << "  Join " << (i+1) << ": " << join.first << " = " << join.second << "\n";
    }
    std::cout << "\nWHERE conditions:\n";
    for (size_t i = 0; i < parsed_query.where_conditions.size(); ++i) {
        const auto& condition = parsed_query.where_conditions[i];
        std::cout << "  Condition " << (i+1) << ": " << condition.column << ' ' << condition.operation << " [value]\n";
    }
    std::cout << "\n";
    if (!parsed_query.query_tree) {
        std::cout << "ERROR: Query tree is null\n";
    } else {
        print_tree(parsed_query.query_tree);
    }

    return 0;
}

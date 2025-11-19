#include "query_optimizer.h"

#include <iostream>
#include <iomanip>

// Cara test:
//   cmake -S . -B build
//   cmake --build build --target test_query_optimizer
//   ./build/src/test_query_optimizer

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
        "SELECT s.name, s.age, s.address, d.nama, d.size, a.name, a.type FROM student s JOIN dept d ON s.dept_id = d.id JOIN apt a ON a.id = s.apt_id WHERE s.age > 20 AND s.age < 60 AND d.size >= 10 AND d.location = 'Jakarta' AND a.name = 'bebek' AND a.active = 1;";

    std::cout << "Original Query:\n";
    std::cout << "  " << query << "\n\n";

    mdbms::qo::ParsedQuery parsed_query = opt.parse_query(query);
    std::cout << "SELECT columns: ";
    for (const auto& column : parsed_query.select_columns) {
        std::cout << column << ';';
    }
    std::cout << "\n\nFROM tables: ";
    for (const auto& table : parsed_query.from_tables) {
        std::cout << table << ';';
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
        std::cout << "Initial Query Tree:\n";
        print_tree(parsed_query.query_tree);
        std::cout << "\n";
        
        mdbms::qo::ParsedQuery optimized_query = opt.optimize_query(parsed_query);  
        if (!optimized_query.query_tree) {
            std::cout << "ERROR: Optimized query tree is null\n";
        } else {
            std::cout << "Optimized Query Tree:\n";
            print_tree(optimized_query.query_tree);
        }
    }

    return 0;
}

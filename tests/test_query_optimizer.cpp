#include "query_optimizer.h"

#include <iostream>

// Di build dulu:
// 1.) Ubuntu / WSL:
//   cmake -S . -B build && cmake --build build --target test_query_optimizer
// 2.) macOS (Makefiles/Ninja):
//   cmake -S . -B build && cmake --build build --target test_query_optimizer
// 3.) macOS (Xcode generator):
//   cmake -S . -B build -GXcode && cmake --build build --config Debug --target test_query_optimizer


// Setelah build: ./build/src/test_query_optimizer

int main() {
    mdbms::qo::OptimizationEngine opt;

    const std::string query =
        "SELECT s.name, d.nama FROM student s JOIN dept d ON s.dept_id = d.id JOIN apt a ON a_id = s.dept_id WHERE s.age > 20 AND d.size >= 10 AND a.name = 'bebek' s.name";

    std::cout << "query awal:\n" << query << "\n\n";

    mdbms::qo::ParsedQuery parsed_query = opt.parse_query(query);

    std::cout << "SELECT columns: ";
    for (const auto& column : parsed_query.select_columns) {
        std::cout << column << ' ';
    }
    std::cout << "\nFROM tables: ";
    for (const auto& table : parsed_query.from_tables) {
        std::cout << table << ' ';
    }
    std::cout << "\nJOIN pairs: ";
    for (const auto& join : parsed_query.join_pairs) {
        std::cout << '(' << join.first << "," << join.second << ") ";
    }
    std::cout << "\nWHERE clauses: ";
    for (const auto& condition : parsed_query.where_conditions) {
        std::cout << condition.column << ' ' << condition.operation << ' ';
    }
    std::cout << "\n\n";


    // ini di implementasikann
    std::cout << "bentukan QueryTree:\n";
    if (!parsed_query.query_tree) { // null
        std::cout << "query tree masi null\n";
    }

    return 0;
}

#include "query_optimizer.h"

#include <iostream>
#include <iomanip>

// yg ditambahin buat cost estimation
#include <unordered_map>
#include <cctype>

namespace mdbms::sm {
StorageEngine::StorageEngine() = default;
StorageEngine::StorageEngine(const std::string& /*data_dir*/) {}
std::optional<Statistic> StorageEngine::get_stat(const std::string& table) const {
    auto to_lower = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    };

    const std::string key = to_lower(table);
    const std::unordered_map<std::string, Statistic> kStats = {
        {"student",
         [] {
             Statistic s;
             s.table_name = "student";
             s.n_r = 1000;
             s.b_r = 20;
             s.f_r = 50;
             s.V_a_r = {{"id", 1000}, {"dept_id", 10}, {"apt_id", 50}, {"age", 60}};
             return s;
         }()},
        {"dept",
         [] {
             Statistic s;
             s.table_name = "dept";
             s.n_r = 10;
             s.b_r = 2;
             s.f_r = 30;
             s.V_a_r = {{"id", 10}, {"size", 10}, {"location", 3}, {"nama", 10}};
             return s;
         }()},
        {"apt",
         [] {
             Statistic s;
             s.table_name = "apt";
             s.n_r = 50;
             s.b_r = 5;
             s.f_r = 40;
             s.V_a_r = {{"id", 50}, {"name", 50}, {"type", 5}, {"active", 2}};
             return s;
         }()},
    };

    auto it = kStats.find(key);
    if (it != kStats.end()) {
        return it->second;
    }
    return std::nullopt;
}
}  // namespace mdbms::sm

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
        // yg ditambahin buat cost estimation
        mdbms::sm::StorageEngine storage;
        const int initial_cost = mdbms::qo::estimate_cost(*parsed_query.query_tree, &storage);
        std::cout << "Estimated initial cost: " << initial_cost << "\n\n";
        
        mdbms::qo::ParsedQuery optimized_query = opt.optimize_query(parsed_query);  
        if (!optimized_query.query_tree) {
            std::cout << "ERROR: Optimized query tree is null\n";
        } else {
            std::cout << "Optimized Query Tree:\n";
            print_tree(optimized_query.query_tree);
            // yg ditambahin buat cost estimation
            const int optimized_cost = mdbms::qo::estimate_cost(*optimized_query.query_tree, &storage);
            std::cout << "Estimated optimized cost: " << optimized_cost << "\n";
        }
    }

    return 0;
}

#include "query_optimizer.h"

#include <any>
#include <cctype>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mdbms::sm {
StorageEngine::StorageEngine() = default;
StorageEngine::StorageEngine(const std::string& /*data_dir*/) {}

std::optional<Statistic> StorageEngine::build_dummy_get_stat(const std::string& table) const {
    auto to_lower = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
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

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

std::string to_upper_copy(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

const mdbms::qo::ColumnDefinition* find_column(const std::vector<mdbms::qo::ColumnDefinition>& columns,
                                               const std::string& name) {
    for (const auto& column : columns) {
        if (to_upper_copy(column.name) == to_upper_copy(name)) {
            return &column;
        }
    }
    return nullptr;
}

void print_tree(const mdbms::qo::QueryTree* node,
                const std::string& prefix = "",
                bool is_last = true,
                int level = 0) {
    if (!node) {
        return;
    }

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
            std::cout << " [Info: " << node->value << "]";
        } else if (node->type == "SCAN") {
            std::cout << " [Source: " << node->value << "]";
        } else {
            std::cout << " [" << node->value << "]";
        }
    }
    std::cout << '\n';

    std::string new_prefix = prefix;
    if (level > 0) {
        new_prefix += (is_last ? "    " : "│   ");
    }

    for (size_t i = 0; i < node->children.size(); ++i) {
        print_tree(node->children[i], new_prefix, i + 1 == node->children.size(), level + 1);
    }
}

void print_parsed_summary(const mdbms::qo::ParsedQuery& pq) {
    std::cout << "Query Type : " << pq.query_type << "\n";
    if (!pq.select_columns.empty()) {
        std::cout << "SELECT     : ";
        for (const auto& col : pq.select_columns) {
            std::cout << col << "; ";
        }
        std::cout << "\n";
    }
    if (!pq.table_references.empty()) {
        std::cout << "FROM       : ";
        for (const auto& ref : pq.table_references) {
            std::cout << ref.table_name;
            if (!ref.alias.empty()) {
                std::cout << " AS " << ref.alias;
            }
            std::cout << "; ";
        }
        std::cout << "\n";
    }
    if (!pq.join_clauses.empty()) {
        std::cout << "JOIN       :\n";
        for (const auto& clause : pq.join_clauses) {
            std::cout << "  - " << clause.join_type;
            if (clause.is_natural) {
                std::cout << " (NATURAL)";
            }
            if (!clause.left_expression.empty() && !clause.right_expression.empty()) {
                std::cout << " ON " << clause.left_expression << " = " << clause.right_expression;
            }
            std::cout << "\n";
        }
    }
    if (!pq.where_conditions.empty()) {
        std::cout << "WHERE      : ";
        for (const auto& cond : pq.where_conditions) {
            std::cout << cond.column << " " << cond.operation;
            if (cond.operand.has_value()) {
                try {
                    if (cond.operand.type() == typeid(std::string)) {
                        std::cout << " " << std::any_cast<std::string>(cond.operand);
                    } else if (cond.operand.type() == typeid(int)) {
                        std::cout << " " << std::any_cast<int>(cond.operand);
                    } else if (cond.operand.type() == typeid(double)) {
                        std::cout << " " << std::any_cast<double>(cond.operand);
                    }
                } catch (...) {
                }
            }
            std::cout << "; ";
        }
        std::cout << "\n";
    }
    if (!pq.order_by_column.empty()) {
        std::cout << "ORDER BY   : " << pq.order_by_column
                  << (pq.order_ascending ? " ASC" : " DESC") << "\n";
    }
    if (pq.limit_value >= 0) {
        std::cout << "LIMIT      : " << pq.limit_value << "\n";
    }
    if (!pq.set_values.empty()) {
        std::cout << "SET        : ";
        for (const auto& [col, val] : pq.set_values) {
            std::cout << col << " = ";
            if (val.type() == typeid(int)) {
                std::cout << std::any_cast<int>(val);
            } else if (val.type() == typeid(double)) {
                std::cout << std::any_cast<double>(val);
            } else if (val.type() == typeid(std::string)) {
                std::cout << std::any_cast<std::string>(val);
            }
            std::cout << "; ";
        }
        std::cout << "\n";
    }
    if (!pq.insert_columns.empty()) {
        std::cout << "INSERT C   : ";
        for (const auto& col : pq.insert_columns) {
            std::cout << col << "; ";
        }
        std::cout << "\n";
    }
    if (!pq.insert_values.empty()) {
        std::cout << "INSERT V   : ";
        for (const auto& val : pq.insert_values) {
            if (val.type() == typeid(int)) {
                std::cout << std::any_cast<int>(val);
            } else if (val.type() == typeid(double)) {
                std::cout << std::any_cast<double>(val);
            } else if (val.type() == typeid(std::string)) {
                std::cout << std::any_cast<std::string>(val);
            }
            std::cout << "; ";
        }
        std::cout << "\n";
    }
}

struct QueryDebugCase {
    std::string name;
    std::string sql;
    bool inspect_plan = true;
};

void print_query_analysis(const QueryDebugCase& debug_case) {
    std::cout << "\n=== " << debug_case.name << " ===\n";
    std::cout << "Original Query:\n  " << debug_case.sql << "\n\n";

    mdbms::qo::OptimizationEngine opt;
    mdbms::qo::ParsedQuery parsed = opt.parse_query(debug_case.sql);
    print_parsed_summary(parsed);

    if (!debug_case.inspect_plan) {
        return;
    }

    if (!parsed.query_tree) {
        std::cout << "Parser did not produce a query tree.\n";
        return;
    }

    std::cout << "\nInitial Query Tree:\n";
    print_tree(parsed.query_tree);

    mdbms::sm::StorageEngine storage;
    const int initial_cost = mdbms::qo::estimate_cost(*parsed.query_tree, &storage);
    std::cout << "Initial cost estimate: " << initial_cost << "\n\n";

    mdbms::qo::ParsedQuery optimized = opt.optimize_query(parsed);
    if (!optimized.query_tree) {
        std::cout << "Optimizer did not produce a query tree.\n";
        return;
    }

    std::cout << "Optimized Query Tree:\n";
    print_tree(optimized.query_tree);
    const int optimized_cost = mdbms::qo::estimate_cost(*optimized.query_tree, &storage);
    std::cout << "Optimized cost estimate: " << optimized_cost << "\n";
}

bool test_select_parsing() {
    std::cout << "\n[TEST] SELECT parsing with JOIN, NATURAL, ORDER BY, LIMIT\n";
    mdbms::qo::OptimizationEngine opt;
    const std::string query =
        "SELECT s.name, d.nama, a.type FROM student AS s INNER JOIN dept d ON s.dept_id = d.id "
        "NATURAL JOIN apt a WHERE s.age > 20 AND d.size >= 10 ORDER BY s.name DESC LIMIT 5;";

    mdbms::qo::ParsedQuery parsed = opt.parse_query(query);
    if (!expect(parsed.query_type == "SELECT", "Query type should be SELECT")) return false;
    if (!expect(parsed.select_columns.size() == 3, "SELECT list should contain 3 items")) return false;
    if (!expect(parsed.table_references.size() == 3, "FROM clause should have 3 table references")) return false;
    if (!expect(parsed.table_references[0].alias == "s", "First table alias should be 's'")) return false;
    if (!expect(parsed.table_references[1].alias == "d", "Second table alias should be 'd'")) return false;
    if (!expect(parsed.join_clauses.size() == 2, "Should detect 2 join clauses")) return false;
    if (!expect(parsed.join_clauses[0].join_type == "INNER", "First join should be INNER")) return false;
    if (!expect(parsed.join_clauses[1].is_natural, "Second join should be NATURAL")) return false;
    if (!expect(parsed.join_pairs.size() == 2, "Join pairs should align with join clauses")) return false;
    if (!expect(parsed.join_pairs[0].first == "s.dept_id", "Join condition should capture left column")) return false;
    if (!expect(parsed.join_pairs[0].second == "d.id", "Join condition should capture right column")) return false;
    if (!expect(parsed.where_conditions.size() == 2, "Should parse two WHERE predicates")) return false;
    if (!expect(parsed.where_conditions[0].operation == ">", "First condition should be '>'")) return false;
    if (!expect(std::any_cast<int>(parsed.where_conditions[0].operand) == 20, "First condition value should be 20")) return false;
    if (!expect(parsed.where_conditions[1].operation == ">=", "Second condition should be '>='")) return false;
    if (!expect(std::any_cast<int>(parsed.where_conditions[1].operand) == 10, "Second condition value should be 10")) return false;
    if (!expect(parsed.order_by_column == "s.name", "ORDER BY column should be s.name")) return false;
    if (!expect(!parsed.order_ascending, "ORDER BY should be DESC")) return false;
    if (!expect(parsed.limit_value == 5, "LIMIT should be 5")) return false;
    if (!expect(parsed.table_aliases.count("s") == 1 && parsed.table_aliases.at("s") == "student",
                "Alias map should store student alias")) return false;

    std::cout << "[PASS] SELECT parsing test\n";
    return true;
}

bool test_update_parsing() {
    std::cout << "\n[TEST] UPDATE parsing\n";
    mdbms::qo::OptimizationEngine opt;
    const std::string query =
        "UPDATE employee SET salary = 1000, name = 'Grace' WHERE id = 7;";

    mdbms::qo::ParsedQuery parsed = opt.parse_query(query);
    if (!expect(parsed.query_type == "UPDATE", "Query type should be UPDATE")) return false;
    if (!expect(parsed.target_table == "employee", "Target table should be employee")) return false;
    if (!expect(parsed.set_values.size() == 2, "Should capture two SET assignments")) return false;
    if (!expect(std::any_cast<int>(parsed.set_values["salary"]) == 1000, "Salary assignment should be integer 1000")) return false;
    if (!expect(std::any_cast<std::string>(parsed.set_values["name"]) == "Grace", "Name assignment should be Grace")) return false;
    if (!expect(parsed.where_conditions.size() == 1, "UPDATE should capture WHERE condition")) return false;
    if (!expect(parsed.where_conditions[0].column == "id", "WHERE column should be id")) return false;
    if (!expect(parsed.where_conditions[0].operation == "=", "WHERE operation should be '='")) return false;
    if (!expect(std::any_cast<int>(parsed.where_conditions[0].operand) == 7, "WHERE value should be 7")) return false;

    std::cout << "[PASS] UPDATE parsing test\n";
    return true;
}

bool test_insert_parsing() {
    std::cout << "\n[TEST] INSERT parsing\n";
    mdbms::qo::OptimizationEngine opt;
    const std::string query =
        "INSERT INTO employee (id, name, salary) VALUES (1, \"Alice\", 1200.5);";

    mdbms::qo::ParsedQuery parsed = opt.parse_query(query);
    if (!expect(parsed.query_type == "INSERT", "Query type should be INSERT")) return false;
    if (!expect(parsed.target_table == "employee", "Target table should be employee")) return false;
    if (!expect(parsed.insert_columns.size() == 3, "Should capture three column names")) return false;
    if (!expect(parsed.insert_values.size() == 3, "Should capture three VALUES entries")) return false;
    if (!expect(parsed.insert_columns[1] == "name", "Second column should be name")) return false;
    if (!expect(std::any_cast<int>(parsed.insert_values[0]) == 1, "First value should be integer 1")) return false;
    if (!expect(std::any_cast<std::string>(parsed.insert_values[1]) == "Alice", "Second value should be Alice")) return false;
    if (!expect(std::abs(std::any_cast<double>(parsed.insert_values[2]) - 1200.5) < 1e-6,
                "Third value should be 1200.5")) return false;

    std::cout << "[PASS] INSERT parsing test\n";
    return true;
}

bool test_delete_parsing() {
    std::cout << "\n[TEST] DELETE parsing\n";
    mdbms::qo::OptimizationEngine opt;
    const std::string query = "DELETE FROM employee WHERE department = \"RnD\";";

    mdbms::qo::ParsedQuery parsed = opt.parse_query(query);
    if (!expect(parsed.query_type == "DELETE", "Query type should be DELETE")) return false;
    if (!expect(parsed.target_table == "employee", "Target table should be employee")) return false;
    if (!expect(parsed.where_conditions.size() == 1, "DELETE should capture WHERE clause")) return false;
    if (!expect(parsed.where_conditions[0].column == "department", "DELETE condition should be on department")) return false;
    if (!expect(parsed.where_conditions[0].operation == "=", "DELETE condition should use '='")) return false;
    if (!expect(std::any_cast<std::string>(parsed.where_conditions[0].operand) == "RnD",
                "DELETE condition value should be RnD")) return false;

    std::cout << "[PASS] DELETE parsing test\n";
    return true;
}

bool test_transaction_control_parsing() {
    std::cout << "\n[TEST] Transaction control parsing\n";
    mdbms::qo::OptimizationEngine opt;
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"BEGIN TRANSACTION;", "BEGIN"},
        {"COMMIT;", "COMMIT"},
        {"ROLLBACK;", "ROLLBACK"},
    };

    for (const auto& [sql, expected_type] : cases) {
        mdbms::qo::ParsedQuery parsed = opt.parse_query(sql);
        if (!expect(parsed.query_type == expected_type, "Transaction query type should be " + expected_type)) {
            return false;
        }
        if (!expect(parsed.query_tree != nullptr, "Transaction query tree should be created")) {
            return false;
        }
        if (!expect(parsed.query_tree->type == expected_type, "Query tree type should match transaction type")) {
            return false;
        }
        if (!expect(parsed.table_references.empty(), "Transaction statements should have no table references")) {
            return false;
        }
    }

    std::cout << "[PASS] Transaction control parsing test\n";
    return true;
}

bool test_create_table_parsing() {
    std::cout << "\n[TEST] CREATE TABLE parsing\n";
    mdbms::qo::OptimizationEngine opt;
    const std::string query =
        "CREATE TABLE employee ("
        "id INTEGER PRIMARY KEY, "
        "dept_id INTEGER, "
        "salary FLOAT, "
        "FOREIGN KEY (dept_id) REFERENCES dept(id)"
        ");";

    mdbms::qo::ParsedQuery parsed = opt.parse_query(query);
    if (!expect(parsed.query_type == "CREATE", "Query type should be CREATE")) return false;
    if (!expect(parsed.target_table == "employee", "CREATE should capture table name")) return false;
    if (!expect(parsed.column_definitions.size() == 3, "Should capture 3 column definitions")) return false;

    const auto* id_column = find_column(parsed.column_definitions, "id");
    const auto* dept_column = find_column(parsed.column_definitions, "dept_id");
    if (!expect(id_column != nullptr, "ID column definition should exist")) return false;
    if (!expect(dept_column != nullptr, "dept_id column definition should exist")) return false;
    if (!expect(id_column->is_primary_key, "ID should be primary key")) return false;
    if (!expect(!id_column->is_foreign_key, "ID should not be foreign key")) return false;
    if (!expect(dept_column->is_foreign_key, "dept_id should be foreign key")) return false;
    if (!expect(dept_column->references_table == "dept", "dept_id should reference dept table")) return false;
    if (!expect(dept_column->references_column == "id", "dept_id should reference dept.id")) return false;

    std::cout << "[PASS] CREATE TABLE parsing test\n";
    return true;
}

bool test_drop_table_parsing() {
    std::cout << "\n[TEST] DROP TABLE parsing\n";
    mdbms::qo::OptimizationEngine opt;
    const std::string query = "DROP TABLE obsolete_table;";

    mdbms::qo::ParsedQuery parsed = opt.parse_query(query);
    if (!expect(parsed.query_type == "DROP", "Query type should be DROP")) return false;
    if (!expect(parsed.target_table == "obsolete_table", "DROP should capture table name")) return false;

    std::cout << "[PASS] DROP TABLE parsing test\n";
    return true;
}

bool test_query_tree_and_cost() {
    std::cout << "\n[TEST] Query tree construction and cost estimation\n";
    mdbms::qo::OptimizationEngine opt;
    const std::string query =
        "SELECT s.name, d.nama FROM student s JOIN dept d ON s.dept_id = d.id "
        "WHERE s.age > 20 AND d.size >= 5;";

    mdbms::qo::ParsedQuery parsed = opt.parse_query(query);
    if (!expect(parsed.query_tree != nullptr, "Parsed query tree should not be null")) return false;

    mdbms::qo::ParsedQuery optimized = opt.optimize_query(parsed);
    if (!expect(optimized.query_tree != nullptr, "Optimized query tree should not be null")) return false;

    mdbms::sm::StorageEngine storage;
    const int initial_cost = mdbms::qo::estimate_cost(*parsed.query_tree, &storage);
    const int optimized_cost = mdbms::qo::estimate_cost(*optimized.query_tree, &storage);
    if (!expect(initial_cost >= 0, "Initial cost should be computed")) return false;
    if (!expect(optimized_cost >= 0, "Optimized cost should be computed")) return false;

    std::cout << "[PASS] Query tree and cost estimation test\n";
    return true;
}

void print_debug_info() {
    std::cout << "\n=== Debug Parser/Tree/Cost Output ===\n";
    const std::vector<QueryDebugCase> debug_cases = {
        {"Single table filter + ORDER/LIMIT",
         "SELECT name, age FROM student WHERE age >= 21 ORDER BY age DESC LIMIT 5;"},
        {"Join with multi predicate",
         "SELECT s.name, d.nama FROM student s INNER JOIN dept d ON s.dept_id = d.id "
         "WHERE s.age > 20 AND d.size >= 5 ORDER BY s.name;"},
        {"Natural join student housing",
         "SELECT s.name, a.type FROM student s NATURAL JOIN apt a WHERE a.active = 1 LIMIT 10;"},
        {"Double join campus overview",
         "SELECT s.name, d.nama, a.name FROM student s INNER JOIN dept d ON s.dept_id = d.id "
         "INNER JOIN apt a ON s.apt_id = a.id WHERE s.age > 20 AND a.active = 1;"},
        {"Explicit AS aliases",
         "SELECT s.name AS student_name, d.nama AS dept_name "
         "FROM student AS s INNER JOIN dept AS d ON s.dept_id = d.id "
         "WHERE d.location = 'north';"},
        {"Update payroll",
         "UPDATE employee SET salary = salary + 100 WHERE dept_id = 2;",
         false},
        {"Insert audit trail",
         "INSERT INTO audit_log (id, action, success) VALUES (101, 'LOGIN', 1);",
         false},
        {"Delete inactive apt",
         "DELETE FROM apt WHERE active = 0;",
         false},
        {"Create temp lab table",
         "CREATE TABLE lab (id INT PRIMARY KEY, name VARCHAR(50));",
         false},
        {"Drop legacy table",
         "DROP TABLE legacy_student;",
         false},
        {"Begin transaction",
         "BEGIN TRANSACTION;",
         false},
        {"Commit transaction",
         "COMMIT;",
         false},
        {"Rollback transaction",
         "ROLLBACK;",
         false},
    };

    for (const auto& debug_case : debug_cases) {
        print_query_analysis(debug_case);
    }
}

}  // namespace


// Cara test:
//   cmake -S . -B build
//   cmake --build build --target test_query_optimizer
//   ./build/src/test_query_optimizer

int main() {
    std::vector<std::pair<std::string, bool (*)()>> tests = {
        {"SELECT parsing", test_select_parsing},
        {"UPDATE parsing", test_update_parsing},
        {"INSERT parsing", test_insert_parsing},
        {"DELETE parsing", test_delete_parsing},
        {"Transaction control parsing", test_transaction_control_parsing},
        {"CREATE TABLE parsing", test_create_table_parsing},
        {"DROP TABLE parsing", test_drop_table_parsing},
        {"Query tree && cost", test_query_tree_and_cost},
    };

    int passed = 0;
    for (const auto& [name, fn] : tests) {
        bool ok = fn();
        if (ok) {
            passed++;
        }
    }

    std::cout << "\n============================\n";
    std::cout << "Query Optimizer Tests: " << passed << " / " << tests.size() << " passed\n";
    std::cout << "============================\n";

    print_debug_info();

    return (passed == static_cast<int>(tests.size())) ? 0 : 1;
}

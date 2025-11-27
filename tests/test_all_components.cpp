#include "query_optimizer.h"
#include "query_processor.h"
#include "storage_manager.h"
#include "concurrency_control.h"
#include "failure_recovery.h"

#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

class ScopedDataDir {
public:
    explicit ScopedDataDir(const std::string& name) {
        fs::path artifacts_root = fs::current_path() / "test_artifacts";
        std::error_code ec;
        fs::create_directories(artifacts_root, ec);

        path_ = artifacts_root / name;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }

    ~ScopedDataDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    std::string str() const {
        return path_.string();
    }

private:
    fs::path path_;
};

void print_section(const std::string& title) {
    std::cout << "\n========== " << title << " ==========" << std::endl;
}

void insert_student(mdbms::sm::StorageEngine& storage,
                    int id,
                    const std::string& name,
                    float gpa) {
    mdbms::DataWrite<mdbms::Row> insert;
    insert.table = "Student";
    insert.is_insert = true;
    insert.new_value.table_name = "Student";
    insert.new_value.row_id = id;
    insert.new_value.columns = {
        {"StudentID", id},
        {"FullName", name},
        {"GPA", gpa}
    };

    storage.write_block(insert);
}

void print_rows(const mdbms::Rows<mdbms::Row>& rows, const std::string& title) {
    std::cout << title << " (" << rows.rows_count << " rows)" << std::endl;
    for (const auto& row : rows.data) {
        std::cout << "  { ";
        for (const auto& [col, value] : row.columns) {
            std::cout << col << ": ";
            if (value.type() == typeid(int)) {
                std::cout << std::any_cast<int>(value);
            } else if (value.type() == typeid(float)) {
                std::cout << std::any_cast<float>(value);
            } else if (value.type() == typeid(double)) {
                std::cout << std::any_cast<double>(value);
            } else if (value.type() == typeid(std::string)) {
                std::cout << std::any_cast<std::string>(value);
            } else {
                std::cout << "<unknown>";
            }
            std::cout << ", ";
        }
        std::cout << "}" << std::endl;
    }
}

void print_query_tree(const mdbms::qo::QueryTree* node,
                      const std::string& prefix = "",
                      bool is_last = true) {
    if (!node) {
        return;
    }

    std::cout << prefix;
    if (!prefix.empty()) {
        std::cout << (is_last ? "└── " : "├── ");
    }
    std::cout << node->type;
    if (!node->value.empty()) {
        std::cout << " [" << node->value << "]";
    }
    std::cout << std::endl;

    for (size_t i = 0; i < node->children.size(); ++i) {
        const bool child_last = (i == node->children.size() - 1);
        const std::string child_prefix = prefix + (is_last ? "    " : "│   ");
        print_query_tree(node->children[i], child_prefix, child_last);
    }
}

bool test_storage_manager_round_trip() {
    ScopedDataDir dir("sm_round_trip");
    mdbms::sm::StorageEngine storage(dir.str());

    insert_student(storage, 101, "Alice", 3.75f);
    insert_student(storage, 102, "Bob", 3.20f);

    mdbms::DataRetrieval all_rows;
    all_rows.table = "Student";
    all_rows.columns = {"StudentID", "FullName", "GPA"};

    print_section("Storage Manager - Initial SELECT");
    auto rows = storage.read_block(all_rows);
    print_rows(rows, "StorageManager SELECT Student");
    if (rows.rows_count != 2) {
        std::cerr << "Expected 2 rows, got " << rows.rows_count << std::endl;
        return false;
    }

    bool saw_alice = false;
    bool saw_bob = false;
    for (const auto& row : rows.data) {
        int id = std::any_cast<int>(row.columns.at("StudentID"));
        std::string name = std::any_cast<std::string>(row.columns.at("FullName"));
        if (id == 101 && name == "Alice") {
            saw_alice = true;
        } else if (id == 102 && name == "Bob") {
            saw_bob = true;
        }
    }
    if (!saw_alice || !saw_bob) {
        std::cerr << "Failed to read back inserted rows\n";
        return false;
    }

    mdbms::DataWrite<mdbms::Row> update;
    update.table = "Student";
    update.conditions = {mdbms::Condition("StudentID", "=", 102)};
    update.columns = {"GPA"};
    update.new_value.columns["GPA"] = 3.40f;

    if (storage.write_block(update) != 1) {
        std::cerr << "Update did not affect expected number of rows\n";
        return false;
    }

    mdbms::DataRetrieval query_bob;
    query_bob.table = "Student";
    query_bob.columns = {"GPA"};
    query_bob.conditions = {mdbms::Condition("StudentID", "=", 102)};
    print_section("Storage Manager - After UPDATE");
    auto bob_row = storage.read_block(query_bob);
    print_rows(bob_row, "StorageManager SELECT Bob -> updated GPA");
    if (bob_row.rows_count != 1) {
        std::cerr << "Expected to reread one row for Bob\n";
        return false;
    }

    float new_gpa = std::any_cast<float>(bob_row.data.front().columns.at("GPA"));
    return new_gpa == 3.40f;
}

bool test_query_optimizer_pipeline() {
    mdbms::qo::OptimizationEngine optimizer;
    const std::string query =
        "SELECT StudentID, FullName FROM Student WHERE GPA >= 3.5 AND StudentID > 10";

    print_section("Query Optimizer - Parsing");
    auto parsed = optimizer.parse_query(query);
    std::cout << "Optimizer parsed columns: ";
    for (const auto& col : parsed.select_columns) {
        std::cout << col << " ";
    }
    std::cout << "\nOptimizer parsed WHERE count: " << parsed.where_conditions.size() << std::endl;
    if (parsed.query_type != "SELECT") {
        std::cerr << "Query type detection failed\n";
        return false;
    }
    if (parsed.select_columns.size() != 2 || parsed.where_conditions.size() != 2) {
        std::cerr << "Parser did not capture select list or conditions correctly\n";
        return false;
    }
    if (!parsed.query_tree) {
        std::cerr << "Logical plan tree was not generated\n";
        return false;
    }

    print_section("Query Optimizer - Logical Plan");
    print_query_tree(parsed.query_tree);

    print_section("Query Optimizer - Optimized Plan");
    auto optimized = optimizer.optimize_query(parsed);
    if (!optimized.query_tree) {
        std::cerr << "Optimizer failed to return a plan\n";
        return false;
    }

    if (optimized.query_tree->type.empty()) {
        std::cerr << "Optimizer returned an unnamed root node\n";
        return false;
    }
    std::cout << "Optimizer root node type: " << optimized.query_tree->type << std::endl;
    print_query_tree(optimized.query_tree);

    bool saw_select = false;
    std::function<void(const mdbms::qo::QueryTree*)> dfs =
        [&](const mdbms::qo::QueryTree* node) {
            if (!node) {
                return;
            }
            if (node->type == "SELECT") {
                saw_select = true;
            }
            for (const auto* child : node->children) {
                dfs(child);
            }
        };
    dfs(optimized.query_tree);

    if (!saw_select) {
        std::cerr << "Optimizer plan missed selection nodes\n";
        return false;
    }
    return true;
}

bool test_query_processor_select_flow() {
    ScopedDataDir dir("qp_component");
    auto optimizer = std::make_shared<mdbms::qo::OptimizationEngine>();
    auto storage = std::make_shared<mdbms::sm::StorageEngine>(dir.str());

    mdbms::qp::QueryProcessor processor(optimizer, storage, nullptr, nullptr);

    insert_student(*storage, 201, "Celine", 3.90f);
    insert_student(*storage, 202, "Darren", 3.10f);
    insert_student(*storage, 203, "Eve", 3.70f);

    print_section("Query Processor - SELECT *");
    auto result = processor.execute_query("SELECT * FROM Student");
    print_rows(result.data, "QueryProcessor SELECT * FROM Student");
    if (!result.success || result.data.rows_count != 3 || result.affected_rows != 3) {
        std::cerr << "QueryProcessor failed to return stored rows\n";
        return false;
    }

    // Exercise helper utilities (WHERE, ORDER BY, LIMIT)
    std::vector<mdbms::Condition> where = {mdbms::Condition("GPA", ">", 3.5f)};
    print_section("Query Processor - WHERE GPA > 3.5");
    auto filtered = processor.apply_where_clause(result.data, where);
    print_rows(filtered, "QueryProcessor WHERE GPA > 3.5");
    if (filtered.rows_count != 2) {
        std::cerr << "apply_where_clause returned " << filtered.rows_count << " rows\n";
        return false;
    }

    print_section("Query Processor - ORDER BY StudentID DESC");
    auto sorted = processor.apply_order_by(result.data, "StudentID", false);
    print_rows(sorted, "QueryProcessor ORDER BY StudentID DESC");
    if (sorted.rows_count != 3) {
        std::cerr << "apply_order_by unexpectedly changed row count\n";
        return false;
    }
    int highest_id = std::any_cast<int>(sorted.data.front().columns.at("StudentID"));
    if (highest_id != 203) {
        std::cerr << "apply_order_by did not sort descending by StudentID\n";
        return false;
    }

    print_section("Query Processor - LIMIT 2");
    auto limited = processor.apply_limit(sorted, 2);
    print_rows(limited, "QueryProcessor LIMIT 2");
    if (limited.rows_count != 2) {
        std::cerr << "apply_limit should have returned 2 rows\n";
        return false;
    }

    return true;
}

bool test_concurrency_control_timestamp() {
    print_section("Concurrency Control - Timestamp Protocol");
    auto& manager = mdbms::ccm::ConcurrencyControlManager::get_instance();
    manager.switch_algorithm("timestamp");

    int tid = manager.begin_transaction();
    if (tid <= 0) {
        std::cerr << "Failed to obtain transaction id\n";
        return false;
    }

    mdbms::Row row;
    row.table_name = "Student";
    row.row_id = 500;
    row.columns["StudentID"] = 500;

    manager.log_object(row, tid);
    auto read_resp = manager.validate_object(row, tid, mdbms::Action::READ);
    auto write_resp = manager.validate_object(row, tid, mdbms::Action::WRITE);
    manager.end_transaction(tid);

    std::cout << "Transaction ID: " << tid << std::endl;
    std::cout << "CCM READ allowed: " << (read_resp.allowed ? "yes" : "no")
              << ", WRITE allowed: " << (write_resp.allowed ? "yes" : "no") << std::endl;
    return read_resp.allowed && write_resp.allowed;
}

bool test_failure_recovery_logging() {
    print_section("Failure Recovery - WAL + UNDO");
    fs::path log_path = fs::current_path() / ".." / "data" / "wal.log";
    std::error_code ec;
    fs::create_directories(log_path.parent_path(), ec);
    fs::remove(log_path, ec);

    auto& frm = mdbms::fr::FailureRecoveryManager::get_instance();

    mdbms::ExecutionResult begin;
    begin.transaction_id = 900;
    begin.query = "BEGIN";
    begin.success = true;
    frm.write_log(begin);

    mdbms::ExecutionResult insert;
    insert.transaction_id = 900;
    insert.query = "INSERT INTO Student VALUES (900, 'Frank', 3.6)";
    insert.success = true;
    mdbms::Row inserted_row;
    inserted_row.table_name = "Student";
    inserted_row.row_id = 900;
    inserted_row.columns = {
        {"StudentID", 900},
        {"FullName", std::string("Frank")},
        {"GPA", 3.60f}
    };
    insert.data.data.push_back(inserted_row);
    insert.data.rows_count = 1;
    frm.write_log(insert);

    mdbms::ExecutionResult commit;
    commit.transaction_id = 900;
    commit.query = "COMMIT";
    commit.success = true;
    frm.write_log(commit);

    frm.save_checkpoint();

    mdbms::RecoverCriteria criteria;
    criteria.transaction_id = 900;
    criteria.use_timestamp = false;
    frm.recover(criteria);

    bool exists = fs::exists(log_path);
    std::cout << "Failure Recovery log file " << (exists ? "exists" : "missing")
              << " at " << log_path << std::endl;
    return exists;
}

}  // namespace

// cmake -S . -B build
// cd build && ctest -R test_all_components --output-on-failure


int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"Storage Manager round-trip", test_storage_manager_round_trip},
        {"Query Optimizer pipeline", test_query_optimizer_pipeline},
        {"Query Processor SELECT flow", test_query_processor_select_flow},
        {"Concurrency Control timestamp protocol", test_concurrency_control_timestamp},
        {"Failure Recovery logging/recover", test_failure_recovery_logging}
    };

    int passed = 0;
    for (const auto& [name, fn] : tests) {
        std::cout << "[ RUN      ] " << name << std::endl;
        bool ok = false;
        try {
            ok = fn();
        } catch (const std::exception& ex) {
            std::cerr << "  Exception: " << ex.what() << std::endl;
            ok = false;
        }

        if (ok) {
            std::cout << "[     PASS ] " << name << std::endl;
            passed++;
        } else {
            std::cout << "[  FAILED  ] " << name << std::endl;
        }
    }

    std::cout << "\nPassed " << passed << " of " << tests.size() << " component tests." << std::endl;
    return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
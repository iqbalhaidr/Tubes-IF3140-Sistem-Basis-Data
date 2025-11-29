#include "query_optimizer.h"
#include "query_processor.h"
#include "storage_manager.h"

#include <cassert>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

std::string any_to_string(const std::any& value) {
    if (!value.has_value()) {
        return "NULL";
    }

    if (value.type() == typeid(int)) {
        return std::to_string(std::any_cast<int>(value));
    }

    if (value.type() == typeid(float)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << std::any_cast<float>(value);
        return oss.str();
    }

    if (value.type() == typeid(std::string)) {
        return std::any_cast<std::string>(value);
    }

    if (value.type() == typeid(double)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << std::any_cast<double>(value);
        return oss.str();
    }

    return "NULL";
}

void print_rows(const mdbms::Rows<mdbms::Row>& rows) {
    if (rows.data.empty()) {
        std::cout << "Empty set (0 rows)" << std::endl;
        return;
    }

    std::vector<std::string> col_names;
    std::map<std::string, size_t> col_widths;

    for (const auto& [col_name, value] : rows.data[0].columns) {
        col_names.push_back(col_name);
        col_widths[col_name] = col_name.length();
    }

    for (const auto& row : rows.data) {
        for (const auto& col_name : col_names) {
            if (row.columns.count(col_name)) {
                size_t val_len = any_to_string(row.columns.at(col_name)).length();
                if (val_len > col_widths[col_name]) {
                    col_widths[col_name] = val_len;
                }
            }
        }
    }

    std::cout << "+";
    for (const auto& col_name : col_names) {
        std::cout << std::string(col_widths[col_name] + 2, '-') << "+";
    }
    std::cout << std::endl;

    std::cout << "|";
    for (const auto& col_name : col_names) {
        std::cout << " " << std::left << std::setw(col_widths[col_name]) << col_name << " |";
    }
    std::cout << std::endl;

    std::cout << "+";
    for (const auto& col_name : col_names) {
        std::cout << std::string(col_widths[col_name] + 2, '-') << "+";
    }
    std::cout << std::endl;

    for (const auto& row : rows.data) {
        std::cout << "|";
        for (const auto& col_name : col_names) {
            std::string val = row.columns.count(col_name) ? any_to_string(row.columns.at(col_name)) : "NULL";
            std::cout << " " << std::left << std::setw(col_widths[col_name]) << val << " |";
        }
        std::cout << std::endl;
    }

    std::cout << "+";
    for (const auto& col_name : col_names) {
        std::cout << std::string(col_widths[col_name] + 2, '-') << "+";
    }
    std::cout << std::endl;

    std::cout << rows.rows_count << " row(s) in set" << std::endl;
}

class TestSetup {
public:
    std::string test_dir;
    std::unique_ptr<mdbms::qp::QueryProcessor> qp;

    TestSetup(const std::string& dir) : test_dir(dir) {
        std::filesystem::create_directory(test_dir);
        // All components are singletons, QueryProcessor uses get_instance() for all
        qp = std::make_unique<mdbms::qp::QueryProcessor>();
    }

    ~TestSetup() {
        std::filesystem::remove_all(test_dir);
    }

    void insert_student(int id, const std::string& name, float gpa) {
        mdbms::DataWrite<mdbms::Row> insert;
        insert.table = "Student";
        insert.new_value.table_name = "Student";
        insert.new_value.columns = {
            {"StudentID", id},
            {"FullName", name},
            {"GPA", gpa}
        };
        // All components are singletons, use get_instance()
        mdbms::sm::StorageEngine::get_instance().write_block(insert);
    }

    void insert_course(int course_id, int year, const std::string& name) {
        mdbms::DataWrite<mdbms::Row> insert;
        insert.table = "Course";
        insert.new_value.table_name = "Course";
        insert.new_value.columns = {
            {"CourseID", course_id},
            {"Year", year},
            {"CourseName", name}
        };
        // All components are singletons, use get_instance()
        mdbms::sm::StorageEngine::get_instance().write_block(insert);
    }

    mdbms::Rows<mdbms::Row> read_all_students() {
        mdbms::DataRetrieval retrieval;
        retrieval.table = "Student";
        retrieval.columns = {"*"};
        // All components are singletons, use get_instance()
        return mdbms::sm::StorageEngine::get_instance().read_block(retrieval);
    }

    mdbms::Rows<mdbms::Row> read_all_courses() {
        mdbms::DataRetrieval retrieval;
        retrieval.table = "Course";
        retrieval.columns = {"*"};
        // All components are singletons, use get_instance()
        return mdbms::sm::StorageEngine::get_instance().read_block(retrieval);
    }
};

bool test_basic_integration() {
    std::cout << "\n=== TEST 1: Basic Integration ===" << std::endl;

    // All components are singletons, QueryProcessor uses get_instance() for all
    mdbms::qp::QueryProcessor query_processor;

    const std::string query = "SELECT * FROM integration_test";
    const auto result = query_processor.execute_query(query);

    if (!result.success) {
        std::cerr << "[FAIL] Query execution marked as failed\n";
        return false;
    }

    if (result.query != query) {
        std::cerr << "[FAIL] Result query mismatch\n";
        return false;
    }

    if (result.transaction_id < 0) {
        std::cerr << "[FAIL] Transaction ID not assigned\n";
        return false;
    }

    if (result.message.find("Retrieved") == std::string::npos) {
        std::cerr << "[FAIL] Unexpected execution message: " << result.message << "\n";
        return false;
    }

    std::cout << "[PASS] Basic integration test\n";
    return true;
}

bool test_begin_transaction() {
    std::cout << "\n=== TEST 2: Begin Transaction ===" << std::endl;

    // All components are singletons, QueryProcessor uses get_instance() for all
    mdbms::qp::QueryProcessor query_processor;

    // Test begin_transaction
    int txn_id = query_processor.begin_transaction();

    if (txn_id < 0) {
        std::cerr << "[FAIL] Failed to begin transaction, got ID: " << txn_id << "\n";
        return false;
    }

    std::cout << "Transaction started with ID: " << txn_id << std::endl;

    // Start another transaction to verify ID increment
    int txn_id2 = query_processor.begin_transaction();

    if (txn_id2 <= txn_id) {
        std::cerr << "[FAIL] Second transaction ID should be greater than first\n";
        return false;
    }

    std::cout << "Second transaction started with ID: " << txn_id2 << std::endl;

    std::cout << "[PASS] Begin Transaction test\n";
    return true;
}

bool test_limit() {
    std::cout << "\n=== TEST 3: LIMIT ===" << std::endl;

    TestSetup setup("qp_test_limit");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto all_rows = setup.read_all_students();

    std::cout << "Before LIMIT:" << std::endl;
    print_rows(all_rows);

    auto limited = setup.qp->apply_limit(all_rows, 5);

    std::cout << "\nAfter LIMIT 5:" << std::endl;
    print_rows(limited);

    if (limited.rows_count != 5) {
        std::cerr << "[FAIL] Expected 5 rows with LIMIT, got " << limited.rows_count << "\n";
        return false;
    }

    std::cout << "[PASS] SELECT with LIMIT test\n";
    return true;
}

bool test_order_by_string_asc() {
    std::cout << "\n=== TEST 4: ORDER BY String ASC ===" << std::endl;

    TestSetup setup("qp_test_orderby_str_asc");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "Before ORDER BY:" << std::endl;
    print_rows(rows);

    auto sorted = setup.qp->apply_order_by(rows, "FullName", true);

    std::cout << "\nAfter ORDER BY FullName ASC:" << std::endl;
    print_rows(sorted);

    std::string first = std::any_cast<std::string>(sorted.data[0].columns.at("FullName"));
    std::string last = std::any_cast<std::string>(sorted.data[9].columns.at("FullName"));

    if (first != "Alice" || last != "Ivy") {
        std::cerr << "[FAIL] Expected Alice first, Ivy last. Got " << first << " and " << last << "\n";
        return false;
    }

    std::cout << "[PASS] ORDER BY String ASC test\n";
    return true;
}

bool test_order_by_string_desc() {
    std::cout << "\n=== TEST 5: ORDER BY String DESC ===" << std::endl;

    TestSetup setup("qp_test_orderby_str_desc");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "Before ORDER BY:" << std::endl;
    print_rows(rows);

    auto sorted = setup.qp->apply_order_by(rows, "FullName", false);

    std::cout << "\nAfter ORDER BY FullName DESC:" << std::endl;
    print_rows(sorted);

    std::string first = std::any_cast<std::string>(sorted.data[0].columns.at("FullName"));
    std::string last = std::any_cast<std::string>(sorted.data[9].columns.at("FullName"));

    if (first != "Ivy" || last != "Alice") {
        std::cerr << "[FAIL] Expected Ivy first, Alice last. Got " << first << " and " << last << "\n";
        return false;
    }

    std::cout << "[PASS] ORDER BY String DESC test\n";
    return true;
}

bool test_order_by_int_asc() {
    std::cout << "\n=== TEST 6: ORDER BY Integer ASC ===" << std::endl;

    TestSetup setup("qp_test_orderby_int_asc");
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(10, "Alice", 3.1f);
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(9, "Ivy", 3.6f);

    auto rows = setup.read_all_students();

    std::cout << "Before ORDER BY:" << std::endl;
    print_rows(rows);

    auto sorted = setup.qp->apply_order_by(rows, "StudentID", true);

    std::cout << "\nAfter ORDER BY StudentID ASC:" << std::endl;
    print_rows(sorted);

    int first = std::any_cast<int>(sorted.data[0].columns.at("StudentID"));
    int last = std::any_cast<int>(sorted.data[9].columns.at("StudentID"));

    if (first != 1 || last != 10) {
        std::cerr << "[FAIL] Expected 1 first, 10 last. Got " << first << " and " << last << "\n";
        return false;
    }

    std::cout << "[PASS] ORDER BY Integer ASC test\n";
    return true;
}

bool test_order_by_int_desc() {
    std::cout << "\n=== TEST 7: ORDER BY Integer DESC ===" << std::endl;

    TestSetup setup("qp_test_orderby_int_desc");
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(10, "Alice", 3.1f);
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(9, "Ivy", 3.6f);

    auto rows = setup.read_all_students();

    std::cout << "Before ORDER BY:" << std::endl;
    print_rows(rows);

    auto sorted = setup.qp->apply_order_by(rows, "StudentID", false);

    std::cout << "\nAfter ORDER BY StudentID DESC:" << std::endl;
    print_rows(sorted);

    int first = std::any_cast<int>(sorted.data[0].columns.at("StudentID"));
    int last = std::any_cast<int>(sorted.data[9].columns.at("StudentID"));

    if (first != 10 || last != 1) {
        std::cerr << "[FAIL] Expected 10 first, 1 last. Got " << first << " and " << last << "\n";
        return false;
    }

    std::cout << "[PASS] ORDER BY Integer DESC test\n";
    return true;
}

bool test_order_by_float_asc() {
    std::cout << "\n=== TEST 8: ORDER BY Float ASC ===" << std::endl;

    TestSetup setup("qp_test_orderby_float_asc");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "Before ORDER BY:" << std::endl;
    print_rows(rows);

    auto sorted = setup.qp->apply_order_by(rows, "GPA", true);

    std::cout << "\nAfter ORDER BY GPA ASC:" << std::endl;
    print_rows(sorted);

    float first = std::any_cast<float>(sorted.data[0].columns.at("GPA"));
    float last = std::any_cast<float>(sorted.data[9].columns.at("GPA"));

    if (first > 2.6f || last < 3.75f) {
        std::cerr << "[FAIL] Expected 2.5 first, 3.8 last. Got " << first << " and " << last << "\n";
        return false;
    }

    std::cout << "[PASS] ORDER BY Float ASC test\n";
    return true;
}

bool test_order_by_float_desc() {
    std::cout << "\n=== TEST 9: ORDER BY Float DESC ===" << std::endl;

    TestSetup setup("qp_test_orderby_float_desc");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "Before ORDER BY:" << std::endl;
    print_rows(rows);

    auto sorted = setup.qp->apply_order_by(rows, "GPA", false);

    std::cout << "\nAfter ORDER BY GPA DESC:" << std::endl;
    print_rows(sorted);

    float first = std::any_cast<float>(sorted.data[0].columns.at("GPA"));
    float last = std::any_cast<float>(sorted.data[9].columns.at("GPA"));

    if (first < 3.75f || last > 2.6f) {
        std::cerr << "[FAIL] Expected 3.8 first, 2.5 last. Got " << first << " and " << last << "\n";
        return false;
    }

    std::cout << "[PASS] ORDER BY Float DESC test\n";
    return true;
}

bool test_where_integer_operators() {
    std::cout << "\n=== TEST 10: WHERE Integer Operators ===" << std::endl;

    TestSetup setup("qp_test_where_int");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "All Students:" << std::endl;
    print_rows(rows);
    std::cout << std::endl;

    // Test = operator
    std::vector<mdbms::Condition> cond_eq;
    cond_eq.push_back(mdbms::Condition("StudentID", "=", 5));
    auto result = setup.qp->apply_where_clause(rows, cond_eq);
    std::cout << "WHERE StudentID = 5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 1) {
        std::cerr << "[FAIL] = operator: Expected 1, got " << result.rows_count << "\n";
        return false;
    }

    // Test <> operator
    std::vector<mdbms::Condition> cond_ne;
    cond_ne.push_back(mdbms::Condition("StudentID", "<>", 5));
    result = setup.qp->apply_where_clause(rows, cond_ne);
    std::cout << "WHERE StudentID <> 5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 9) {
        std::cerr << "[FAIL] <> operator: Expected 9, got " << result.rows_count << "\n";
        return false;
    }

    // Test > operator
    std::vector<mdbms::Condition> cond_gt;
    cond_gt.push_back(mdbms::Condition("StudentID", ">", 5));
    result = setup.qp->apply_where_clause(rows, cond_gt);
    std::cout << "WHERE StudentID > 5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 5) {
        std::cerr << "[FAIL] > operator: Expected 5, got " << result.rows_count << "\n";
        return false;
    }

    // Test >= operator
    std::vector<mdbms::Condition> cond_ge;
    cond_ge.push_back(mdbms::Condition("StudentID", ">=", 5));
    result = setup.qp->apply_where_clause(rows, cond_ge);
    std::cout << "WHERE StudentID >= 5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 6) {
        std::cerr << "[FAIL] >= operator: Expected 6, got " << result.rows_count << "\n";
        return false;
    }

    // Test < operator
    std::vector<mdbms::Condition> cond_lt;
    cond_lt.push_back(mdbms::Condition("StudentID", "<", 5));
    result = setup.qp->apply_where_clause(rows, cond_lt);
    std::cout << "WHERE StudentID < 5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 4) {
        std::cerr << "[FAIL] < operator: Expected 4, got " << result.rows_count << "\n";
        return false;
    }

    // Test <= operator
    std::vector<mdbms::Condition> cond_le;
    cond_le.push_back(mdbms::Condition("StudentID", "<=", 5));
    result = setup.qp->apply_where_clause(rows, cond_le);
    std::cout << "WHERE StudentID <= 5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 5) {
        std::cerr << "[FAIL] <= operator: Expected 5, got " << result.rows_count << "\n";
        return false;
    }

    std::cout << "[PASS] WHERE Integer Operators test\n";
    return true;
}

bool test_where_float_operators() {
    std::cout << "\n=== TEST 11: WHERE Float Operators ===" << std::endl;

    TestSetup setup("qp_test_where_float");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "All Students:" << std::endl;
    print_rows(rows);
    std::cout << std::endl;

    // Test = operator
    std::vector<mdbms::Condition> cond_eq;
    cond_eq.push_back(mdbms::Condition("GPA", "=", 3.5f));
    auto result = setup.qp->apply_where_clause(rows, cond_eq);
    std::cout << "WHERE GPA = 3.5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 2) {  // Charlie, Diana
        std::cerr << "[FAIL] = operator: Expected 2, got " << result.rows_count << "\n";
        return false;
    }

    // Test <> operator
    std::vector<mdbms::Condition> cond_ne;
    cond_ne.push_back(mdbms::Condition("GPA", "<>", 3.5f));
    result = setup.qp->apply_where_clause(rows, cond_ne);
    std::cout << "WHERE GPA <> 3.5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 8) {
        std::cerr << "[FAIL] <> operator: Expected 8, got " << result.rows_count << "\n";
        return false;
    }

    // Test > operator
    std::vector<mdbms::Condition> cond_gt;
    cond_gt.push_back(mdbms::Condition("GPA", ">", 3.5f));
    result = setup.qp->apply_where_clause(rows, cond_gt);
    std::cout << "WHERE GPA > 3.5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 3) {  // Alice 3.8, Frank 3.7, Ivy 3.6
        std::cerr << "[FAIL] > operator: Expected 3, got " << result.rows_count << "\n";
        return false;
    }

    // Test >= operator
    std::vector<mdbms::Condition> cond_ge;
    cond_ge.push_back(mdbms::Condition("GPA", ">=", 3.5f));
    result = setup.qp->apply_where_clause(rows, cond_ge);
    std::cout << "WHERE GPA >= 3.5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 5) {  // Alice 3.8, Charlie 3.5, Diana 3.5, Frank 3.7, Ivy 3.6
        std::cerr << "[FAIL] >= operator: Expected 5, got " << result.rows_count << "\n";
        return false;
    }

    // Test < operator
    std::vector<mdbms::Condition> cond_lt;
    cond_lt.push_back(mdbms::Condition("GPA", "<", 3.5f));
    result = setup.qp->apply_where_clause(rows, cond_lt);
    std::cout << "WHERE GPA < 3.5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 5) {  // Bob 3.0, Alice 2.5, Grace 3.4, Henry 2.8, Alice 3.1
        std::cerr << "[FAIL] < operator: Expected 5, got " << result.rows_count << "\n";
        return false;
    }

    // Test <= operator
    std::vector<mdbms::Condition> cond_le;
    cond_le.push_back(mdbms::Condition("GPA", "<=", 3.5f));
    result = setup.qp->apply_where_clause(rows, cond_le);
    std::cout << "WHERE GPA <= 3.5:" << std::endl;
    print_rows(result);
    if (result.rows_count != 7) {  // Bob, Charlie, Diana, Alice, Grace, Henry, Alice
        std::cerr << "[FAIL] <= operator: Expected 7, got " << result.rows_count << "\n";
        return false;
    }

    std::cout << "[PASS] WHERE Float Operators test\n";
    return true;
}

bool test_where_string_operators() {
    std::cout << "\n=== TEST 12: WHERE String Operators ===" << std::endl;

    TestSetup setup("qp_test_where_str");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "All Students:" << std::endl;
    print_rows(rows);
    std::cout << std::endl;

    // Test = operator
    std::vector<mdbms::Condition> cond_eq;
    cond_eq.push_back(mdbms::Condition("FullName", "=", std::string("Alice")));
    auto result = setup.qp->apply_where_clause(rows, cond_eq);
    std::cout << "WHERE FullName = 'Alice':" << std::endl;
    print_rows(result);
    if (result.rows_count != 3) {
        std::cerr << "[FAIL] = operator: Expected 3, got " << result.rows_count << "\n";
        return false;
    }

    // Test <> operator
    std::vector<mdbms::Condition> cond_ne;
    cond_ne.push_back(mdbms::Condition("FullName", "<>", std::string("Alice")));
    result = setup.qp->apply_where_clause(rows, cond_ne);
    std::cout << "WHERE FullName <> 'Alice':" << std::endl;
    print_rows(result);
    if (result.rows_count != 7) {
        std::cerr << "[FAIL] <> operator: Expected 7, got " << result.rows_count << "\n";
        return false;
    }

    // Test > operator 
    std::vector<mdbms::Condition> cond_gt;
    cond_gt.push_back(mdbms::Condition("FullName", ">", std::string("Frank")));
    result = setup.qp->apply_where_clause(rows, cond_gt);
    std::cout << "WHERE FullName > 'Frank':" << std::endl;
    print_rows(result);
    if (result.rows_count != 3) {  // Grace, Henry, Ivy
        std::cerr << "[FAIL] > operator: Expected 3, got " << result.rows_count << "\n";
        return false;
    }

    // Test >= operator
    std::vector<mdbms::Condition> cond_ge;
    cond_ge.push_back(mdbms::Condition("FullName", ">=", std::string("Frank")));
    result = setup.qp->apply_where_clause(rows, cond_ge);
    std::cout << "WHERE FullName >= 'Frank':" << std::endl;
    print_rows(result);
    if (result.rows_count != 4) {  // Frank, Grace, Henry, Ivy
        std::cerr << "[FAIL] >= operator: Expected 4, got " << result.rows_count << "\n";
        return false;
    }

    // Test < operator
    std::vector<mdbms::Condition> cond_lt;
    cond_lt.push_back(mdbms::Condition("FullName", "<", std::string("Charlie")));
    result = setup.qp->apply_where_clause(rows, cond_lt);
    std::cout << "WHERE FullName < 'Charlie':" << std::endl;
    print_rows(result);
    if (result.rows_count != 4) {  // Alice x3, Bob
        std::cerr << "[FAIL] < operator: Expected 4, got " << result.rows_count << "\n";
        return false;
    }

    // Test <= operator
    std::vector<mdbms::Condition> cond_le;
    cond_le.push_back(mdbms::Condition("FullName", "<=", std::string("Charlie")));
    result = setup.qp->apply_where_clause(rows, cond_le);
    std::cout << "WHERE FullName <= 'Charlie':" << std::endl;
    print_rows(result);
    if (result.rows_count != 5) {  // Alice x3, Bob, Charlie
        std::cerr << "[FAIL] <= operator: Expected 5, got " << result.rows_count << "\n";
        return false;
    }

    std::cout << "[PASS] WHERE String Operators test\n";
    return true;
}

bool test_where_multiple_conditions() {
    std::cout << "\n=== TEST 13: WHERE Multiple Conditions ===" << std::endl;

    TestSetup setup("qp_test_where_multi");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "All Students:" << std::endl;
    print_rows(rows);
    std::cout << std::endl;

    // Test multiple conditions: GPA > 3.0 AND StudentID < 6
    std::vector<mdbms::Condition> conditions;
    conditions.push_back(mdbms::Condition("GPA", ">", 3.0f));
    conditions.push_back(mdbms::Condition("StudentID", "<", 6));

    auto result = setup.qp->apply_where_clause(rows, conditions);
    std::cout << "WHERE GPA > 3.0 AND StudentID < 6:" << std::endl;
    print_rows(result);

    // Should get Alice (3.8, ID=1), Charlie (3.5, ID=3), Diana (3.5, ID=4)
    if (result.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << result.rows_count << "\n";
        return false;
    }

    // Test three conditions
    conditions.clear();
    conditions.push_back(mdbms::Condition("GPA", ">=", 3.5f));
    conditions.push_back(mdbms::Condition("StudentID", ">", 3));
    conditions.push_back(mdbms::Condition("FullName", "<>", std::string("Diana")));

    result = setup.qp->apply_where_clause(rows, conditions);
    std::cout << "WHERE GPA >= 3.5 AND StudentID > 3 AND FullName <> 'Diana':" << std::endl;
    print_rows(result);

    // Should get Frank (3.7, ID=6), Ivy (3.6, ID=9)
    if (result.rows_count != 2) {
        std::cerr << "[FAIL] Expected 2 rows, got " << result.rows_count << "\n";
        return false;
    }

    std::cout << "[PASS] WHERE Multiple Conditions test\n";
    return true;
}

bool test_combined_where_orderby_limit() {
    std::cout << "\n=== TEST 14: Combined WHERE + ORDER BY + LIMIT ===" << std::endl;

    TestSetup setup("qp_test_combined");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    auto rows = setup.read_all_students();

    std::cout << "All Students:" << std::endl;
    print_rows(rows);
    std::cout << std::endl;

    // WHERE GPA > 3.0
    std::vector<mdbms::Condition> conditions;
    conditions.push_back(mdbms::Condition("GPA", ">", 3.0f));
    auto filtered = setup.qp->apply_where_clause(rows, conditions);

    // ORDER BY GPA DESC
    auto sorted = setup.qp->apply_order_by(filtered, "GPA", false);

    // LIMIT 5
    auto limited = setup.qp->apply_limit(sorted, 5);

    std::cout << "Final Result - WHERE GPA > 3.0 ORDER BY GPA DESC LIMIT 5:" << std::endl;
    print_rows(limited);

    if (limited.rows_count != 5) {
        std::cerr << "[FAIL] Expected 5 rows, got " << limited.rows_count << "\n";
        return false;
    }

    // Verify order: Alice (3.8), Frank (3.7), Ivy (3.6), Charlie (3.5), Diana (3.5)
    float first_gpa = std::any_cast<float>(limited.data[0].columns.at("GPA"));
    float fifth_gpa = std::any_cast<float>(limited.data[4].columns.at("GPA"));

    if (first_gpa < 3.75f || fifth_gpa > 3.55f) {
        std::cerr << "[FAIL] Order incorrect. First=" << first_gpa << ", Fifth=" << fifth_gpa << "\n";
        return false;
    }

    std::cout << "[PASS] Combined WHERE + ORDER BY + LIMIT test\n";
    return true;
}

bool test_cartesian_product() {
    std::cout << "\n=== TEST 15: Cartesian Product ===" << std::endl;

    TestSetup setup("qp_test_cartesian");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);

    setup.insert_course(101, 2023, "Database");
    setup.insert_course(102, 2024, "Algorithms");
    setup.insert_course(103, 2023, "Networks");
    setup.insert_course(104, 2024, "Operating Systems");

    auto students = setup.read_all_students();
    auto courses = setup.read_all_courses();

    std::cout << "Students:" << std::endl;
    print_rows(students);
    std::cout << "Courses:" << std::endl;
    print_rows(courses);

    // Empty condition for cartesian product
    mdbms::Condition empty_cond;
    auto result = setup.qp->execute_join(students, courses, empty_cond, "INNER");

    std::cout << "Cartesian Product (5 students x 4 courses = 20 rows):" << std::endl;
    print_rows(result);

    // 5 students * 4 courses = 20 rows
    if (result.rows_count != 20) {
        std::cerr << "[FAIL] Expected 20 rows, got " << result.rows_count << "\n";
        return false;
    }

    // Check that each row has columns from both tables
    bool has_student = result.data[0].columns.find("FullName") != result.data[0].columns.end();
    bool has_course = result.data[0].columns.find("CourseName") != result.data[0].columns.end();

    if (!has_student || !has_course) {
        std::cerr << "[FAIL] Cartesian product missing columns from one table\n";
        return false;
    }

    std::cout << "[PASS] Cartesian Product test\n";
    return true;
}

bool test_join_on() {
    std::cout << "\n=== TEST 16: JOIN ON ===" << std::endl;

    TestSetup setup("qp_test_join_on");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    setup.insert_course(101, 1, "Database");
    setup.insert_course(102, 2, "Algorithms");
    setup.insert_course(103, 5, "Networks");
    setup.insert_course(104, 7, "Operating Systems");
    setup.insert_course(105, 10, "Machine Learning");
    setup.insert_course(106, 15, "Distributed Systems"); 

    auto students = setup.read_all_students();
    auto courses = setup.read_all_courses();

    std::cout << "Students:" << std::endl;
    print_rows(students);
    std::cout << "Courses:" << std::endl;
    print_rows(courses);

    // JOIN ON StudentID = Year
    mdbms::Condition join_cond("StudentID", "=", std::string("Year"));
    auto result = setup.qp->execute_join(students, courses, join_cond, "INNER");

    std::cout << "JOIN ON StudentID = Year:" << std::endl;
    print_rows(result);

    // Should have 5 rows: Alice+DB(1), Bob+Algo(2), Alice+Networks(5), Grace+OS(7), Alice+ML(10)
    if (result.rows_count != 5) {
        std::cerr << "[FAIL] Expected 5 rows, got " << result.rows_count << "\n";
        return false;
    }

    bool found_alice_db = false;
    bool found_bob_algo = false;
    bool found_alice_networks = false;
    bool found_grace_os = false;
    bool found_alice_ml = false;
    for (const auto& row : result.data) {
        std::string name = std::any_cast<std::string>(row.columns.at("FullName"));
        std::string course = std::any_cast<std::string>(row.columns.at("CourseName"));
        if (name == "Alice" && course == "Database") found_alice_db = true;
        if (name == "Bob" && course == "Algorithms") found_bob_algo = true;
        if (name == "Alice" && course == "Networks") found_alice_networks = true;
        if (name == "Grace" && course == "Operating Systems") found_grace_os = true;
        if (name == "Alice" && course == "Machine Learning") found_alice_ml = true;
    }

    if (!found_alice_db || !found_bob_algo || !found_alice_networks || !found_grace_os || !found_alice_ml) {
        std::cerr << "[FAIL] Join results incorrect\n";
        return false;
    }

    std::cout << "[PASS] JOIN ON test\n";
    return true;
}

bool test_execute_select_basic() {
    std::cout << "\n=== TEST 17: Execute SELECT Basic ===" << std::endl;

    TestSetup setup("qp_test_exec_select_basic");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    // Create ParsedQuery for SELECT * FROM Student
    mdbms::qo::ParsedQuery select_query;
    select_query.query_type = "SELECT";
    select_query.select_columns = {"*"};
    select_query.from_tables.push_back("Student");

    int txn_id = setup.qp->begin_transaction();
    auto result = setup.qp->execute_select(select_query, txn_id);

    std::cout << "Result - SELECT * FROM Student:" << std::endl;
    print_rows(result);

    if (result.rows_count != 10) {
        std::cerr << "[FAIL] Expected 10 rows, got " << result.rows_count << "\n";
        return false;
    }

    std::cout << "[PASS] Execute SELECT Basic test\n";
    return true;
}

bool test_execute_select_with_where() {
    std::cout << "\n=== TEST 18: Execute SELECT with WHERE ===" << std::endl;

    TestSetup setup("qp_test_exec_select_where");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    // Create ParsedQuery for SELECT * FROM Student WHERE GPA > 3.5
    mdbms::qo::ParsedQuery select_query;
    select_query.query_type = "SELECT";
    select_query.select_columns = {"*"};
    select_query.from_tables.push_back("Student");
    select_query.where_conditions.push_back(mdbms::Condition("GPA", ">", 3.5f));

    int txn_id = setup.qp->begin_transaction();
    auto result = setup.qp->execute_select(select_query, txn_id);

    std::cout << "Result - SELECT * FROM Student WHERE GPA > 3.5:" << std::endl;
    print_rows(result);

    // Should get: Alice (3.8), Frank (3.7), Ivy (3.6) = 3 rows
    if (result.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << result.rows_count << "\n";
        return false;
    }

    std::cout << "[PASS] Execute SELECT with WHERE test\n";
    return true;
}

bool test_execute_select_with_order_by() {
    std::cout << "\n=== TEST 19: Execute SELECT with ORDER BY ===" << std::endl;

    TestSetup setup("qp_test_exec_select_orderby");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    // Create ParsedQuery for SELECT * FROM Student ORDER BY GPA DESC
    mdbms::qo::ParsedQuery select_query;
    select_query.query_type = "SELECT";
    select_query.select_columns = {"*"};
    select_query.from_tables.push_back("Student");
    select_query.order_by_column = "GPA";
    select_query.order_ascending = false;

    int txn_id = setup.qp->begin_transaction();
    auto result = setup.qp->execute_select(select_query, txn_id);

    std::cout << "Result - SELECT * FROM Student ORDER BY GPA DESC:" << std::endl;
    print_rows(result);

    if (result.rows_count != 10) {
        std::cerr << "[FAIL] Expected 10 rows, got " << result.rows_count << "\n";
        return false;
    }

    // Check first row has highest GPA (3.8)
    float first_gpa = std::any_cast<float>(result.data[0].columns.at("GPA"));
    if (first_gpa < 3.75f) {
        std::cerr << "[FAIL] First row should have highest GPA (3.8), got " << first_gpa << "\n";
        return false;
    }

    std::cout << "[PASS] Execute SELECT with ORDER BY test\n";
    return true;
}

bool test_execute_select_with_limit() {
    std::cout << "\n=== TEST 20: Execute SELECT with LIMIT ===" << std::endl;

    TestSetup setup("qp_test_exec_select_limit");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    // Create ParsedQuery for SELECT * FROM Student LIMIT 3
    mdbms::qo::ParsedQuery select_query;
    select_query.query_type = "SELECT";
    select_query.select_columns = {"*"};
    select_query.from_tables.push_back("Student");
    select_query.limit_value = 3;

    int txn_id = setup.qp->begin_transaction();
    auto result = setup.qp->execute_select(select_query, txn_id);

    std::cout << "Result - SELECT * FROM Student LIMIT 3:" << std::endl;
    print_rows(result);

    if (result.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << result.rows_count << "\n";
        return false;
    }

    std::cout << "[PASS] Execute SELECT with LIMIT test\n";
    return true;
}

bool test_execute_select_combined() {
    std::cout << "\n=== TEST 21: Execute SELECT Combined (WHERE + ORDER BY + LIMIT) ===" << std::endl;

    TestSetup setup("qp_test_exec_select_combined");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    // Create ParsedQuery for SELECT * FROM Student WHERE GPA > 3.0 ORDER BY GPA DESC LIMIT 5
    mdbms::qo::ParsedQuery select_query;
    select_query.query_type = "SELECT";
    select_query.select_columns = {"*"};
    select_query.from_tables.push_back("Student");
    select_query.where_conditions.push_back(mdbms::Condition("GPA", ">", 3.0f));
    select_query.order_by_column = "GPA";
    select_query.order_ascending = false;
    select_query.limit_value = 5;

    int txn_id = setup.qp->begin_transaction();
    auto result = setup.qp->execute_select(select_query, txn_id);

    std::cout << "Result - SELECT * FROM Student WHERE GPA > 3.0 ORDER BY GPA DESC LIMIT 5:" << std::endl;
    print_rows(result);

    if (result.rows_count != 5) {
        std::cerr << "[FAIL] Expected 5 rows, got " << result.rows_count << "\n";
        return false;
    }

    // Check first row has highest GPA (3.8)
    float first_gpa = std::any_cast<float>(result.data[0].columns.at("GPA"));
    float last_gpa = std::any_cast<float>(result.data[4].columns.at("GPA"));

    if (first_gpa < 3.75f) {
        std::cerr << "[FAIL] First row should have GPA 3.8, got " << first_gpa << "\n";
        return false;
    }

    if (first_gpa < last_gpa) {
        std::cerr << "[FAIL] Results not sorted DESC. First=" << first_gpa << ", Last=" << last_gpa << "\n";
        return false;
    }

    std::cout << "[PASS] Execute SELECT Combined test\n";
    return true;
}

bool test_update_single_column() {
    std::cout << "\n=== TEST 22: UPDATE Single Column ===" << std::endl;

    TestSetup setup("qp_test_update_single");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    std::cout << "Before UPDATE:" << std::endl;
    auto before = setup.read_all_students();
    print_rows(before);

    mdbms::qo::ParsedQuery update_query;
    update_query.query_type = "UPDATE";
    update_query.from_tables.push_back("Student");
    update_query.set_values["GPA"] = 4.0f;
    update_query.where_conditions.push_back(mdbms::Condition("StudentID", "=", 1));

    int txn_id = setup.qp->begin_transaction();
    int affected = setup.qp->execute_update(update_query, txn_id);

    std::cout << "UPDATE Student SET GPA = 4.0 WHERE StudentID = 1" << std::endl;
    std::cout << "Affected rows: " << affected << std::endl;

    if (affected != 1) {
        std::cerr << "[FAIL] Expected 1 affected row, got " << affected << "\n";
        return false;
    }

    std::cout << "After UPDATE:" << std::endl;
    auto after = setup.read_all_students();
    print_rows(after);

    bool found_updated = false;
    for (const auto& row : after.data) {
        int id = std::any_cast<int>(row.columns.at("StudentID"));
        if (id == 1) {
            float gpa = std::any_cast<float>(row.columns.at("GPA"));
            if (gpa == 4.0f) {
                found_updated = true;
            } else {
                std::cerr << "[FAIL] GPA should be 4.0, got " << gpa << "\n";
                return false;
            }
        }
    }

    if (!found_updated) {
        std::cerr << "[FAIL] Could not find updated row\n";
        return false;
    }

    std::cout << "[PASS] UPDATE Single Column test\n";
    return true;
}

bool test_update_multiple_rows() {
    std::cout << "\n=== TEST 23: UPDATE Multiple Rows ===" << std::endl;

    TestSetup setup("qp_test_update_multi");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    std::cout << "Before UPDATE:" << std::endl;
    auto before = setup.read_all_students();
    print_rows(before);

    // Should update: Alice (3.8), Frank (3.7), Ivy (3.6) = 3 rows
    mdbms::qo::ParsedQuery update_query;
    update_query.query_type = "UPDATE";
    update_query.from_tables.push_back("Student");
    update_query.set_values["GPA"] = 3.9f;
    update_query.where_conditions.push_back(mdbms::Condition("GPA", ">", 3.5f));

    int txn_id = setup.qp->begin_transaction();
    int affected = setup.qp->execute_update(update_query, txn_id);

    std::cout << "UPDATE Student SET GPA = 3.9 WHERE GPA > 3.5" << std::endl;
    std::cout << "Affected rows: " << affected << std::endl;

    if (affected != 3) {
        std::cerr << "[FAIL] Expected 3 affected rows, got " << affected << "\n";
        return false;
    }

    std::cout << "After UPDATE:" << std::endl;
    auto after = setup.read_all_students();
    print_rows(after);

    int count_39 = 0;
    for (const auto& row : after.data) {
        float gpa = std::any_cast<float>(row.columns.at("GPA"));
        if (gpa == 3.9f) {
            count_39++;
        }
    }

    if (count_39 != 3) {
        std::cerr << "[FAIL] Expected 3 rows with GPA=3.9, got " << count_39 << "\n";
        return false;
    }

    std::cout << "[PASS] UPDATE Multiple Rows test\n";
    return true;
}

bool test_update_with_string_condition() {
    std::cout << "\n=== TEST 24: UPDATE with String Condition ===" << std::endl;

    TestSetup setup("qp_test_update_str");
    setup.insert_student(1, "Alice", 3.8f);
    setup.insert_student(2, "Bob", 3.0f);
    setup.insert_student(3, "Charlie", 3.5f);
    setup.insert_student(4, "Diana", 3.5f);
    setup.insert_student(5, "Alice", 2.5f);
    setup.insert_student(6, "Frank", 3.7f);
    setup.insert_student(7, "Grace", 3.4f);
    setup.insert_student(8, "Henry", 2.8f);
    setup.insert_student(9, "Ivy", 3.6f);
    setup.insert_student(10, "Alice", 3.1f);

    std::cout << "Before UPDATE:" << std::endl;
    auto before = setup.read_all_students();
    print_rows(before);

    // Should update 3 rows (ID 1, 5, 10)
    mdbms::qo::ParsedQuery update_query;
    update_query.query_type = "UPDATE";
    update_query.from_tables.push_back("Student");
    update_query.set_values["GPA"] = 4.0f;
    update_query.where_conditions.push_back(mdbms::Condition("FullName", "=", std::string("Alice")));

    int txn_id = setup.qp->begin_transaction();
    int affected = setup.qp->execute_update(update_query, txn_id);

    std::cout << "UPDATE Student SET GPA = 4.0 WHERE FullName = 'Alice'" << std::endl;
    std::cout << "Affected rows: " << affected << std::endl;

    if (affected != 3) {
        std::cerr << "[FAIL] Expected 3 affected rows, got " << affected << "\n";
        return false;
    }

    std::cout << "After UPDATE:" << std::endl;
    auto after = setup.read_all_students();
    print_rows(after);

    for (const auto& row : after.data) {
        std::string name = std::any_cast<std::string>(row.columns.at("FullName"));
        if (name == "Alice") {
            float gpa = std::any_cast<float>(row.columns.at("GPA"));
            if (gpa != 4.0f) {
                std::cerr << "[FAIL] Alice's GPA should be 4.0, got " << gpa << "\n";
                return false;
            }
        }
    }

    std::cout << "[PASS] UPDATE with String Condition test\n";
    return true;
}

bool test_natural_join() {
    std::cout << "\n=== TEST 25: NATURAL JOIN ===" << std::endl;

    TestSetup setup("qp_test_natural_join");

    // Left table with columns: ID, Name
    mdbms::Rows<mdbms::Row> left_table;
    mdbms::Row l1;
    l1.columns["ID"] = 1;
    l1.columns["Name"] = std::string("Alice");
    left_table.data.push_back(l1);

    mdbms::Row l2;
    l2.columns["ID"] = 2;
    l2.columns["Name"] = std::string("Bob");
    left_table.data.push_back(l2);

    mdbms::Row l3;
    l3.columns["ID"] = 3;
    l3.columns["Name"] = std::string("Charlie");
    left_table.data.push_back(l3);

    mdbms::Row l4;
    l4.columns["ID"] = 4;
    l4.columns["Name"] = std::string("Diana");
    left_table.data.push_back(l4);

    mdbms::Row l5;
    l5.columns["ID"] = 5;
    l5.columns["Name"] = std::string("Eve");
    left_table.data.push_back(l5);

    mdbms::Row l6;
    l6.columns["ID"] = 6;
    l6.columns["Name"] = std::string("Frank");
    left_table.data.push_back(l6);

    mdbms::Row l7;
    l7.columns["ID"] = 7;
    l7.columns["Name"] = std::string("Grace");
    left_table.data.push_back(l7);

    mdbms::Row l8;
    l8.columns["ID"] = 8;
    l8.columns["Name"] = std::string("Henry");
    left_table.data.push_back(l8);

    mdbms::Row l9;
    l9.columns["ID"] = 9;
    l9.columns["Name"] = std::string("Ivy");
    left_table.data.push_back(l9);

    mdbms::Row l10;
    l10.columns["ID"] = 10;
    l10.columns["Name"] = std::string("Jack");
    left_table.data.push_back(l10);

    left_table.rows_count = 10;

    // Right table with columns: ID, Score
    mdbms::Rows<mdbms::Row> right_table;
    mdbms::Row r1;
    r1.columns["ID"] = 1;
    r1.columns["Score"] = 95;
    right_table.data.push_back(r1);

    mdbms::Row r2;
    r2.columns["ID"] = 2;
    r2.columns["Score"] = 87;
    right_table.data.push_back(r2);

    mdbms::Row r3;
    r3.columns["ID"] = 4;
    r3.columns["Score"] = 90;
    right_table.data.push_back(r3);

    mdbms::Row r4;
    r4.columns["ID"] = 5;
    r4.columns["Score"] = 78;
    right_table.data.push_back(r4);

    mdbms::Row r5;
    r5.columns["ID"] = 7;
    r5.columns["Score"] = 92;
    right_table.data.push_back(r5);

    mdbms::Row r6;
    r6.columns["ID"] = 10;
    r6.columns["Score"] = 85;
    right_table.data.push_back(r6);

    mdbms::Row r7;
    r7.columns["ID"] = 15;  
    r7.columns["Score"] = 88;
    right_table.data.push_back(r7);

    mdbms::Row r8;
    r8.columns["ID"] = 20;  
    r8.columns["Score"] = 91;
    right_table.data.push_back(r8);

    right_table.rows_count = 8;

    std::cout << "Left table (ID, Name):" << std::endl;
    print_rows(left_table);
    std::cout << "Right table (ID, Score):" << std::endl;
    print_rows(right_table);

    mdbms::Condition empty_cond;
    auto result = setup.qp->execute_join(left_table, right_table, empty_cond, "NATURAL");

    std::cout << "NATURAL JOIN (on ID):" << std::endl;
    print_rows(result);

    // Should have 6 rows: ID=1,2,4,5,7,10
    if (result.rows_count != 6) {
        std::cerr << "[FAIL] Expected 6 rows, got " << result.rows_count << "\n";
        return false;
    }

    bool has_id = result.data[0].columns.find("ID") != result.data[0].columns.end();
    bool has_name = result.data[0].columns.find("Name") != result.data[0].columns.end();
    bool has_score = result.data[0].columns.find("Score") != result.data[0].columns.end();

    if (!has_id || !has_name || !has_score) {
        std::cerr << "[FAIL] Natural join result missing expected columns\n";
        return false;
    }

    std::cout << "[PASS] NATURAL JOIN test\n";
    return true;
}

int main() {
    std::cout << "===================================" << std::endl;
    std::cout << "  Query Processor Tests" << std::endl;
    std::cout << "===================================" << std::endl;

    int passed = 0;
    int total = 25;

    if (test_basic_integration()) passed++;
    if (test_begin_transaction()) passed++;
    if (test_limit()) passed++;
    if (test_order_by_string_asc()) passed++;
    if (test_order_by_string_desc()) passed++;
    if (test_order_by_int_asc()) passed++;
    if (test_order_by_int_desc()) passed++;
    if (test_order_by_float_asc()) passed++;
    if (test_order_by_float_desc()) passed++;
    if (test_where_integer_operators()) passed++;
    if (test_where_float_operators()) passed++;
    if (test_where_string_operators()) passed++;
    if (test_where_multiple_conditions()) passed++;
    if (test_combined_where_orderby_limit()) passed++;
    if (test_cartesian_product()) passed++;
    if (test_join_on()) passed++;
    if (test_execute_select_basic()) passed++;
    if (test_execute_select_with_where()) passed++;
    if (test_execute_select_with_order_by()) passed++;
    if (test_execute_select_with_limit()) passed++;
    if (test_execute_select_combined()) passed++;
    if (test_update_single_column()) passed++;
    if (test_update_multiple_rows()) passed++;
    if (test_update_with_string_condition()) passed++;
    if (test_natural_join()) passed++;

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << passed << "/" << total << " tests passed" << std::endl;
    std::cout << "========================================" << std::endl;

    return (passed == total) ? 0 : 1;
}

#include "query_processor.h"
#include "query_optimizer.h"
#include "storage_manager.h"
#include "concurrency_control.h"
#include "failure_recovery.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <memory>

using namespace mdbms;
using namespace mdbms::qp;
using namespace mdbms::sm;

// ============================================================================
// Helper Functions
// ============================================================================

std::string any_to_string(const std::any& value) {
    if (!value.has_value()) {
        return "NULL";
    }

    // Integer types
    if (value.type() == typeid(int)) {
        return std::to_string(std::any_cast<int>(value));
    }
    if (value.type() == typeid(long)) {
        return std::to_string(std::any_cast<long>(value));
    }
    if (value.type() == typeid(long long)) {
        return std::to_string(std::any_cast<long long>(value));
    }
    if (value.type() == typeid(unsigned int)) {
        return std::to_string(std::any_cast<unsigned int>(value));
    }
    if (value.type() == typeid(unsigned long)) {
        return std::to_string(std::any_cast<unsigned long>(value));
    }
    if (value.type() == typeid(unsigned long long)) {
        return std::to_string(std::any_cast<unsigned long long>(value));
    }
    if (value.type() == typeid(short)) {
        return std::to_string(std::any_cast<short>(value));
    }
    if (value.type() == typeid(int64_t)) {
        return std::to_string(std::any_cast<int64_t>(value));
    }
    if (value.type() == typeid(uint64_t)) {
        return std::to_string(std::any_cast<uint64_t>(value));
    }
    if (value.type() == typeid(size_t)) {
        return std::to_string(std::any_cast<size_t>(value));
    }

    if (value.type() == typeid(float)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << std::any_cast<float>(value);
        return oss.str();
    }
    if (value.type() == typeid(double)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << std::any_cast<double>(value);
        return oss.str();
    }

    // String types
    if (value.type() == typeid(std::string)) {
        return std::any_cast<std::string>(value);
    }
    if (value.type() == typeid(const char*)) {
        return std::string(std::any_cast<const char*>(value));
    }

    if (value.type() == typeid(bool)) {
        return std::any_cast<bool>(value) ? "true" : "false";
    }

    return std::string("<?") + value.type().name() + "?>";
}

// Safe cast helpers to handle different integer representations across platforms
int safe_cast_int(const std::any& value) {
    if (value.type() == typeid(int)) return std::any_cast<int>(value);
    if (value.type() == typeid(long)) return static_cast<int>(std::any_cast<long>(value));
    if (value.type() == typeid(long long)) return static_cast<int>(std::any_cast<long long>(value));
    if (value.type() == typeid(short)) return static_cast<int>(std::any_cast<short>(value));
    if (value.type() == typeid(unsigned int)) return static_cast<int>(std::any_cast<unsigned int>(value));
    if (value.type() == typeid(unsigned long)) return static_cast<int>(std::any_cast<unsigned long>(value));
    if (value.type() == typeid(int64_t)) return static_cast<int>(std::any_cast<int64_t>(value));
    if (value.type() == typeid(uint64_t)) return static_cast<int>(std::any_cast<uint64_t>(value));
    if (value.type() == typeid(size_t)) return static_cast<int>(std::any_cast<size_t>(value));
    throw std::bad_any_cast();
}

float safe_cast_float(const std::any& value) {
    if (value.type() == typeid(float)) return std::any_cast<float>(value);
    if (value.type() == typeid(double)) return static_cast<float>(std::any_cast<double>(value));
    if (value.type() == typeid(int)) return static_cast<float>(std::any_cast<int>(value));
    if (value.type() == typeid(long)) return static_cast<float>(std::any_cast<long>(value));
    if (value.type() == typeid(long long)) return static_cast<float>(std::any_cast<long long>(value));
    throw std::bad_any_cast();
}

std::string safe_cast_string(const std::any& value) {
    if (value.type() == typeid(std::string)) return std::any_cast<std::string>(value);
    if (value.type() == typeid(const char*)) return std::string(std::any_cast<const char*>(value));
    throw std::bad_any_cast();
}

void print_rows(const Rows<Row>& rows) {
    if (rows.data.empty()) {
        std::cout << "Empty set (0 rows)" << std::endl;
        return;
    }

    // Get column names from first row
    std::vector<std::string> col_names;
    std::map<std::string, size_t> col_widths;

    for (const auto& [col_name, value] : rows.data[0].columns) {
        col_names.push_back(col_name);
        col_widths[col_name] = col_name.length();
    }

    // Calculate max widths
    for (const auto& row : rows.data) {
        for (const auto& col_name : col_names) {
            auto it = row.columns.find(col_name);
            if (it != row.columns.end()) {
                size_t val_len = any_to_string(it->second).length();
                if (val_len > col_widths[col_name]) {
                    col_widths[col_name] = val_len;
                }
            }
        }
    }

    // Print header
    std::cout << "+";
    for (const auto& col_name : col_names) {
        std::cout << std::string(col_widths[col_name] + 2, '-') << "+";
    }
    std::cout << std::endl;

    std::cout << "|";
    for (const auto& col_name : col_names) {
        std::cout << " " << std::setw(col_widths[col_name]) << col_name << " |";
    }
    std::cout << std::endl;

    std::cout << "+";
    for (const auto& col_name : col_names) {
        std::cout << std::string(col_widths[col_name] + 2, '-') << "+";
    }
    std::cout << std::endl;

    // Print rows
    for (const auto& row : rows.data) {
        std::cout << "|";
        for (const auto& col_name : col_names) {
            auto it = row.columns.find(col_name);
            std::string val = (it != row.columns.end()) ? any_to_string(it->second) : "NULL";
            std::cout << " " << std::setw(col_widths[col_name]) << val << " |";
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

void print_result(const ExecutionResult& result) {
    std::cout << "  Success: " << (result.success ? "YES" : "NO") << std::endl;
    std::cout << "  Message: " << result.message << std::endl;
    std::cout << "  Affected rows: " << result.affected_rows << std::endl;
    if (!result.data.data.empty()) {
        std::cout << "  Data rows: " << result.data.rows_count << std::endl;
    }
}

// ============================================================================
// Test Data Setup Helper
// ============================================================================

/**
 * Creates sample Rows<Row> data for testing QueryProcessor methods
 * that don't require database access (apply_where, apply_order_by, apply_limit, etc.)
 */
Rows<Row> create_sample_student_data() {
    Rows<Row> rows;
    rows.column_names = {"StudentID", "FullName", "GPA"};
    
    // Add 10 students with various data
    std::vector<std::tuple<int, std::string, float>> students = {
        {1, "Alice", 3.8f},
        {2, "Bob", 3.0f},
        {3, "Charlie", 3.5f},
        {4, "Diana", 3.5f},
        {5, "Eve", 2.5f},
        {6, "Frank", 3.7f},
        {7, "Grace", 3.4f},
        {8, "Henry", 2.8f},
        {9, "Ivy", 3.6f},
        {10, "Jack", 3.1f}
    };
    
    for (const auto& [id, name, gpa] : students) {
        Row row;
        row.table_name = "Student";
        row.row_id = id;
        row.columns["StudentID"] = id;
        row.columns["FullName"] = name;
        row.columns["GPA"] = gpa;
        rows.data.push_back(row);
    }
    
    rows.rows_count = static_cast<int>(rows.data.size());
    return rows;
}

Rows<Row> create_sample_course_data() {
    Rows<Row> rows;
    rows.column_names = {"CourseID", "CourseName", "Credits"};
    
    std::vector<std::tuple<int, std::string, int>> courses = {
        {101, "Mathematics", 3},
        {102, "Physics", 4},
        {103, "Chemistry", 3},
        {104, "Biology", 3},
        {105, "Computer Science", 4}
    };
    
    for (const auto& [id, name, credits] : courses) {
        Row row;
        row.table_name = "Course";
        row.row_id = id;
        row.columns["CourseID"] = id;
        row.columns["CourseName"] = name;
        row.columns["Credits"] = credits;
        rows.data.push_back(row);
    }
    
    rows.rows_count = static_cast<int>(rows.data.size());
    return rows;
}

Rows<Row> create_sample_enrollment_data() {
    Rows<Row> rows;
    rows.column_names = {"StudentID", "CourseID", "Grade"};
    
    std::vector<std::tuple<int, int, std::string>> enrollments = {
        {1, 101, "A"},
        {1, 102, "B+"},
        {2, 101, "B"},
        {2, 103, "A-"},
        {3, 102, "A"},
        {3, 104, "B+"},
        {4, 105, "A"},
        {5, 101, "C+"}
    };
    
    int row_id = 1;
    for (const auto& [sid, cid, grade] : enrollments) {
        Row row;
        row.table_name = "Enrollment";
        row.row_id = row_id++;
        row.columns["StudentID"] = sid;
        row.columns["CourseID"] = cid;
        row.columns["Grade"] = grade;
        rows.data.push_back(row);
    }
    
    rows.rows_count = static_cast<int>(rows.data.size());
    return rows;
}

// ============================================================================
// UNIT TESTS - Test QueryProcessor methods with in-memory data
// ============================================================================

bool test_apply_limit() {
    std::cout << "\n=== TEST: apply_limit ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::cout << "Original data has " << data.rows_count << " rows" << std::endl;
    
    // Test limit less than total
    auto limited = qp.apply_limit(data, 5);
    if (limited.rows_count != 5) {
        std::cerr << "[FAIL] Expected 5 rows, got " << limited.rows_count << std::endl;
        return false;
    }
    std::cout << "  LIMIT 5: got " << limited.rows_count << " rows [OK]" << std::endl;
    
    // Test limit greater than total
    limited = qp.apply_limit(data, 20);
    if (limited.rows_count != 10) {
        std::cerr << "[FAIL] Expected 10 rows (all), got " << limited.rows_count << std::endl;
        return false;
    }
    std::cout << "  LIMIT 20: got " << limited.rows_count << " rows (capped at total) [OK]" << std::endl;
    
    // Test limit of 0
    limited = qp.apply_limit(data, 0);
    if (limited.rows_count != 10) {
        std::cerr << "[FAIL] LIMIT 0 should return all rows, got " << limited.rows_count << std::endl;
        return false;
    }
    std::cout << "  LIMIT 0: got " << limited.rows_count << " rows (returns all) [OK]" << std::endl;
    
    // Test limit of 1
    limited = qp.apply_limit(data, 1);
    if (limited.rows_count != 1) {
        std::cerr << "[FAIL] Expected 1 row, got " << limited.rows_count << std::endl;
        return false;
    }
    std::cout << "  LIMIT 1: got " << limited.rows_count << " row [OK]" << std::endl;
    
    std::cout << "[PASS] apply_limit test" << std::endl;
    return true;
}

bool test_apply_order_by_string_asc() {
    std::cout << "\n=== TEST: apply_order_by (String ASC) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    auto sorted = qp.apply_order_by(data, "FullName", true);
    
    if (sorted.data.empty()) {
        std::cerr << "[FAIL] Sorted result is empty" << std::endl;
        return false;
    }
    
    std::string first = safe_cast_string(sorted.data[0].columns.at("FullName"));
    std::string last = safe_cast_string(sorted.data[sorted.data.size()-1].columns.at("FullName"));
    
    std::cout << "  First: " << first << ", Last: " << last << std::endl;
    
    if (first != "Alice") {
        std::cerr << "[FAIL] First should be Alice, got " << first << std::endl;
        return false;
    }
    
    if (last != "Jack") {
        std::cerr << "[FAIL] Last should be Jack, got " << last << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_order_by String ASC test" << std::endl;
    return true;
}

bool test_apply_order_by_string_desc() {
    std::cout << "\n=== TEST: apply_order_by (String DESC) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    auto sorted = qp.apply_order_by(data, "FullName", false);
    
    if (sorted.data.empty()) {
        std::cerr << "[FAIL] Sorted result is empty" << std::endl;
        return false;
    }
    
    std::string first = safe_cast_string(sorted.data[0].columns.at("FullName"));
    std::string last = safe_cast_string(sorted.data[sorted.data.size()-1].columns.at("FullName"));
    
    std::cout << "  First: " << first << ", Last: " << last << std::endl;
    
    if (first != "Jack") {
        std::cerr << "[FAIL] First should be Jack (DESC), got " << first << std::endl;
        return false;
    }
    
    if (last != "Alice") {
        std::cerr << "[FAIL] Last should be Alice (DESC), got " << last << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_order_by String DESC test" << std::endl;
    return true;
}

bool test_apply_order_by_int_asc() {
    std::cout << "\n=== TEST: apply_order_by (Integer ASC) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    // Shuffle the data first to make sure sorting works
    std::swap(data.data[0], data.data[5]);
    std::swap(data.data[2], data.data[8]);
    
    auto sorted = qp.apply_order_by(data, "StudentID", true);
    
    if (sorted.data.empty()) {
        std::cerr << "[FAIL] Sorted result is empty" << std::endl;
        return false;
    }
    
    int first = safe_cast_int(sorted.data[0].columns.at("StudentID"));
    int last = safe_cast_int(sorted.data[sorted.data.size()-1].columns.at("StudentID"));
    
    std::cout << "  First ID: " << first << ", Last ID: " << last << std::endl;
    
    if (first != 1) {
        std::cerr << "[FAIL] First StudentID should be 1, got " << first << std::endl;
        return false;
    }
    
    if (last != 10) {
        std::cerr << "[FAIL] Last StudentID should be 10, got " << last << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_order_by Integer ASC test" << std::endl;
    return true;
}

bool test_apply_order_by_int_desc() {
    std::cout << "\n=== TEST: apply_order_by (Integer DESC) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    auto sorted = qp.apply_order_by(data, "StudentID", false);
    
    if (sorted.data.empty()) {
        std::cerr << "[FAIL] Sorted result is empty" << std::endl;
        return false;
    }
    
    int first = safe_cast_int(sorted.data[0].columns.at("StudentID"));
    int last = safe_cast_int(sorted.data[sorted.data.size()-1].columns.at("StudentID"));
    
    std::cout << "  First ID: " << first << ", Last ID: " << last << std::endl;
    
    if (first != 10) {
        std::cerr << "[FAIL] First StudentID should be 10 (DESC), got " << first << std::endl;
        return false;
    }
    
    if (last != 1) {
        std::cerr << "[FAIL] Last StudentID should be 1 (DESC), got " << last << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_order_by Integer DESC test" << std::endl;
    return true;
}

bool test_apply_order_by_float_asc() {
    std::cout << "\n=== TEST: apply_order_by (Float ASC) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    auto sorted = qp.apply_order_by(data, "GPA", true);
    
    if (sorted.data.empty()) {
        std::cerr << "[FAIL] Sorted result is empty" << std::endl;
        return false;
    }
    
    float first = safe_cast_float(sorted.data[0].columns.at("GPA"));
    float last = safe_cast_float(sorted.data[sorted.data.size()-1].columns.at("GPA"));
    
    std::cout << "  First GPA: " << first << ", Last GPA: " << last << std::endl;
    
    // Lowest GPA is 2.5 (Eve), highest is 3.8 (Alice)
    if (first < 2.4f || first > 2.6f) {
        std::cerr << "[FAIL] First GPA should be ~2.5, got " << first << std::endl;
        return false;
    }
    
    if (last < 3.7f || last > 3.9f) {
        std::cerr << "[FAIL] Last GPA should be ~3.8, got " << last << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_order_by Float ASC test" << std::endl;
    return true;
}

bool test_apply_order_by_float_desc() {
    std::cout << "\n=== TEST: apply_order_by (Float DESC) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    auto sorted = qp.apply_order_by(data, "GPA", false);
    
    if (sorted.data.empty()) {
        std::cerr << "[FAIL] Sorted result is empty" << std::endl;
        return false;
    }
    
    float first = safe_cast_float(sorted.data[0].columns.at("GPA"));
    float last = safe_cast_float(sorted.data[sorted.data.size()-1].columns.at("GPA"));
    
    std::cout << "  First GPA: " << first << ", Last GPA: " << last << std::endl;
    
    // DESC: highest GPA is 3.8 (Alice), lowest is 2.5 (Eve)
    if (first < 3.7f || first > 3.9f) {
        std::cerr << "[FAIL] First GPA should be ~3.8 (DESC), got " << first << std::endl;
        return false;
    }
    
    if (last < 2.4f || last > 2.6f) {
        std::cerr << "[FAIL] Last GPA should be ~2.5 (DESC), got " << last << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_order_by Float DESC test" << std::endl;
    return true;
}

bool test_apply_where_int_equals() {
    std::cout << "\n=== TEST: apply_where_clause (INT =) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::vector<Condition> conditions = {
        Condition("StudentID", "=", 5)
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE StudentID = 5: got " << filtered.rows_count << " rows" << std::endl;
    
    if (filtered.rows_count != 1) {
        std::cerr << "[FAIL] Expected 1 row, got " << filtered.rows_count << std::endl;
        return false;
    }
    
    std::string name = safe_cast_string(filtered.data[0].columns.at("FullName"));
    if (name != "Eve") {
        std::cerr << "[FAIL] Expected Eve, got " << name << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_where_clause INT = test" << std::endl;
    return true;
}

bool test_apply_where_int_greater() {
    std::cout << "\n=== TEST: apply_where_clause (INT >) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::vector<Condition> conditions = {
        Condition("StudentID", ">", 7)
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE StudentID > 7: got " << filtered.rows_count << " rows" << std::endl;
    
    if (filtered.rows_count != 3) {  // IDs 8, 9, 10
        std::cerr << "[FAIL] Expected 3 rows, got " << filtered.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_where_clause INT > test" << std::endl;
    return true;
}

bool test_apply_where_int_less() {
    std::cout << "\n=== TEST: apply_where_clause (INT <) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::vector<Condition> conditions = {
        Condition("StudentID", "<", 4)
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE StudentID < 4: got " << filtered.rows_count << " rows" << std::endl;
    
    if (filtered.rows_count != 3) {  // IDs 1, 2, 3
        std::cerr << "[FAIL] Expected 3 rows, got " << filtered.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_where_clause INT < test" << std::endl;
    return true;
}

bool test_apply_where_float_greater() {
    std::cout << "\n=== TEST: apply_where_clause (FLOAT >) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::vector<Condition> conditions = {
        Condition("GPA", ">", 3.5f)
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE GPA > 3.5: got " << filtered.rows_count << " rows" << std::endl;
    print_rows(filtered);
    
    // GPAs > 3.5: Alice(3.8), Frank(3.7), Ivy(3.6)
    if (filtered.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows (GPA > 3.5), got " << filtered.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_where_clause FLOAT > test" << std::endl;
    return true;
}

bool test_apply_where_float_less_equal() {
    std::cout << "\n=== TEST: apply_where_clause (FLOAT <=) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::vector<Condition> conditions = {
        Condition("GPA", "<=", 3.0f)
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE GPA <= 3.0: got " << filtered.rows_count << " rows" << std::endl;
    
    // GPAs <= 3.0: Bob(3.0), Eve(2.5), Henry(2.8)
    if (filtered.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows (GPA <= 3.0), got " << filtered.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_where_clause FLOAT <= test" << std::endl;
    return true;
}

bool test_apply_where_string_equals() {
    std::cout << "\n=== TEST: apply_where_clause (STRING =) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::vector<Condition> conditions = {
        Condition("FullName", "=", std::string("Alice"))
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE FullName = 'Alice': got " << filtered.rows_count << " rows" << std::endl;
    
    if (filtered.rows_count != 1) {
        std::cerr << "[FAIL] Expected 1 row, got " << filtered.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_where_clause STRING = test" << std::endl;
    return true;
}

bool test_apply_where_multiple_conditions() {
    std::cout << "\n=== TEST: apply_where_clause (Multiple AND) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    // Find students with GPA > 3.0 AND StudentID < 6
    std::vector<Condition> conditions = {
        Condition("GPA", ">", 3.0f),
        Condition("StudentID", "<", 6)
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE GPA > 3.0 AND StudentID < 6: got " << filtered.rows_count << " rows" << std::endl;
    print_rows(filtered);
    
    // Matching: Alice(1, 3.8), Charlie(3, 3.5), Diana(4, 3.5)
    if (filtered.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << filtered.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_where_clause Multiple AND test" << std::endl;
    return true;
}

bool test_apply_combined_where_orderby_limit() {
    std::cout << "\n=== TEST: Combined WHERE + ORDER BY + LIMIT ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    // WHERE GPA >= 3.0
    std::vector<Condition> conditions = {
        Condition("GPA", ">=", 3.0f)
    };
    auto filtered = qp.apply_where_clause(data, conditions);
    std::cout << "  After WHERE GPA >= 3.0: " << filtered.rows_count << " rows" << std::endl;
    
    // ORDER BY GPA DESC
    auto sorted = qp.apply_order_by(filtered, "GPA", false);
    
    // LIMIT 5
    auto limited = qp.apply_limit(sorted, 5);
    std::cout << "  After ORDER BY GPA DESC LIMIT 5: " << limited.rows_count << " rows" << std::endl;
    print_rows(limited);
    
    if (limited.rows_count != 5) {
        std::cerr << "[FAIL] Expected 5 rows, got " << limited.rows_count << std::endl;
        return false;
    }
    
    // First should be Alice (3.8), second Frank (3.7)
    float first_gpa = safe_cast_float(limited.data[0].columns.at("GPA"));
    if (first_gpa < 3.75f) {
        std::cerr << "[FAIL] First GPA should be highest (~3.8), got " << first_gpa << std::endl;
        return false;
    }
    
    std::cout << "[PASS] Combined WHERE + ORDER BY + LIMIT test" << std::endl;
    return true;
}

bool test_execute_join_cartesian() {
    std::cout << "\n=== TEST: execute_join (Cartesian Product) ===" << std::endl;
    
    QueryProcessor qp;
    
    // Create small datasets for cross join
    Rows<Row> students;
    students.column_names = {"StudentID", "Name"};
    for (int i = 1; i <= 3; i++) {
        Row r;
        r.table_name = "Student";
        r.columns["StudentID"] = i;
        r.columns["Name"] = std::string("S") + std::to_string(i);
        students.data.push_back(r);
    }
    students.rows_count = 3;
    
    Rows<Row> courses;
    courses.column_names = {"CourseID", "Title"};
    for (int i = 1; i <= 2; i++) {
        Row r;
        r.table_name = "Course";
        r.columns["CourseID"] = i;
        r.columns["Title"] = std::string("C") + std::to_string(i);
        courses.data.push_back(r);
    }
    courses.rows_count = 2;
    
    // Empty condition = cartesian product
    Condition empty_cond;
    auto joined = qp.execute_join(students, courses, empty_cond, "CROSS");
    
    std::cout << "  3 students x 2 courses = " << joined.rows_count << " rows" << std::endl;
    
    if (joined.rows_count != 6) {
        std::cerr << "[FAIL] Expected 6 rows (3x2), got " << joined.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] execute_join Cartesian Product test" << std::endl;
    return true;
}

bool test_execute_join_on_condition() {
    std::cout << "\n=== TEST: execute_join (ON condition) ===" << std::endl;
    
    QueryProcessor qp;
    
    // Students
    Rows<Row> students;
    students.column_names = {"StudentID", "Name"};
    Row s1; s1.table_name = "Student"; s1.columns["StudentID"] = 1; s1.columns["Name"] = std::string("Alice"); students.data.push_back(s1);
    Row s2; s2.table_name = "Student"; s2.columns["StudentID"] = 2; s2.columns["Name"] = std::string("Bob"); students.data.push_back(s2);
    Row s3; s3.table_name = "Student"; s3.columns["StudentID"] = 3; s3.columns["Name"] = std::string("Charlie"); students.data.push_back(s3);
    students.rows_count = 3;
    
    // Enrollments (with StudentID foreign key)
    Rows<Row> enrollments;
    enrollments.column_names = {"EnrollID", "StudentID", "Course"};
    Row e1; e1.table_name = "Enrollment"; e1.columns["EnrollID"] = 1; e1.columns["StudentID"] = 1; e1.columns["Course"] = std::string("Math"); enrollments.data.push_back(e1);
    Row e2; e2.table_name = "Enrollment"; e2.columns["EnrollID"] = 2; e2.columns["StudentID"] = 1; e2.columns["Course"] = std::string("Physics"); enrollments.data.push_back(e2);
    Row e3; e3.table_name = "Enrollment"; e3.columns["EnrollID"] = 3; e3.columns["StudentID"] = 2; e3.columns["Course"] = std::string("Math"); enrollments.data.push_back(e3);
    enrollments.rows_count = 3;
    
    // JOIN ON Student.StudentID = Enrollment.StudentID
    Condition join_cond("Student.StudentID", "=", std::string("Enrollment.StudentID"));
    auto joined = qp.execute_join(students, enrollments, join_cond, "INNER");
    
    std::cout << "  JOIN result: " << joined.rows_count << " rows" << std::endl;
    print_rows(joined);
    
    // Expected: Alice-Math, Alice-Physics, Bob-Math (3 rows)
    // Charlie has no enrollment so won't appear
    if (joined.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << joined.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] execute_join ON condition test" << std::endl;
    return true;
}

bool test_execute_natural_join() {
    std::cout << "\n=== TEST: execute_join (NATURAL JOIN) ===" << std::endl;
    
    QueryProcessor qp;
    
    // Students with StudentID
    Rows<Row> students;
    students.column_names = {"StudentID", "Name"};
    Row s1; s1.table_name = "Student"; s1.columns["StudentID"] = 1; s1.columns["Name"] = std::string("Alice"); students.data.push_back(s1);
    Row s2; s2.table_name = "Student"; s2.columns["StudentID"] = 2; s2.columns["Name"] = std::string("Bob"); students.data.push_back(s2);
    students.rows_count = 2;
    
    // Grades with StudentID (common column)
    Rows<Row> grades;
    grades.column_names = {"StudentID", "Grade"};
    Row g1; g1.table_name = "Grade"; g1.columns["StudentID"] = 1; g1.columns["Grade"] = std::string("A"); grades.data.push_back(g1);
    Row g2; g2.table_name = "Grade"; g2.columns["StudentID"] = 2; g2.columns["Grade"] = std::string("B"); grades.data.push_back(g2);
    Row g3; g3.table_name = "Grade"; g3.columns["StudentID"] = 1; g3.columns["Grade"] = std::string("A-"); grades.data.push_back(g3);
    grades.rows_count = 3;
    
    // NATURAL JOIN on common column "StudentID"
    Condition empty_cond;
    auto joined = qp.execute_join(students, grades, empty_cond, "NATURAL");
    
    std::cout << "  NATURAL JOIN result: " << joined.rows_count << " rows" << std::endl;
    print_rows(joined);
    
    // Expected: Alice-A, Alice-A-, Bob-B (3 rows)
    if (joined.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << joined.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] execute_join NATURAL JOIN test" << std::endl;
    return true;
}

bool test_begin_transaction() {
    std::cout << "\n=== TEST: begin_transaction ===" << std::endl;
    
    QueryProcessor qp;
    
    int txn1 = qp.begin_transaction();
    std::cout << "  Transaction 1 ID: " << txn1 << std::endl;
    
    if (txn1 < 0) {
        std::cerr << "[FAIL] Transaction ID should be >= 0" << std::endl;
        return false;
    }
    
    int txn2 = qp.begin_transaction();
    std::cout << "  Transaction 2 ID: " << txn2 << std::endl;
    
    if (txn2 <= txn1) {
        std::cerr << "[FAIL] Second transaction ID should be > first" << std::endl;
        return false;
    }
    
    std::cout << "[PASS] begin_transaction test" << std::endl;
    return true;
}

bool test_table_alias_functions() {
    std::cout << "\n=== TEST: Table Alias Functions ===" << std::endl;
    
    QueryProcessor qp;
    
    // Test get_table_from_alias
    std::map<std::string, std::string> aliases = {
        {"s", "Student"},
        {"c", "Course"},
        {"e", "Enrollment"}
    };
    
    std::string resolved = qp.get_table_from_alias("s", aliases);
    if (resolved != "Student") {
        std::cerr << "[FAIL] get_table_from_alias('s') should return 'Student', got " << resolved << std::endl;
        return false;
    }
    std::cout << "  get_table_from_alias('s') = " << resolved << " [OK]" << std::endl;
    
    // Test with non-alias (should return original)
    resolved = qp.get_table_from_alias("UnknownTable", aliases);
    if (resolved != "UnknownTable") {
        std::cerr << "[FAIL] get_table_from_alias for non-alias should return original" << std::endl;
        return false;
    }
    std::cout << "  get_table_from_alias('UnknownTable') = " << resolved << " [OK]" << std::endl;
    
    // Test resolve_aliased_column
    std::string col = qp.resolve_aliased_column("s.Name", aliases);
    if (col != "Student.Name") {
        std::cerr << "[FAIL] resolve_aliased_column('s.Name') should return 'Student.Name', got " << col << std::endl;
        return false;
    }
    std::cout << "  resolve_aliased_column('s.Name') = " << col << " [OK]" << std::endl;
    
    // Test apply_table_aliases
    Rows<Row> students = create_sample_student_data();
    auto aliased = qp.apply_table_aliases(students, "Student", aliases);
    
    if (aliased.column_names.empty()) {
        std::cerr << "[FAIL] apply_table_aliases returned empty column names" << std::endl;
        return false;
    }
    
    // Column names should now be prefixed with alias "s"
    bool found_aliased_col = false;
    for (const auto& col_name : aliased.column_names) {
        if (col_name.find("s.") == 0) {
            found_aliased_col = true;
            break;
        }
    }
    
    if (found_aliased_col) {
        std::cout << "  apply_table_aliases added alias prefix [OK]" << std::endl;
    } else {
        std::cout << "  apply_table_aliases: columns = ";
        for (const auto& c : aliased.column_names) std::cout << c << " ";
        std::cout << std::endl;
    }
    
    std::cout << "[PASS] Table Alias Functions test" << std::endl;
    return true;
}

bool test_empty_data_handling() {
    std::cout << "\n=== TEST: Empty Data Handling ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> empty_data;
    empty_data.rows_count = 0;
    
    // Test apply_limit on empty data
    auto limited = qp.apply_limit(empty_data, 5);
    if (limited.rows_count != 0) {
        std::cerr << "[FAIL] apply_limit on empty should return 0 rows" << std::endl;
        return false;
    }
    std::cout << "  apply_limit on empty data: " << limited.rows_count << " rows [OK]" << std::endl;
    
    // Test apply_order_by on empty data
    auto sorted = qp.apply_order_by(empty_data, "SomeColumn", true);
    if (sorted.rows_count != 0) {
        std::cerr << "[FAIL] apply_order_by on empty should return 0 rows" << std::endl;
        return false;
    }
    std::cout << "  apply_order_by on empty data: " << sorted.rows_count << " rows [OK]" << std::endl;
    
    // Test apply_where_clause on empty data
    std::vector<Condition> conds = {Condition("x", "=", 1)};
    auto filtered = qp.apply_where_clause(empty_data, conds);
    if (filtered.rows_count != 0) {
        std::cerr << "[FAIL] apply_where_clause on empty should return 0 rows" << std::endl;
        return false;
    }
    std::cout << "  apply_where_clause on empty data: " << filtered.rows_count << " rows [OK]" << std::endl;
    
    std::cout << "[PASS] Empty Data Handling test" << std::endl;
    return true;
}

bool test_where_not_equal() {
    std::cout << "\n=== TEST: apply_where_clause (!=) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::vector<Condition> conditions = {
        Condition("FullName", "!=", std::string("Alice"))
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE FullName != 'Alice': got " << filtered.rows_count << " rows" << std::endl;
    
    // Should return 9 rows (everyone except Alice)
    if (filtered.rows_count != 9) {
        std::cerr << "[FAIL] Expected 9 rows, got " << filtered.rows_count << std::endl;
        return false;
    }
    
    // Verify Alice is not in results
    for (const auto& row : filtered.data) {
        std::string name = safe_cast_string(row.columns.at("FullName"));
        if (name == "Alice") {
            std::cerr << "[FAIL] Alice should not be in filtered results" << std::endl;
            return false;
        }
    }
    
    std::cout << "[PASS] apply_where_clause != test" << std::endl;
    return true;
}

bool test_where_greater_equal() {
    std::cout << "\n=== TEST: apply_where_clause (>=) ===" << std::endl;
    
    QueryProcessor qp;
    Rows<Row> data = create_sample_student_data();
    
    std::vector<Condition> conditions = {
        Condition("StudentID", ">=", 8)
    };
    
    auto filtered = qp.apply_where_clause(data, conditions);
    
    std::cout << "  WHERE StudentID >= 8: got " << filtered.rows_count << " rows" << std::endl;
    
    // IDs 8, 9, 10
    if (filtered.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << filtered.rows_count << std::endl;
        return false;
    }
    
    std::cout << "[PASS] apply_where_clause >= test" << std::endl;
    return true;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "     QueryProcessor Unit Tests" << std::endl;
    std::cout << "================================================" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    // Unit tests that use in-memory data (no storage dependencies)
    std::vector<std::pair<std::string, bool(*)()>> unit_tests = {
        {"apply_limit", test_apply_limit},
        {"apply_order_by (String ASC)", test_apply_order_by_string_asc},
        {"apply_order_by (String DESC)", test_apply_order_by_string_desc},
        {"apply_order_by (Integer ASC)", test_apply_order_by_int_asc},
        {"apply_order_by (Integer DESC)", test_apply_order_by_int_desc},
        {"apply_order_by (Float ASC)", test_apply_order_by_float_asc},
        {"apply_order_by (Float DESC)", test_apply_order_by_float_desc},
        {"apply_where_clause (INT =)", test_apply_where_int_equals},
        {"apply_where_clause (INT >)", test_apply_where_int_greater},
        {"apply_where_clause (INT <)", test_apply_where_int_less},
        {"apply_where_clause (FLOAT >)", test_apply_where_float_greater},
        {"apply_where_clause (FLOAT <=)", test_apply_where_float_less_equal},
        {"apply_where_clause (STRING =)", test_apply_where_string_equals},
        {"apply_where_clause (!=)", test_where_not_equal},
        {"apply_where_clause (>=)", test_where_greater_equal},
        {"apply_where_clause (Multiple AND)", test_apply_where_multiple_conditions},
        {"Combined WHERE + ORDER BY + LIMIT", test_apply_combined_where_orderby_limit},
        {"execute_join (Cartesian)", test_execute_join_cartesian},
        {"execute_join (ON condition)", test_execute_join_on_condition},
        {"execute_join (NATURAL)", test_execute_natural_join},
        {"begin_transaction", test_begin_transaction},
        {"Table Alias Functions", test_table_alias_functions},
        {"Empty Data Handling", test_empty_data_handling},
    };
    
    std::cout << "\n--- Running " << unit_tests.size() << " Unit Tests ---\n";
    
    for (const auto& [name, test_fn] : unit_tests) {
        try {
            if (test_fn()) {
                passed++;
            } else {
                failed++;
                std::cerr << "FAILED: " << name << std::endl;
            }
        } catch (const std::exception& e) {
            failed++;
            std::cerr << "EXCEPTION in " << name << ": " << e.what() << std::endl;
        }
    }
    
    // Summary
    std::cout << "\n================================================" << std::endl;
    std::cout << "              TEST SUMMARY" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "  Total:  " << (passed + failed) << std::endl;
    std::cout << "  Passed: " << passed << std::endl;
    std::cout << "  Failed: " << failed << std::endl;
    std::cout << "================================================" << std::endl;
    
    if (failed > 0) {
        std::cout << "\n[X] SOME TESTS FAILED\n" << std::endl;
        return 1;
    }
    
    std::cout << "\n[OK] ALL TESTS PASSED\n" << std::endl;
    return 0;
}

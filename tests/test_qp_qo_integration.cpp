#include "query_optimizer.h"
#include "query_processor.h"
#include "storage_manager.h"
#include "concurrency_control.h"
#include "failure_recovery.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>

// Helper to insert test data directly via storage manager
void insert_test_data(mdbms::sm::StorageEngine& storage, const std::string& table, 
                      int id, const std::string& name, float gpa) {
    mdbms::DataWrite<mdbms::Row> insert;
    insert.table = table;
    insert.is_insert = true;
    insert.new_value.table_name = table;
    insert.new_value.columns = {
        {"StudentID", id},
        {"FullName", name},
        {"GPA", gpa}
    };
    storage.write_block(insert);
}

bool test_query_optimizer_integration() {
    std::cout << "\n=== TEST: Query Optimizer Integration ===" << std::endl;

    // Setup
    std::string test_dir = "test_qp_qo_integration";
    std::filesystem::create_directory(test_dir);

    auto optimizer = std::make_shared<mdbms::qo::OptimizationEngine>();
    auto storage = std::make_shared<mdbms::sm::StorageEngine>(test_dir);
    auto ccm = std::make_shared<mdbms::ccm::ConcurrencyControlManager>();
    auto recovery = std::make_shared<mdbms::fr::FailureRecoveryManager>();
    
    mdbms::qp::QueryProcessor qp(optimizer, storage, ccm, recovery);

    // Insert test data
    insert_test_data(*storage, "Student", 1, "Alice", 3.8f);
    insert_test_data(*storage, "Student", 2, "Bob", 3.5f);
    insert_test_data(*storage, "Student", 3, "Charlie", 3.2f);

    // Test 1: Verify optimizer is called and parses SELECT query
    std::cout << "Test 1: SELECT query parsing and optimization" << std::endl;
    std::string select_query = "SELECT * FROM Student";
    
    auto result = qp.execute_query(select_query);
    
    if (!result.success) {
        std::cerr << "[FAIL] Query execution failed: " << result.message << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    // Should get all 3 rows
    if (result.affected_rows != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << result.affected_rows << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    std::cout << "  ✓ SELECT query parsed and executed correctly" << std::endl;

    // Test 2: Verify parsed query structure is used
    std::cout << "Test 2: Verify parsed query columns are used" << std::endl;
    std::string select_columns = "SELECT StudentID, FullName FROM Student";
    
    auto result2 = qp.execute_query(select_columns);
    
    if (!result2.success) {
        std::cerr << "[FAIL] Query execution failed: " << result2.message << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    if (result2.data.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << result2.data.rows_count << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    // Verify columns are projected correctly
    if (!result2.data.data.empty()) {
        const auto& first_row = result2.data.data[0];
        if (first_row.columns.find("StudentID") == first_row.columns.end() ||
            first_row.columns.find("FullName") == first_row.columns.end()) {
            std::cerr << "[FAIL] Column projection failed" << std::endl;
            std::filesystem::remove_all(test_dir);
            return false;
        }
    }

    std::cout << "  ✓ Column projection works correctly" << std::endl;

    // Test 3: Verify WHERE conditions are parsed (integration test, not functionality test)
    std::cout << "Test 3: Verify WHERE conditions are parsed by optimizer" << std::endl;
    
    // Parse query directly to verify WHERE conditions are in parsed query
    mdbms::qo::ParsedQuery parsed_with_where = optimizer->parse_query("SELECT * FROM Student WHERE StudentID = 2");
    
    if (parsed_with_where.where_conditions.empty()) {
        std::cerr << "[FAIL] WHERE conditions not parsed by optimizer" << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    if (parsed_with_where.where_conditions[0].column != "StudentID" ||
        parsed_with_where.where_conditions[0].operation != "=") {
        std::cerr << "[FAIL] WHERE condition parsed incorrectly" << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    std::cout << "  ✓ WHERE conditions parsed correctly by optimizer" << std::endl;

    // Test 4: Verify optimizer creates query tree
    std::cout << "Test 4: Verify query tree is created" << std::endl;
    
    // Parse query directly to check query tree
    mdbms::qo::ParsedQuery parsed = optimizer->parse_query(select_query);
    
    if (!parsed.query_tree) {
        std::cerr << "[FAIL] Query tree not created" << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    std::cout << "  ✓ Query tree created: " << parsed.query_tree->type << std::endl;

    // Test 5: Verify optimization is called
    std::cout << "Test 5: Verify query optimization is performed" << std::endl;
    
    mdbms::qo::ParsedQuery optimized = optimizer->optimize_query(parsed);
    
    if (!optimized.query_tree) {
        std::cerr << "[FAIL] Optimized query tree not created" << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    std::cout << "  ✓ Query optimization performed" << std::endl;

    // Cleanup
    std::filesystem::remove_all(test_dir);

    std::cout << "[PASS] Query Optimizer integration test" << std::endl;
    return true;
}

bool test_query_optimizer_parsing_details() {
    std::cout << "\n=== TEST: Query Optimizer Parsing Details ===" << std::endl;

    mdbms::qo::OptimizationEngine optimizer;

    // Test parsing different query types
    std::vector<std::string> queries = {
        "SELECT * FROM Student",
        "SELECT StudentID, FullName FROM Student WHERE GPA > 3.5",
        "SELECT * FROM Student WHERE StudentID = 1",
        "UPDATE Student SET GPA = 4.0 WHERE StudentID = 1",
        "INSERT INTO Student VALUES (4, 'Diana', 3.9)",
        "DELETE FROM Student WHERE StudentID = 2"
    };

    for (const auto& query : queries) {
        mdbms::qo::ParsedQuery parsed = optimizer.parse_query(query);
        
        if (parsed.original_query != query) {
            std::cerr << "[FAIL] Original query not preserved: " << query << std::endl;
            return false;
        }

        if (parsed.query_type.empty()) {
            std::cerr << "[FAIL] Query type not detected for: " << query << std::endl;
            return false;
        }

        std::cout << "  ✓ Parsed: " << parsed.query_type << " - " 
                  << (query.length() > 50 ? query.substr(0, 50) + "..." : query) << std::endl;
    }

    std::cout << "[PASS] Query Optimizer parsing details test" << std::endl;
    return true;
}

bool test_optimized_query_execution() {
    std::cout << "\n=== TEST: Optimized Query Execution ===" << std::endl;

    std::string test_dir = "test_qp_qo_optimized";
    std::filesystem::create_directory(test_dir);

    auto optimizer = std::make_shared<mdbms::qo::OptimizationEngine>();
    auto storage = std::make_shared<mdbms::sm::StorageEngine>(test_dir);
    auto ccm = std::make_shared<mdbms::ccm::ConcurrencyControlManager>();
    auto recovery = std::make_shared<mdbms::fr::FailureRecoveryManager>();
    
    mdbms::qp::QueryProcessor qp(optimizer, storage, ccm, recovery);

    // Insert test data
    insert_test_data(*storage, "Student", 1, "Alice", 3.8f);
    insert_test_data(*storage, "Student", 2, "Bob", 3.5f);
    insert_test_data(*storage, "Student", 3, "Charlie", 3.2f);
    insert_test_data(*storage, "Student", 4, "Diana", 3.9f);

    // Test that optimized query executes correctly
    std::string complex_query = "SELECT StudentID, FullName FROM Student";
    
    auto result = qp.execute_query(complex_query);
    
    if (!result.success) {
        std::cerr << "[FAIL] Optimized query execution failed: " << result.message << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }

    // Should get all 4 rows (Alice, Bob, Charlie, Diana)
    if (result.affected_rows != 4) {
        std::cerr << "[FAIL] Expected 4 rows, got " << result.affected_rows << std::endl;
        std::filesystem::remove_all(test_dir);
        return false;
    }
    
    // Verify column projection is working (only StudentID and FullName, not GPA)
    if (!result.data.data.empty()) {
        const auto& row = result.data.data[0];
        if (row.columns.find("StudentID") == row.columns.end() ||
            row.columns.find("FullName") == row.columns.end()) {
            std::cerr << "[FAIL] Column projection failed" << std::endl;
            std::filesystem::remove_all(test_dir);
            return false;
        }
    }

    // Verify only selected columns are present
    if (!result.data.data.empty()) {
        const auto& row = result.data.data[0];
        if (row.columns.find("StudentID") == row.columns.end() ||
            row.columns.find("FullName") == row.columns.end() ||
            row.columns.find("GPA") != row.columns.end()) {
            std::cerr << "[FAIL] Column projection incorrect" << std::endl;
            std::filesystem::remove_all(test_dir);
            return false;
        }
    }

    std::filesystem::remove_all(test_dir);
    std::cout << "[PASS] Optimized query execution test" << std::endl;
    return true;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Query Processor & Query Optimizer" << std::endl;
    std::cout << "  Integration Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int total = 3;

    if (test_query_optimizer_integration()) passed++;
    if (test_query_optimizer_parsing_details()) passed++;
    if (test_optimized_query_execution()) passed++;

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << passed << "/" << total << " tests passed" << std::endl;
    std::cout << "========================================" << std::endl;

    return (passed == total) ? 0 : 1;
}


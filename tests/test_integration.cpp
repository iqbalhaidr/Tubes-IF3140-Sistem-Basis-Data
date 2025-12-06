#include "query_optimizer.h"
#include "query_processor.h"
#include "storage_manager.h"
#include "concurrency_control.h"
#include "failure_recovery.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>

// Helper to insert test data directly via storage manager
void insert_test_data(mdbms::sm::StorageEngine& storage, const std::string& table, 
                      int id, const std::string& name, float gpa) {
    mdbms::DataWrite<mdbms::Row> insert;
    insert.table = table;
    insert.is_insert = true;
    insert.new_value.table_name = table;
    insert.new_value.columns = {
        {"id", id},      // Match schema: lowercase
        {"name", name},  // Match schema: lowercase
        {"gpa", gpa}     // Match schema: lowercase
    };
    storage.write_block(insert);
}

// Test setup helper
class IntegrationTestSetup {
public:
    std::string test_dir;
    std::unique_ptr<mdbms::qp::QueryProcessor> qp;

    IntegrationTestSetup(const std::string& dir) : test_dir(dir) {
        // Clear buffer pool from previous test
        auto& storage = mdbms::sm::StorageEngine::get_instance();
        storage.clear_buffer_for_testing();
        
        std::filesystem::create_directory(test_dir);
        // All components are singletons, QueryProcessor uses get_instance() for all
        qp = std::make_unique<mdbms::qp::QueryProcessor>();
    }

    ~IntegrationTestSetup() {
        std::filesystem::remove_all(test_dir);
    }

    void insert_student_direct(int id, const std::string& name, float gpa) {
        // All components are singletons, use get_instance()
        insert_test_data(mdbms::sm::StorageEngine::get_instance(), "Student", id, name, gpa);
    }
};

// ============================================================================
// Test 1: Basic Integration (All Components)
// ============================================================================
bool test_basic_integration() {
    std::cout << "\n=== TEST 1: Basic Integration (QP + QO + SM + CCM + FRM) ===" << std::endl;

    // All components are singletons, QueryProcessor uses get_instance() for all
    mdbms::qp::QueryProcessor qp;

    const std::string query = "SELECT * FROM Student";
    std::ostringstream captured_output;
    auto* original_buf = std::cout.rdbuf(captured_output.rdbuf());
    const auto result = qp.execute_query(query);
    std::cout.rdbuf(original_buf);

    bool passed = true;

    if (!result.success) {
        std::cerr << "[FAIL] QueryProcessor reported failure\n";
        passed = false;
    }

    if (result.query != query) {
        std::cerr << "[FAIL] Query string mismatch\n";
        passed = false;
    }

    const std::string out = captured_output.str();
    // Check for CCM transaction start (either "dimulai" or "Transaksi X dimulai")
    if (out.find("dimulai") == std::string::npos && out.find("Transaksi") == std::string::npos) {
        std::cerr << "[FAIL] Concurrency manager did not start transaction\n";
        passed = false;
    }
    // Check for CCM validation (either "diizinkan" or "READ"/"WRITE")
    if (out.find("diizinkan") == std::string::npos && out.find("READ") == std::string::npos && out.find("WRITE") == std::string::npos) {
        std::cerr << "[FAIL] Concurrency manager did not validate object\n";
        passed = false;
    }
    // Check for CCM logging (either "mencatat akses" or "log")
    if (out.find("mencatat akses") == std::string::npos && out.find("log") == std::string::npos) {
        std::cerr << "[FAIL] Concurrency manager did not log object\n";
        passed = false;
    }
    // Check for CCM transaction end (either "COMMITTED" or "Mengakhiri" or "Cleanup")
    if (out.find("COMMITTED") == std::string::npos && out.find("Mengakhiri") == std::string::npos && out.find("Cleanup") == std::string::npos) {
        std::cerr << "[FAIL] Concurrency manager did not end transaction\n";
        passed = false;
    }

    if (out.find("FRM: Menulis log") == std::string::npos) {
        std::cerr << "[FAIL] Failure recovery manager did not log operations\n";
        passed = false;
    }

    if (!passed) {
        return false;
    }

    std::cout << "[PASS] Basic integration test\n";
    return true;
}

// ============================================================================
// Test 2: Query Optimizer Integration
// ============================================================================
bool test_query_optimizer_integration() {
    std::cout << "\n=== TEST 2: Query Optimizer Integration ===" << std::endl;

    IntegrationTestSetup setup("test_qo_integration");
    setup.insert_student_direct(1, "Alice", 3.8f);
    setup.insert_student_direct(2, "Bob", 3.5f);
    setup.insert_student_direct(3, "Charlie", 3.2f);

    // Test 1: Verify optimizer parses and optimizes queries
    std::cout << "  Testing query parsing and optimization..." << std::endl;
    std::string select_query = "SELECT * FROM Student";
    
    auto result = setup.qp->execute_query(select_query);
    
    if (!result.success) {
        std::cerr << "[FAIL] Query execution failed: " << result.message << std::endl;
        return false;
    }

    if (result.affected_rows != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << result.affected_rows << std::endl;
        return false;
    }

    // Test 2: Verify column projection from parsed query
    std::string select_columns = "SELECT StudentID, FullName FROM Student";
    auto result2 = setup.qp->execute_query(select_columns);
    
    if (!result2.success || result2.data.rows_count != 3) {
        std::cerr << "[FAIL] Column projection failed" << std::endl;
        return false;
    }

    // Test 3: Verify query tree is created
    // All components are singletons, use get_instance()
    mdbms::qo::ParsedQuery parsed = mdbms::qo::OptimizationEngine::get_instance().parse_query(select_query);
    if (!parsed.query_tree) {
        std::cerr << "[FAIL] Query tree not created" << std::endl;
        return false;
    }

    // Test 4: Verify optimization is performed
    mdbms::qo::ParsedQuery optimized = mdbms::qo::OptimizationEngine::get_instance().optimize_query(parsed);
    if (!optimized.query_tree) {
        std::cerr << "[FAIL] Optimized query tree not created" << std::endl;
        return false;
    }

    std::cout << "[PASS] Query Optimizer integration test" << std::endl;
    return true;
}

// ============================================================================
// Test 3: Storage Manager Integration
// ============================================================================
bool test_storage_manager_integration() {
    std::cout << "\n=== TEST 3: Storage Manager Integration ===" << std::endl;

    IntegrationTestSetup setup("test_sm_integration");
    
    // Insert data directly via storage manager
    setup.insert_student_direct(1, "Alice", 3.8f);
    setup.insert_student_direct(2, "Bob", 3.5f);
    setup.insert_student_direct(3, "Charlie", 3.2f);

    // Query via query processor
    mdbms::qo::ParsedQuery select_query;
    select_query.query_type = "SELECT";
    select_query.select_columns = {"*"};
    select_query.from_tables.push_back("Student");

    int txn_id = setup.qp->begin_transaction();
    auto result = setup.qp->execute_select(select_query, txn_id);

    if (result.rows_count != 3) {
        std::cerr << "[FAIL] Expected 3 rows, got " << result.rows_count << std::endl;
        return false;
    }

    std::cout << "[PASS] Storage Manager integration test" << std::endl;
    return true;
}

// ============================================================================
// Test 4: Concurrency Control Manager Integration
// ============================================================================
bool test_concurrency_control_integration() {
    std::cout << "\n=== TEST 4: Concurrency Control Manager Integration ===" << std::endl;

    IntegrationTestSetup setup("test_ccm_integration");
    setup.insert_student_direct(1, "Alice", 3.8f);
    setup.insert_student_direct(2, "Bob", 3.5f);

    // Test 1: Verify transaction begins through CCM
    std::cout << "  Testing transaction management..." << std::endl;
    int txn_id = setup.qp->begin_transaction();
    
    if (txn_id < 0) {
        std::cerr << "[FAIL] Failed to begin transaction" << std::endl;
        return false;
    }

    // Test 2: Verify READ access validation for SELECT
    std::cout << "  Testing READ access validation..." << std::endl;
    std::string select_query = "SELECT * FROM Student";
    std::ostringstream captured_output;
    auto* original_buf = std::cout.rdbuf(captured_output.rdbuf());
    
    auto result = setup.qp->execute_query(select_query);
    
    std::cout.rdbuf(original_buf);
    const std::string out = captured_output.str();

    if (!result.success) {
        std::cerr << "[FAIL] SELECT query failed: " << result.message << std::endl;
        return false;
    }

    if (out.find("diizinkan") == std::string::npos && out.find("READ") == std::string::npos) {
        std::cerr << "[FAIL] CCM did not validate READ access" << std::endl;
        return false;
    }

    if (out.find("mencatat akses") == std::string::npos) {
        std::cerr << "[FAIL] CCM did not log object access" << std::endl;
        return false;
    }

    // Test 3: Verify WRITE access validation for UPDATE
    std::cout << "  Testing WRITE access validation..." << std::endl;
    std::ostringstream captured_output2;
    original_buf = std::cout.rdbuf(captured_output2.rdbuf());
    
    // Use direct UPDATE call since query optimizer might not parse UPDATE strings yet
    mdbms::qo::ParsedQuery update_query;
    update_query.query_type = "UPDATE";
    update_query.from_tables.push_back("Student");
    update_query.set_values["GPA"] = 4.0f;
    update_query.where_conditions.push_back(mdbms::Condition("StudentID", "=", 1));
    
    int update_txn = setup.qp->begin_transaction();
    int affected = setup.qp->execute_update(update_query, update_txn);
    setup.qp->commit_transaction(update_txn);
    
    std::cout.rdbuf(original_buf);
    const std::string out2 = captured_output2.str();

    if (affected < 0) {
        std::cerr << "[FAIL] UPDATE query failed" << std::endl;
        return false;
    }

    if (out2.find("diizinkan") == std::string::npos && out2.find("WRITE") == std::string::npos) {
        std::cerr << "[FAIL] CCM did not validate WRITE access" << std::endl;
        return false;
    }

    // Test 4: Verify transaction commit through CCM
    std::cout << "  Testing transaction commit..." << std::endl;
    std::ostringstream captured_output3;
    original_buf = std::cout.rdbuf(captured_output3.rdbuf());
    
    bool committed = setup.qp->commit_transaction(txn_id);
    
    std::cout.rdbuf(original_buf);
    const std::string out3 = captured_output3.str();

    if (!committed) {
        std::cerr << "[FAIL] Transaction commit failed" << std::endl;
        return false;
    }

    if (out3.find("COMMITTED") == std::string::npos && out3.find("Mengakhiri") == std::string::npos) {
        std::cerr << "[FAIL] CCM did not end transaction on commit" << std::endl;
        return false;
    }

    // Test 5: Verify transaction abort through CCM
    std::cout << "  Testing transaction abort..." << std::endl;
    int txn_id2 = setup.qp->begin_transaction();
    std::ostringstream captured_output4;
    original_buf = std::cout.rdbuf(captured_output4.rdbuf());
    
    bool aborted = setup.qp->abort_transaction(txn_id2);
    
    std::cout.rdbuf(original_buf);
    const std::string out4 = captured_output4.str();

    if (!aborted) {
        std::cerr << "[FAIL] Transaction abort failed" << std::endl;
        return false;
    }

    if (out4.find("COMMITTED") == std::string::npos && out4.find("Mengakhiri") == std::string::npos) {
        std::cerr << "[FAIL] CCM did not end transaction on abort" << std::endl;
        return false;
    }

    std::cout << "[PASS] Concurrency Control Manager integration test" << std::endl;
    return true;
}

// ============================================================================
// Test 5: Full End-to-End Workflow
// ============================================================================
bool test_full_workflow() {
    std::cout << "\n=== TEST 5: Full End-to-End Workflow ===" << std::endl;

    IntegrationTestSetup setup("test_full_workflow");
    
    // Step 1: Begin transaction
    std::cout << "  Step 1: Begin transaction..." << std::endl;
    int txn_id = setup.qp->begin_transaction();
    if (txn_id < 0) {
        std::cerr << "[FAIL] Failed to begin transaction" << std::endl;
        return false;
    }

    // Step 2: Insert data via storage manager
    std::cout << "  Step 2: Insert test data..." << std::endl;
    setup.insert_student_direct(1, "Alice", 3.8f);
    setup.insert_student_direct(2, "Bob", 3.5f);
    setup.insert_student_direct(3, "Charlie", 3.2f);

    // Step 3: SELECT query (goes through QO -> CCM -> SM)
    std::cout << "  Step 3: Execute SELECT query..." << std::endl;
    std::string select_query = "SELECT StudentID, FullName FROM Student";
    auto select_result = setup.qp->execute_query(select_query);
    
    if (!select_result.success || select_result.affected_rows != 3) {
        std::cerr << "[FAIL] SELECT query failed or returned wrong number of rows" << std::endl;
        return false;
    }

    // Step 4: UPDATE query (goes through QO -> CCM -> SM)
    // Note: UPDATE parsing might not be fully implemented in QO, so we'll test via direct call
    std::cout << "  Step 4: Execute UPDATE query..." << std::endl;
    mdbms::qo::ParsedQuery update_query;
    update_query.query_type = "UPDATE";
    update_query.from_tables.push_back("Student");
    update_query.set_values["gpa"] = 4.0f;
    update_query.where_conditions.push_back(mdbms::Condition("id", "=", 1));
    
    int update_txn = setup.qp->begin_transaction();
    int affected = setup.qp->execute_update(update_query, update_txn);
    setup.qp->commit_transaction(update_txn);
    
    if (affected != 1) {
        std::cerr << "[FAIL] UPDATE query affected wrong number of rows: " << affected << std::endl;
        return false;
    }

    // Step 5: Verify UPDATE by SELECT
    std::cout << "  Step 5: Verify UPDATE result..." << std::endl;
    // Use direct SELECT call to verify
    mdbms::qo::ParsedQuery verify_query;
    verify_query.query_type = "SELECT";
    verify_query.select_columns = {"*"};
    verify_query.from_tables.push_back("Student");
    verify_query.where_conditions.push_back(mdbms::Condition("id", "=", 1));
    
    int verify_txn = setup.qp->begin_transaction();
    auto verify_result = setup.qp->execute_select(verify_query, verify_txn);
    setup.qp->commit_transaction(verify_txn);
    
    if (verify_result.rows_count < 1) {
        std::cerr << "[FAIL] Verification SELECT failed, got " << verify_result.rows_count << " rows" << std::endl;
        return false;
    }
    
    // Verify the GPA was actually updated
    if (!verify_result.data.empty()) {
        float gpa = std::any_cast<float>(verify_result.data[0].columns.at("gpa"));
        if (gpa != 4.0f) {
            std::cerr << "[FAIL] GPA not updated correctly, expected 4.0, got " << gpa << std::endl;
            return false;
        }
    }

    // Step 6: Commit transaction
    std::cout << "  Step 6: Commit transaction..." << std::endl;
    bool committed = setup.qp->commit_transaction(txn_id);
    if (!committed) {
        std::cerr << "[FAIL] Transaction commit failed" << std::endl;
        return false;
    }

    std::cout << "[PASS] Full end-to-end workflow test" << std::endl;
    return true;
}

// ============================================================================
// Test 6: Multiple Transactions
// ============================================================================
bool test_multiple_transactions() {
    std::cout << "\n=== TEST 6: Multiple Transactions ===" << std::endl;

    IntegrationTestSetup setup("test_multi_txn");
    setup.insert_student_direct(1, "Alice", 3.8f);
    setup.insert_student_direct(2, "Bob", 3.5f);

    // Start first transaction
    int txn1 = setup.qp->begin_transaction();
    if (txn1 < 0) {
        std::cerr << "[FAIL] Failed to begin first transaction" << std::endl;
        return false;
    }

    // Start second transaction
    int txn2 = setup.qp->begin_transaction();
    if (txn2 <= txn1) {
        std::cerr << "[FAIL] Second transaction ID should be greater than first" << std::endl;
        return false;
    }

    // Execute queries in both transactions
    auto result1 = setup.qp->execute_query("SELECT * FROM Student");
    auto result2 = setup.qp->execute_query("SELECT * FROM Student");

    if (!result1.success || !result2.success) {
        std::cerr << "[FAIL] Query execution failed in one of the transactions" << std::endl;
        return false;
    }

    // Commit both transactions
    bool committed1 = setup.qp->commit_transaction(txn1);
    bool committed2 = setup.qp->commit_transaction(txn2);

    if (!committed1 || !committed2) {
        std::cerr << "[FAIL] One or both transactions failed to commit" << std::endl;
        return false;
    }

    std::cout << "[PASS] Multiple transactions test" << std::endl;
    return true;
}

// ============================================================================
// Test 7: Failure Recovery Manager Integration
// ============================================================================
bool test_failure_recovery_integration() {
    std::cout << "\n=== TEST 7: Failure Recovery Manager Integration ===" << std::endl;

    IntegrationTestSetup setup("test_frm_integration");
    setup.insert_student_direct(1, "Alice", 3.8f);
    setup.insert_student_direct(2, "Bob", 3.5f);

    // Test 1: Verify logging for BEGIN transaction
    std::cout << "  Testing BEGIN transaction logging..." << std::endl;
    std::ostringstream captured_output;
    auto* original_buf = std::cout.rdbuf(captured_output.rdbuf());
    
    int txn_id = setup.qp->begin_transaction();
    
    std::cout.rdbuf(original_buf);
    const std::string out = captured_output.str();

    if (txn_id < 0) {
        std::cerr << "[FAIL] Failed to begin transaction" << std::endl;
        return false;
    }

    if (out.find("FRM: Menulis log") == std::string::npos && 
        out.find("FRM: Log ID") == std::string::npos) {
        std::cerr << "[FAIL] FRM did not log BEGIN transaction" << std::endl;
        return false;
    }

    // Test 2: Verify logging for SELECT query
    std::cout << "  Testing SELECT query logging..." << std::endl;
    std::ostringstream captured_output2;
    original_buf = std::cout.rdbuf(captured_output2.rdbuf());
    
    auto result = setup.qp->execute_query("SELECT * FROM Student");
    
    std::cout.rdbuf(original_buf);
    const std::string out2 = captured_output2.str();

    if (!result.success) {
        std::cerr << "[FAIL] SELECT query failed" << std::endl;
        return false;
    }

    if (out2.find("FRM: Menulis log") == std::string::npos && 
        out2.find("FRM: Log ID") == std::string::npos) {
        std::cerr << "[FAIL] FRM did not log SELECT query" << std::endl;
        return false;
    }

    // Test 3: Verify logging for UPDATE with old_value and new_value
    std::cout << "  Testing UPDATE query logging with old/new values..." << std::endl;
    std::ostringstream captured_output3;
    original_buf = std::cout.rdbuf(captured_output3.rdbuf());
    
    mdbms::qo::ParsedQuery update_query;
    update_query.query_type = "UPDATE";
    update_query.from_tables.push_back("Student");
    update_query.set_values["gpa"] = 4.0f;
    update_query.where_conditions.push_back(mdbms::Condition("id", "=", 1));
    
    int update_txn = setup.qp->begin_transaction();
    int affected = setup.qp->execute_update(update_query, update_txn);
    setup.qp->commit_transaction(update_txn);
    
    std::cout.rdbuf(original_buf);
    const std::string out3 = captured_output3.str();

    if (affected != 1) {
        std::cerr << "[FAIL] UPDATE affected wrong number of rows: " << affected << std::endl;
        return false;
    }

    if (out3.find("FRM: Menulis log") == std::string::npos && 
        out3.find("FRM: Log ID") == std::string::npos) {
        std::cerr << "[FAIL] FRM did not log UPDATE query" << std::endl;
        return false;
    }

    // Test 4: Verify logging for COMMIT
    std::cout << "  Testing COMMIT transaction logging..." << std::endl;
    int commit_txn = setup.qp->begin_transaction();
    std::ostringstream captured_output4;
    original_buf = std::cout.rdbuf(captured_output4.rdbuf());
    
    bool committed = setup.qp->commit_transaction(commit_txn);
    
    std::cout.rdbuf(original_buf);
    const std::string out4 = captured_output4.str();

    if (!committed) {
        std::cerr << "[FAIL] Transaction commit failed" << std::endl;
        return false;
    }

    if (out4.find("FRM: Menulis log") == std::string::npos && 
        out4.find("FRM: Log ID") == std::string::npos) {
        std::cerr << "[FAIL] FRM did not log COMMIT" << std::endl;
        return false;
    }

    // Test 5: Verify checkpoint is saved after commit
    if (out4.find("FRM: Menyimpan checkpoint") == std::string::npos &&
        out4.find("FRM: Checkpoint") == std::string::npos) {
        std::cerr << "[FAIL] FRM did not save checkpoint after commit" << std::endl;
        return false;
    }

    // Test 6: Verify logging for ABORT and recovery
    std::cout << "  Testing ABORT transaction and recovery..." << std::endl;
    int abort_txn = setup.qp->begin_transaction();
    
    // Do an UPDATE
    mdbms::qo::ParsedQuery update_query2;
    update_query2.query_type = "UPDATE";
    update_query2.from_tables.push_back("Student");
    update_query2.set_values["gpa"] = 3.9f;
    update_query2.where_conditions.push_back(mdbms::Condition("id", "=", 2));
    setup.qp->execute_update(update_query2, abort_txn);
    
    std::ostringstream captured_output5;
    original_buf = std::cout.rdbuf(captured_output5.rdbuf());
    
    bool aborted = setup.qp->abort_transaction(abort_txn);
    
    std::cout.rdbuf(original_buf);
    const std::string out5 = captured_output5.str();

    if (!aborted) {
        std::cerr << "[FAIL] Transaction abort failed" << std::endl;
        return false;
    }

    if (out5.find("FRM: Melakukan recovery") == std::string::npos &&
        out5.find("FRM: Memulai proses recovery") == std::string::npos) {
        std::cerr << "[FAIL] FRM did not perform recovery on abort" << std::endl;
        return false;
    }

    if (out5.find("FRM: Menulis log") == std::string::npos && 
        out5.find("FRM: Log ID") == std::string::npos) {
        std::cerr << "[FAIL] FRM did not log ABORT" << std::endl;
        return false;
    }

    std::cout << "[PASS] Failure Recovery Manager integration test" << std::endl;
    return true;
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Full Integration Tests" << std::endl;
    std::cout << "  Query Processor + All Components" << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int total = 7;

    if (test_basic_integration()) passed++;
    if (test_query_optimizer_integration()) passed++;
    if (test_storage_manager_integration()) passed++;
    if (test_concurrency_control_integration()) passed++;
    if (test_full_workflow()) passed++;
    if (test_multiple_transactions()) passed++;
    if (test_failure_recovery_integration()) passed++;

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << passed << "/" << total << " tests passed" << std::endl;
    std::cout << "========================================" << std::endl;

    return (passed == total) ? 0 : 1;
}
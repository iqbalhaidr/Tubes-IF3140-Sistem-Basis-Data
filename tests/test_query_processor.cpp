#include "query_optimizer.h"
#include "query_processor.h"
#include "storage_manager.h"

#include <iostream>
#include <memory>

int main() {
    auto optimizer = std::make_shared<mdbms::qo::OptimizationEngine>();
    auto storage = std::make_shared<mdbms::sm::StorageEngine>();
    mdbms::qp::QueryProcessor query_processor(optimizer, storage);

    const std::string query = "SELECT * FROM integration_test";
    const auto result = query_processor.execute_query(query);

    bool passed = true;

    if (!result.success) {
        std::cerr << "[FAIL] Query execution marked as failed\n";
        passed = false;
    }

    if (result.query != query) {
        std::cerr << "[FAIL] Result query mismatch\n";
        passed = false;
    }

    if (result.transaction_id < 0) {
        std::cerr << "[FAIL] Transaction ID not assigned\n";
        passed = false;
    }

    if (result.message.find("Retrieved") == std::string::npos) {
        std::cerr << "[FAIL] Unexpected execution message: " << result.message << "\n";
        passed = false;
    }

    if (result.data.rows_count != 0) {
        std::cerr << "[FAIL] Expected no rows from empty storage but got " << result.data.rows_count << "\n";
        passed = false;
    }

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] QueryProcessor integrates with OptimizationEngine stub\n";
    return 0;
}

#include "query_optimizer.h"
#include "query_processor.h"
#include "storage_manager.h"

#include <iostream>
#include <memory>

int main() {
    auto optimizer = std::make_shared<mdbms::qo::OptimizationEngine>();
    auto storage = std::make_shared<mdbms::sm::StorageEngine>("data");

    mdbms::qp::QueryProcessor query_processor(optimizer, storage);

    const std::string query = "SELECT name FROM integration_stub";
    const auto result = query_processor.execute_query(query);

    bool passed = true;

    if (!result.success) {
        std::cerr << "[FAIL] QueryProcessor reported failure\n";
        passed = false;
    }

    if (result.query != query) {
        std::cerr << "[FAIL] Query string mismatch\n";
        passed = false;
    }

    if (result.data.rows_count != 0) {
        std::cerr << "[FAIL] Expected stub storage to return 0 rows, got " << result.data.rows_count << "\n";
        passed = false;
    }

    if (result.message.find("Retrieved") == std::string::npos) {
        std::cerr << "[FAIL] Missing retrieval message: " << result.message << "\n";
        passed = false;
    }

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] QueryProcessor/Optimizer/Storage stub integration\n";
    return 0;
}

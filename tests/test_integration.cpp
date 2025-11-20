#include "query_optimizer.h"
#include "query_processor.h"
#include "storage_manager.h"
#include "failure_recovery.h"

#include <iostream>
#include <memory>
#include <sstream>

int main() {
    auto optimizer = std::make_shared<mdbms::qo::OptimizationEngine>();
    auto storage = std::make_shared<mdbms::sm::StorageEngine>();

    mdbms::qp::QueryProcessor query_processor(optimizer, storage, nullptr);

    const std::string query = "SELECT name FROM integration_stub";
    std::ostringstream captured_output;
    auto* original_buf = std::cout.rdbuf(captured_output.rdbuf());
    const auto result = query_processor.execute_query(query);
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

    if (result.data.rows_count != 0) {
        std::cerr << "[FAIL] Expected stub storage to return 0 rows, got " << result.data.rows_count << "\n";
        passed = false;
    }

    if (result.message.find("Retrieved") == std::string::npos) {
        std::cerr << "[FAIL] Missing retrieval message: " << result.message << "\n";
        passed = false;
    }

    const std::string out = captured_output.str();
    if (out.find("CCM: Memulai transaksi") == std::string::npos) {
        std::cerr << "[FAIL] Concurrency manager did not start transaction\n";
        passed = false;
    }
    if (out.find("CCM: Memvalidasi objek") == std::string::npos) {
        std::cerr << "[FAIL] Concurrency manager did not validate object\n";
        passed = false;
    }
    if (out.find("CCM: Logging objek") == std::string::npos) {
        std::cerr << "[FAIL] Concurrency manager did not log object\n";
        passed = false;
    }
    if (out.find("CCM: Mengakhiri transaksi") == std::string::npos) {
        std::cerr << "[FAIL] Concurrency manager did not end transaction\n";
        passed = false;
    }

    if (out.find("FRM: Menulis log") == std::string::npos) {
        std::cerr << "[FAIL] Failure recovery manager did not log operations\n";
        passed = false;
    }

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] QueryProcessor/Optimizer/Storage/CCM/FRM stub integration\n";
    return 0;
}

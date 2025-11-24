#include <iostream>
#include <memory>

#include "storage_manager.h"
#include "query_optimizer.h"
#include "query_processor.h"

int main() {
    // Use singleton instances
    mdbms::qp::QueryProcessor query_processor();

    std::cout << "Main: Query Processor and Optimization Engine initialized." << std::endl;
    return 0;
}

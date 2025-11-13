#include <iostream>
#include <memory>

#include "storage_manager.h"
#include "query_optimizer.h"
#include "query_processor.h"

int main() {
    auto optimization_engine = std::make_shared<mdbms::qo::OptimizationEngine>();
    auto storage_engine = std::make_shared<mdbms::sm::StorageEngine>("data");
    mdbms::qp::QueryProcessor query_processor(optimization_engine, storage_engine);

    std::cout << "Main: Query Processor and Optimization Engine initialized." << std::endl;
    return 0;
}

#include <iostream>
#include <memory>

#include "query_optimizer.h"
#include "query_processor.h"

int main() {
    auto optimization_engine = std::make_shared<mdbms::qo::OptimizationEngine>();
    mdbms::qp::QueryProcessor query_processor(optimization_engine);

    std::cout << "Main: Query Processor and Optimization Engine initialized." << std::endl;
    return 0;
}

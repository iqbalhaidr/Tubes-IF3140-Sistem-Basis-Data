#include "query_processor.h"
#include <iostream>
namespace mdbms::qp {

ExecutionResult QueryProcessor::execute_query(const std::string& query) {
    std::cout << "QP: Menerima query: " << query << std::endl;
    
    ExecutionResult res;
    res.transaction_id = 1;
    res.query = query;
    res.message = "Query berhasil dieksekusi.";
    return res;
}

} // namespace mdbms::qp
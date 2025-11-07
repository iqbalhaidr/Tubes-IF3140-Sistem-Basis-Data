#pragma once
#include <string>

// Definisikan ParsedQuery di sini atau di types.h jika dipakai bersama

namespace mdbms::qo {

// Struct sementara untuk ParsedQuery
struct ParsedQuery {
    std::string query;
    // ... detail lain (misal: query tree)
};

class OptimizationEngine {
public:
    ParsedQuery parse_query(const std::string& query);
    ParsedQuery optimize_query(const ParsedQuery& query);
};

} // namespace mdbms::qo

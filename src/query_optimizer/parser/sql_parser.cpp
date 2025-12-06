#include "query_optimizer.h"

#include "ddl/ddl_parser.h"
#include "dml/dml_parser.h"
#include "dml/select_parser.h"
#include "utils/string_utils.h"

namespace mdbms::qo {
namespace {

std::string detect_query_type(const std::string& normalized_query) {
    if (normalized_query.empty()) {
        return {};
    }

    std::string upper = to_upper(normalized_query);
    if (upper.rfind("SELECT", 0) == 0) {
        return "SELECT";
    }
    if (upper.rfind("INSERT", 0) == 0) {
        return "INSERT";
    }
    if (upper.rfind("UPDATE", 0) == 0) {
        return "UPDATE";
    }
    if (upper.rfind("DELETE", 0) == 0) {
        return "DELETE";
    }
    if (upper.rfind("BEGIN", 0) == 0) {
        return "BEGIN";
    }
    if (upper.rfind("COMMIT", 0) == 0) {
        return "COMMIT";
    }
    if (upper.rfind("ROLLBACK", 0) == 0) {
        return "ROLLBACK";
    }
    if (upper.rfind("CREATE", 0) == 0) {
        return "CREATE";
    }
    if (upper.rfind("DROP", 0) == 0) {
        return "DROP";
    }
    return {};
}

}  // namespace

ParsedQuery sql_parser(const std::string& query) {
    ParsedQuery pq;
    pq.original_query = trim(query);
    if (!pq.original_query.empty() && pq.original_query.back() == ';') {
        pq.original_query.pop_back();
    }
    pq.query_type = detect_query_type(pq.original_query);

    if (pq.query_type == "SELECT") {
        parse_select_query(pq.original_query, pq);
    } else if (pq.query_type == "UPDATE") {
        parse_update_query(pq.original_query, pq);
    } else if (pq.query_type == "INSERT") {
        parse_insert_query(pq.original_query, pq);
    } else if (pq.query_type == "DELETE") {
        parse_delete_query(pq.original_query, pq);
    } else if (pq.query_type == "CREATE") {
        parse_create_table(pq.original_query, pq);
    } else if (pq.query_type == "DROP") {
        parse_drop_table(pq.original_query, pq);
    } else if (pq.query_type == "BEGIN" || pq.query_type == "COMMIT" ||
               pq.query_type == "ROLLBACK") {
    }

    return pq;
}

}  // namespace mdbms::qo

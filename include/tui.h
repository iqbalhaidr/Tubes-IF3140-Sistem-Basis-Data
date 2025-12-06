#pragma once

#include "query_processor.h"
#include "types.h"
#include <string>
#include <vector>

namespace mdbms::qp { class QueryProcessor; }

namespace mdbms::tui {

class TUI {
public:
    TUI();
    ~TUI() = default;

    void run();
    void handle_sql_command(const std::string& query, mdbms::qp::QueryProcessor& query_processor);
    void display_results(const Rows<Row>& rows);
    void display_execution_result(const ExecutionResult& result);
    void print_separator(int width);
    std::string trim(const std::string& str);
};

} // namespace mdbms::tui


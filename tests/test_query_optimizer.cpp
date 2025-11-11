#include "query_optimizer.h"
#include "query_tree.h"
#include "sql_parser.h"
#include <iostream>

// cmake --build build --target test_query_optimizer 
// ./build/src/test_query_optimizer

namespace {

template <typename Collection, typename Formatter>

void print_list(const std::string& label, const Collection& list, Formatter formatter) {
    std::cout << label << " -> [";
    bool first = true;
    for (const auto& item : list) {
        if (!first) {
            std::cout << ", ";
        }
        formatter(item);
        first = false;
    }
    std::cout << "]\n";
}

void print_segments(const mdbms::qo::PlanSegments& segments) {
    print_list("SELECT", segments.select_list, [](const auto& s) { std::cout << s; });
    print_list("FROM", segments.from_tables, [](const auto& s) { std::cout << s; });
    print_list("JOIN", segments.joins, [](const auto& join) {
        std::cout << join.left << " = " << join.right;
    });
    print_list("WHERE", segments.where_conditions, [](const auto& s) { std::cout << s; });
}

} // namespace

int main() {
    mdbms::qo::OptimizationEngine opt;

    const std::string query =
        "SELECT s.name, d.nama FROM student s JOIN dept d ON s.dept_id = d.id JOIN apt a ON a_id = s.dept_id WHERE s.age > 20 AND d.size >= 10 AND a.name = 'bebek' s.name";


    // debug plan segment
    const auto segments = mdbms::qo::parse_plan_segments(query);

    std::cout << "query awal:\n" << query << "\n\n";

    std::cout << "Parsed segments:\n";
    print_segments(segments);
    std::cout << '\n';


    // fungsi publik parse_query
    auto pq = opt.parse_query(query);


    std::cout << "bentukan QueryTree:\n";
    if (!pq.query_tree) {
        std::cout << "query tree masi null\n";
    }


    return 0;
}

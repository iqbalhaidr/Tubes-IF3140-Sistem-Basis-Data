#pragma once
#include <map>
#include <string>
#include <vector>

#include "types.h"

namespace mdbms::qo {
    struct QueryTree {
        std::string type;                  // Tipe operasi: SELECT, PROJECT, JOIN, SCAN, etc.
        std::string value;                 // Parameter (nama tabel, kolom, kondisi, dll)
        std::vector<QueryTree*> children;  // Child nodes (sub-expressions)
        QueryTree* parent;                 // Parent node

        // Additional metadata untuk optimization
        int estimated_rows;  // Estimasi jumlah baris output
        int estimated_cost;  // Estimasi cost operasi ini

        QueryTree() : parent(nullptr), estimated_rows(0), estimated_cost(0) {
        }

        ~QueryTree() {
            for (auto child : children) {
                delete child;
            }
        }

        void add_child(QueryTree* child) {
            children.push_back(child);
            child->parent = this;
        }

        QueryTree* clone() const {
            QueryTree* new_tree = new QueryTree();
            new_tree->type = type;
            new_tree->value = value;
            new_tree->estimated_rows = estimated_rows;
            new_tree->estimated_cost = estimated_cost;

            for (auto child : children) {
                new_tree->add_child(child->clone());
            }

            return new_tree;
        }
    };

    struct ParsedQuery {
        std::string original_query;
        std::string query_type;
        QueryTree* query_tree;
        int estimated_cost;

        std::vector<std::string> select_columns;
        std::vector<std::string> from_tables;
        std::vector<Condition> where_conditions;
        std::vector<std::pair<std::string, std::string>> join_pairs;  // Join conditions
        std::string order_by_column;                                  // Kolom untuk ORDER BY
        bool order_ascending;                                         // ASC atau DESC
        int limit_value;                                              // Nilai LIMIT

        // For UPDATE
        std::map<std::string, std::any> set_values;  // SET column = value

        // For INSERT
        std::vector<std::any> insert_values;  // VALUES (...)

        ParsedQuery()
            : query_tree(nullptr), estimated_cost(0), order_ascending(true), limit_value(-1) {
        }

        ~ParsedQuery() {
            if (query_tree) {
                delete query_tree;
            }
        }
    };

    class OptimizationEngine {
       public:
        ParsedQuery parse_query(const std::string& query);
        ParsedQuery optimize_query(const ParsedQuery& query);
    };

}  // namespace mdbms::qo

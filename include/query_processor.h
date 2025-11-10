#pragma once

#include <string>
#include <memory>
#include "types.h"

namespace mdbms::qo { class OptimizationEngine; }
namespace mdbms::sm { class StorageEngine; }
namespace mdbms::ccm { class ConcurrencyControlManager; }
namespace mdbms::fr { class FailureRecoveryManager; }

namespace mdbms::qp {

using QueryTreePtr = std::shared_ptr<QueryTree>;

class QueryProcessor {
public:
    QueryProcessor(
        qo::OptimizationEngine& qo,
        sm::StorageEngine& sm,
        ccm::ConcurrencyControlManager& ccm,
        fr::FailureRecoveryManager& frm
    );

    ExecutionResult execute_query(const std::string& query);

private:
    qo::OptimizationEngine& qo_engine_;
    sm::StorageEngine& sm_engine_;
    ccm::ConcurrencyControlManager& ccm_manager_;
    fr::FailureRecoveryManager& frm_manager_;

    ExecutionResult execute_plan(const ParsedQuery& plan, int transaction_id);

    ExecutionResult handle_select(QueryTreePtr select_node, int tx_id);
    
    ExecutionResult handle_update(QueryTreePtr update_node, int tx_id);

    ExecutionResult handle_join(QueryTreePtr join_node, int tx_id);

    ExecutionResult handle_delete(QueryTreePtr delete_node, int tx_id);

    ExecutionResult handle_insert(QueryTreePtr insert_node, int tx_id);

    ExecutionResult handle_create(QueryTreePtr create_node, int tx_id);

    ExecutionResult handle_drop(QueryTreePtr drop_node, int tx_id);

    std::vector<Condition> parse_conditions_from_tree(QueryTreePtr where_node);

    void apply_projection(Rows& rows, QueryTreePtr column_list_node);

    void apply_order_by(Rows& rows, QueryTreePtr order_by_node);

    // Helper methods for extracting information from query tree
    std::vector<std::string> extract_columns_from_list(QueryTreePtr column_list_node);
    
    std::string extract_query_action(QueryTreePtr query_tree);
    
    std::any convert_operand(const std::string& value, const std::string& type = "string");
    
    std::any evaluate_expression(QueryTreePtr expr_node, const std::vector<std::any>& row, 
                                  const std::vector<std::string>& column_names);
    
    int find_column_index(const std::string& column_name, const std::vector<std::string>& column_names);
    
    Rows perform_cartesian_product(const Rows& left, const Rows& right,
                                   const std::vector<std::string>& left_cols,
                                   const std::vector<std::string>& right_cols);

    bool compare_values(const std::any& left, const std::any& right, const std::string& op);
};

} // namespace mdbms::qp
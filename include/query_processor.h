#pragma once
#include <memory>

#include "types.h"

namespace mdbms::qo { class OptimizationEngine; struct ParsedQuery; struct QueryTree; }
namespace mdbms::sm { class StorageEngine; }
namespace mdbms::ccm { class ConcurrencyControlManager; }
namespace mdbms::fr { class FailureRecoveryManager; }

namespace mdbms::qp {

class QueryProcessor {
public:
    explicit QueryProcessor(std::shared_ptr<mdbms::qo::OptimizationEngine> optimizer = nullptr,
                            std::shared_ptr<mdbms::sm::StorageEngine> storage = nullptr,
                            std::shared_ptr<mdbms::ccm::ConcurrencyControlManager> concurrency = nullptr,
                            std::shared_ptr<mdbms::fr::FailureRecoveryManager> recovery = nullptr);

    /**
    * Todo:
     * 1. Parse dan optimize query melalui Query Optimizer
     * 2. Identifikasi jenis operasi (SELECT, UPDATE, INSERT, DELETE, dll)
     * 3. Untuk operasi data (read/write):
     *    - Minta izin dari Concurrency Control Manager
     *    - Tunggu hingga response diterima (blocking)
     *    - Jika tidak diizinkan (deadlock/conflict), batalkan transaksi
     * 4. Eksekusi operasi melalui Storage Manager:
     *    - SELECT: ambil data dengan read_block()
     *    - UPDATE/DELETE: modifikasi data dengan write_block()/delete_block()
     *    - JOIN: lakukan algoritma join sesuai query plan
     * 5. Log operasi ke Failure Recovery Manager
     * 6. Kembalikan hasil dalam bentuk ExecutionResult
    */
    ExecutionResult execute_query(const std::string& query);

    // Memanggil Concurrency Control Manager untuk mendapatkan transaction ID dan Failure Recovery Manager untuk mencatat BEGIN.
    int begin_transaction(); 

    // Menyelesaikan transaksi dengan sukses. Semua perubahan akan di-commit ke disk melalui Failure Recovery Manager.
    bool commit_transaction(int transaction_id);

    // Rollback semua perubahan yang dilakukan oleh transaksi. Meminta Failure Recovery Manager untuk melakukan UNDO.
    bool abort_transaction(int transaction_id);

    // Method tiap query
    Rows<Row> execute_select(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id);
    int execute_update(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id);
    int execute_insert(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id);
    int execute_delete(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id);
    bool execute_create_table(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id);
    bool execute_drop_table(const mdbms::qo::ParsedQuery& parsed_query, int transaction_id);
    Rows<Row> execute_join(const Rows<Row>& left_table, const Rows<Row>& right_table, const Condition& join_condition, const std::string& join_type);
    Rows<Row> apply_where_clause(const Rows<Row>& rows,const std::vector<Condition>& conditions);
    Rows<Row> apply_order_by(const Rows<Row>& rows, const std::string& column, bool ascending);
    Rows<Row> apply_limit(const Rows<Row>& rows, int limit);
private:
    mdbms::qo::OptimizationEngine* qo_engine;  // Singleton - non-owning pointer
    mdbms::sm::StorageEngine* sm_engine;  // Singleton - non-owning pointer
    mdbms::ccm::ConcurrencyControlManager* ccm_manager;  // Singleton - non-owning pointer
    mdbms::fr::FailureRecoveryManager* frm_manager;  // Singleton - non-owning pointer

    int current_transaction_id;

    std::string parse_query_type(const std::string& query);
};

} // namespace mdbms::qp
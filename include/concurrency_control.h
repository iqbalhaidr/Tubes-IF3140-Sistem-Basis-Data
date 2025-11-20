#pragma once
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include "types.h"

namespace mdbms::ccm {

// Struktur informasi transaksi
struct Transaction {
    int id;
    int timestamp;
    TransactionStatus state;

    Transaction(int tid, int ts) : id(tid), timestamp(ts), state(TransactionStatus::ACTIVE) {}
};

// Informasi timestamp untuk setiap objek (record)
struct ObjectTimestamp {
    int read_ts;   // Timestamp terbaru dari transaksi yang membaca objek ini
    int write_ts;  // Timestamp terbaru dari transaksi yang menulis objek ini

    ObjectTimestamp() : read_ts(0), write_ts(0) {}
};

// Abstract Base Class (ABC) untuk protokol concurrency control
class CCManager {
public:
    virtual ~CCManager() = default;

    virtual int begin_transaction() = 0;
    virtual void log_object(const Row& object, int transaction_id) = 0;
    virtual Response validate_object(const Row& object, int transaction_id, Action action) = 0;
    virtual void end_transaction(int transaction_id) = 0;

protected:
    static size_t generate_row_hash(const Row& row);
};

// Implementasi Protokol Timestamp Ordering
class TimestampCCManager : public CCManager {
public:
    TimestampCCManager();
    ~TimestampCCManager() override;

    int begin_transaction() override;
    void log_object(const Row& object, int transaction_id) override;
    Response validate_object(const Row& object, int transaction_id, Action action) override;
    void end_transaction(int transaction_id) override;

private:
    std::map<int, std::shared_ptr<Transaction>> transactions_;
    std::map<size_t, ObjectTimestamp> object_timestamps_;
    int current_timestamp_;
    std::mutex mutex_;
};

// ini kayanya bakal dipindah ke types.h (tapi yah sama aja)
enum class TransactionPhase {
    GROWING,
    SHRINKING
};

struct TransactionInfo2PL {
    int id;
    TransactionStatus status;
    TransactionPhase phase;
    std::set<size_t> locked_records;  // Hash dari record yang dikunci

    TransactionInfo2PL(int tid)
        : id(tid), status(TransactionStatus::ACTIVE), phase(TransactionPhase::GROWING) {}
};

class TwoPhaseLockingCCManager : public CCManager {
public:
    TwoPhaseLockingCCManager();
    ~TwoPhaseLockingCCManager() override;

    int begin_transaction() override;
    void log_object(const Row& object, int transaction_id) override;
    Response validate_object(const Row& object, int transaction_id, Action action) override;
    void end_transaction(int transaction_id) override;

private:
    bool acquire_s_lock(int transaction_id, size_t record_hash);
    bool acquire_x_lock(int transaction_id, size_t record_hash);
    bool check_cycle(int current_id, std::set<int>& visited, std::set<int>& rec_stack);
    bool detect_deadlock(int waiting_trx_id, int holding_trx_id);
    void abort_transaction(int transaction_id);
    void release_s_lock(int transaction_id, size_t record_hash);
    void release_x_lock(int transaction_id, size_t record_hash);
    void release_all_locks(int transaction_id);

    // map <record_hash -> transaction ID> untuk xlock
    std::map<size_t, int> x_lock_table;

    // map <record_hash -> set of transaction ID> untuk slock
    std::map<size_t, std::set<int>> s_lock_table;

    // map <transaction ID -> info transaksi dalam protokol 2PL>
    std::map<int, std::shared_ptr<TransactionInfo2PL>> transactions;

    // map <transaction ID -> set of transaction ID> untuk wait-for graph (deteksi deadlock)
    std::map<int, std::set<int>> wait_for_graph;

    int current_transaction_id;
    std::recursive_mutex mutex_;
};

// Kelas wrapper yang mengelola protokol concurrency control aktif (Singleton)
class ConcurrencyControlManager {
public:
    // Menghapus copy constructor dan assignment operator untuk Singleton
    ConcurrencyControlManager(const ConcurrencyControlManager&) = delete;
    ConcurrencyControlManager& operator=(const ConcurrencyControlManager&) = delete;
    ~ConcurrencyControlManager();

    // Method untuk mendapatkan instance singleton
    static ConcurrencyControlManager& get_instance(const std::string& algorithm = "timestamp");

    void switch_algorithm(const std::string& algorithm);

    int begin_transaction();
    void log_object(const Row& object, int transaction_id);
    Response validate_object(const Row& object, int transaction_id, Action action);
    void end_transaction(int transaction_id);

private:
    // Constructor private untuk Singleton
    explicit ConcurrencyControlManager(const std::string& algorithm = "timestamp");

    std::unique_ptr<CCManager> cc_manager_;
    std::string current_algorithm_;
    static std::unique_ptr<ConcurrencyControlManager> instance_;
    static std::mutex instance_mutex_;
};

}  // namespace mdbms::ccm
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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

// MVCC: Multi-Version Concurrency Control
struct ObjectVersion {
    int version;
    int write_ts;
    int read_ts;
    Row data;
};

class MVCCManager : public CCManager {
public:
    MVCCManager();
    ~MVCCManager() override;

    int begin_transaction() override;
    void log_object(const Row& object, int transaction_id) override;
    Response validate_object(const Row& object, int transaction_id, Action action) override;
    void end_transaction(int transaction_id) override;
    void abort_transaction(int transaction_id);

private:
    std::unordered_map<size_t, std::vector<ObjectVersion>> object_versions_;
    std::unordered_map<int, std::shared_ptr<Transaction>> transactions_;
    int current_timestamp_;
    std::mutex mutex_;
    std::unordered_map<int, std::vector<int>> read_dependency_;

    ObjectVersion* find_latest_version(size_t row_hash, int trans_ts);
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
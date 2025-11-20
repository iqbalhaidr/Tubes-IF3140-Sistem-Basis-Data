#include <algorithm>
#include <iostream>

#include "concurrency_control.h"

namespace mdbms::ccm {

// Implementasi Timestamp Based Protocol
TimestampCCManager::TimestampCCManager() : current_timestamp_(0) {
    std::cout << "CCM: Protokol Timestamp Ordering diinisialisasi" << std::endl;
}

TimestampCCManager::~TimestampCCManager() {
    transactions_.clear();
    object_timestamps_.clear();
}

int TimestampCCManager::begin_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);

    current_timestamp_++;
    int transaction_id = current_timestamp_;

    auto transaction = std::make_shared<Transaction>(transaction_id, current_timestamp_);
    transactions_[transaction_id] = transaction;

    std::cout << "CCM: Transaksi " << transaction_id << " dimulai dengan timestamp "
              << current_timestamp_ << std::endl;

    return transaction_id;
}

void TimestampCCManager::log_object(const Row& object, int transaction_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (transactions_.find(transaction_id) == transactions_.end()) {
        throw std::runtime_error("Transaksi " + std::to_string(transaction_id) +
                                 " tidak ditemukan");
    }

    // Generate hash untuk baris
    size_t row_hash = generate_row_hash(object);

    // Inisialisasi entry timestamp jika belum ada
    if (object_timestamps_.find(row_hash) == object_timestamps_.end()) {
        object_timestamps_[row_hash] = ObjectTimestamp();
    }

    std::cout << "CCM: Transaksi " << transaction_id
              << " mencatat akses ke objek (hash: " << row_hash << ") pada tabel "
              << object.table_name << std::endl;
}

Response TimestampCCManager::validate_object(const Row& object, int transaction_id, Action action) {
    if (transactions_.find(transaction_id) == transactions_.end()) {
        throw std::runtime_error("Transaksi " + std::to_string(transaction_id) +
                                 " tidak ditemukan");
    }

    auto transaction = transactions_[transaction_id];
    size_t row_hash = generate_row_hash(object);

    // Pastikan objek sudah di-log
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (object_timestamps_.find(row_hash) == object_timestamps_.end()) {
            object_timestamps_[row_hash] = ObjectTimestamp();
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ObjectTimestamp& ts = object_timestamps_[row_hash];

    if (action == Action::READ) {
        // Operasi READ: TS(T) < write_ts(X) -> ABORT
        if (transaction->timestamp < ts.write_ts) {
            std::cout << "CCM: Transaksi " << transaction_id
                      << " GAGAL karena konflik timestamp pada READ (TS=" << transaction->timestamp
                      << " < write_ts=" << ts.write_ts << ")" << std::endl;

            transaction->state = TransactionStatus::FAILED;
            return Response(false, transaction_id);
        }

        // Update read timestamp
        ts.read_ts = std::max(ts.read_ts, transaction->timestamp);

        std::cout << "CCM: Transaksi " << transaction_id
                  << " diizinkan READ pada objek (hash: " << row_hash << "), read_ts diupdate ke "
                  << ts.read_ts << std::endl;
    } else if (action == Action::WRITE) {
        // Operasi WRITE: TS(T) < read_ts(X) ATAU TS(T) < write_ts(X) -> ABORT
        if (transaction->timestamp < ts.read_ts || transaction->timestamp < ts.write_ts) {
            std::cout << "CCM: Transaksi " << transaction_id
                      << " GAGAL karena konflik timestamp pada WRITE (TS=" << transaction->timestamp
                      << ", read_ts=" << ts.read_ts << ", write_ts=" << ts.write_ts << ")"
                      << std::endl;

            transaction->state = TransactionStatus::FAILED;
            return Response(false, transaction_id);
        }

        // Update write timestamp
        ts.write_ts = transaction->timestamp;

        std::cout << "CCM: Transaksi " << transaction_id
                  << " diizinkan WRITE pada objek (hash: " << row_hash << "), write_ts diupdate ke "
                  << ts.write_ts << std::endl;
    }

    return Response(true, transaction_id);
}

}  // namespace mdbms::ccm

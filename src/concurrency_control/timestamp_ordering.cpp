
#include <algorithm>
#include <iostream>
#include <thread>

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
    // Error handling: jika transaksi tidak ditemukan, log dan return Response gagal
    if (transactions_.find(transaction_id) == transactions_.end()) {
        std::cerr << "[ERROR] Transaksi " << transaction_id << " tidak ditemukan pada validate_object" << std::endl;
        return Response(false, transaction_id);
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

    // Starvation mitigation: retry otomatis jika gagal karena konflik timestamp
    int retry_count = 0;
    const int max_retries = 3;
    const int retry_delay_ms = 10;
    while (retry_count <= max_retries) {
        bool allowed = true;
        if (action == Action::READ) {
            if (transaction->timestamp < ts.write_ts) {
                allowed = false;
            }
        } else if (action == Action::WRITE) {
            if (transaction->timestamp < ts.read_ts || transaction->timestamp < ts.write_ts) {
                allowed = false;
            }
        }

        if (allowed) {
            if (action == Action::READ) {
                ts.read_ts = std::max(ts.read_ts, transaction->timestamp);
                std::cout << "CCM: Transaksi " << transaction_id
                    << " diizinkan READ pada objek (hash: " << row_hash << "), read_ts diupdate ke "
                    << ts.read_ts << std::endl;
            } else if (action == Action::WRITE) {
                ts.write_ts = transaction->timestamp;
                std::cout << "CCM: Transaksi " << transaction_id
                    << " diizinkan WRITE pada objek (hash: " << row_hash << "), write_ts diupdate ke "
                    << ts.write_ts << std::endl;
            }
            return Response(true, transaction_id);
        } else {
            retry_count++;
            if (retry_count > max_retries) {
                if (action == Action::READ) {
                    std::cout << "CCM: Transaksi " << transaction_id
                        << " GAGAL karena konflik timestamp pada READ (TS=" << transaction->timestamp
                        << " < write_ts=" << ts.write_ts << ")" << std::endl;
                } else {
                    std::cout << "CCM: Transaksi " << transaction_id
                        << " GAGAL karena konflik timestamp pada WRITE (TS=" << transaction->timestamp
                        << ", read_ts=" << ts.read_ts << ", write_ts=" << ts.write_ts << ")"
                        << std::endl;
                }
                transaction->state = TransactionStatus::FAILED;
                return Response(false, transaction_id);
            }
            // Starvation mitigation: delay sebelum retry
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms * retry_count));
        }
    }
    // Fallback error
    std::cerr << "[ERROR] validate_object fallback error" << std::endl;
    return Response(false, transaction_id);
}
void TimestampCCManager::end_transaction(int transaction_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (transactions_.find(transaction_id) == transactions_.end()) {
        std::cerr << "[ERROR] Transaksi " << transaction_id << " tidak ditemukan pada end_transaction" << std::endl;
        return;
    }

    auto transaction = transactions_[transaction_id];

    // Transisi state transaksi berdasarkan properti ACID:
    // ACTIVE -> PARTIALLY_COMMITTED -> COMMITTED (jalur sukses)
    // ACTIVE -> FAILED -> ABORTED -> TERMINATED (jalur gagal)

    if (transaction->state == TransactionStatus::FAILED) {
        std::cout << "CCM: Transaksi " << transaction_id
                  << " melakukan rollback (FAILED -> ABORTED)" << std::endl;
        transaction->state = TransactionStatus::ABORTED;

        // TODO: Lakukan rollback sebenarnya (koordinasi dengan Failure Recovery Manager)
        // Untuk saat ini, hanya transisi ke TERMINATED
        std::cout << "CCM: Transaksi " << transaction_id << " dihentikan (rollback selesai)"
                  << std::endl;
        transaction->state = TransactionStatus::TERMINATED;
    } else if (transaction->state == TransactionStatus::ABORTED) {
        std::cout << "CCM: Transaksi " << transaction_id << " dihentikan (sudah ABORTED)"
                  << std::endl;
        transaction->state = TransactionStatus::TERMINATED;
    } else if (transaction->state == TransactionStatus::ACTIVE) {
        std::cout << "CCM: Transaksi " << transaction_id << " memasuki state PARTIALLY_COMMITTED"
                  << std::endl;
        transaction->state = TransactionStatus::PARTIALLY_COMMITTED;

        // TODO: Lakukan flush perubahan ke stable storage melalui Failure Recovery Manager

        // Setelah berhasil flush ke stable storage:
        std::cout << "CCM: Transaksi " << transaction_id << " COMMITTED dengan sukses" << std::endl;
        transaction->state = TransactionStatus::COMMITTED;

        // Transaksi sekarang durable, bisa di-terminate
        transaction->state = TransactionStatus::TERMINATED;
    } else {
        std::cout << "CCM: Transaksi " << transaction_id << " sudah dalam state terminal"
                  << std::endl;
        transaction->state = TransactionStatus::TERMINATED;
    }

    // Resource cleanup: log dan hapus transaksi
    std::cout << "CCM: Cleanup resource transaksi " << transaction_id << std::endl;
    transactions_.erase(transaction_id);
}

}  // namespace mdbms::ccm

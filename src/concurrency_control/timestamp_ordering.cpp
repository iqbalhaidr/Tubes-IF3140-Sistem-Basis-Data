
#include <algorithm>
#include <iostream>

#include "concurrency_control.h"

namespace mdbms::ccm {

// Implementasi Timestamp Based Protocol
TimestampCCManager::TimestampCCManager() : current_timestamp_(0) {
    std::cout << "TS: Protokol Timestamp Ordering diinisialisasi" << std::endl;
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

    std::cout << "TS: Transaksi " << transaction_id << " dimulai dengan timestamp "
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

    std::cout << "TS: Transaksi " << transaction_id
              << " mencatat akses ke objek (hash: " << row_hash << ") pada tabel "
              << object.table_name << std::endl;
}

Response TimestampCCManager::validate_object(const Row& object, int transaction_id, Action action) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Error handling: jika transaksi tidak ditemukan, log dan return Response gagal
    if (transactions_.find(transaction_id) == transactions_.end()) {
        std::cerr << "[ERROR] Transaksi " << transaction_id
                  << " tidak ditemukan pada validate_object" << std::endl;
        return Response(false, transaction_id);
    }

    auto transaction = transactions_[transaction_id];

    // Jika transaksi sudah FAILED sebelumnya, langsung reject
    if (transaction->state == TransactionStatus::FAILED) {
        std::cerr << "[ERROR] Transaksi " << transaction_id << " sudah dalam state FAILED"
                  << std::endl;
        return Response(false, transaction_id);
    }

    size_t row_hash = generate_row_hash(object);

    if (object_timestamps_.find(row_hash) == object_timestamps_.end()) {
        std::cerr << "[ERROR] Objek belum di-log sebelum validate! "
                  << "Harus panggil log_object() dulu." << std::endl;
        return Response(false, transaction_id);
    }

    ObjectTimestamp& ts = object_timestamps_[row_hash];

    // Basic Timestamp Ordering Protocol
    if (action == Action::READ) {
        // Operasi READ: TS(T) < write_ts(X) -> ABORT
        if (transaction->timestamp < ts.write_ts) {
            std::cout << "TS: Transaksi " << transaction_id
                      << " GAGAL karena konflik timestamp pada READ (TS=" << transaction->timestamp
                      << " < write_ts=" << ts.write_ts << ")" << std::endl;

            transaction->state = TransactionStatus::FAILED;
            return Response(false, transaction_id);
        }

        // Update read timestamp
        ts.read_ts = std::max(ts.read_ts, transaction->timestamp);

        std::cout << "TS: Transaksi " << transaction_id
                  << " diizinkan READ pada objek (hash: " << row_hash << "), read_ts diupdate ke "
                  << ts.read_ts << std::endl;
    } else if (action == Action::WRITE) {
        // Operasi WRITE: TS(T) < read_ts(X) ATAU TS(T) < write_ts(X) -> ABORT
        if (transaction->timestamp < ts.read_ts || transaction->timestamp < ts.write_ts) {
            std::cout << "TS: Transaksi " << transaction_id
                      << " GAGAL karena konflik timestamp pada WRITE (TS=" << transaction->timestamp
                      << ", read_ts=" << ts.read_ts << ", write_ts=" << ts.write_ts << ")"
                      << std::endl;

            transaction->state = TransactionStatus::FAILED;
            return Response(false, transaction_id);
        }

        // Update write timestamp
        ts.write_ts = transaction->timestamp;

        std::cout << "TS: Transaksi " << transaction_id
                  << " diizinkan WRITE pada objek (hash: " << row_hash << "), write_ts diupdate ke "
                  << ts.write_ts << std::endl;
    }

    return Response(true, transaction_id);
}

void TimestampCCManager::end_transaction(int transaction_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (transactions_.find(transaction_id) == transactions_.end()) {
        std::cerr << "[ERROR] Transaksi " << transaction_id
                  << " tidak ditemukan pada end_transaction" << std::endl;
        return;
    }

    auto transaction = transactions_[transaction_id];

    // Transisi state sesuai diagram state transaksi:
    // Success path: ACTIVE → PARTIALLY_COMMITTED → COMMITTED → TERMINATED
    // Failure path: ACTIVE/PARTIALLY_COMMITTED → FAILED → ABORTED → TERMINATED

    if (transaction->state == TransactionStatus::FAILED) {
        // Jalur FAILED → ABORTED
        std::cout << "TS: Transaksi " << transaction_id << " transisi: FAILED → ABORTED"
                  << std::endl;
        transaction->state = TransactionStatus::ABORTED;

        // Note: Actual data rollback dilakukan oleh Failure Recovery Manager
        // CCM hanya menghapus transaksi dari daftar aktif

        std::cout << "TS: Transaksi " << transaction_id << " transisi: ABORTED → TERMINATED"
                  << std::endl;
        transaction->state = TransactionStatus::TERMINATED;

    } else if (transaction->state == TransactionStatus::ABORTED) {
        // Sudah ABORTED sebelumnya, langsung ke TERMINATED
        std::cout << "TS: Transaksi " << transaction_id << " transisi: ABORTED → TERMINATED"
                  << std::endl;
        transaction->state = TransactionStatus::TERMINATED;

    } else if (transaction->state == TransactionStatus::ACTIVE) {
        // Jalur sukses: ACTIVE → PARTIALLY_COMMITTED → COMMITTED → TERMINATED
        std::cout << "TS: Transaksi " << transaction_id << " transisi: ACTIVE → PARTIALLY_COMMITTED"
                  << std::endl;
        transaction->state = TransactionStatus::PARTIALLY_COMMITTED;

        // Di state PARTIALLY_COMMITTED, semua operasi sudah selesai
        // Menunggu flush ke stable storage (dilakukan FRM)
        // Setelah FRM confirm flush sukses, baru bisa COMMITTED

        std::cout << "TS: Transaksi " << transaction_id
                  << " transisi: PARTIALLY_COMMITTED → COMMITTED" << std::endl;
        transaction->state = TransactionStatus::COMMITTED;

        // Flush: retain object timestamps (sudah commit, timestamps tetap valid)
        // Tidak perlu cleanup timestamps karena transaksi berhasil

        std::cout << "TS: Transaksi " << transaction_id << " transisi: COMMITTED → TERMINATED"
                  << std::endl;
        transaction->state = TransactionStatus::TERMINATED;

    } else if (transaction->state == TransactionStatus::PARTIALLY_COMMITTED) {
        // Edge case: jika dipanggil saat PARTIALLY_COMMITTED
        std::cout << "TS: Transaksi " << transaction_id
                  << " transisi: PARTIALLY_COMMITTED → COMMITTED" << std::endl;
        transaction->state = TransactionStatus::COMMITTED;

        std::cout << "TS: Transaksi " << transaction_id << " transisi: COMMITTED → TERMINATED"
                  << std::endl;
        transaction->state = TransactionStatus::TERMINATED;

    } else {
        // Sudah terminal atau state tidak valid
        std::cout << "TS: Transaksi " << transaction_id
                  << " sudah dalam state terminal atau invalid: "
                  << static_cast<int>(transaction->state) << std::endl;
        transaction->state = TransactionStatus::TERMINATED;
    }

    // Cleanup: hapus transaksi dari daftar aktif setelah TERMINATED
    std::cout << "TS: Cleanup transaksi " << transaction_id << " dari daftar aktif" << std::endl;
    transactions_.erase(transaction_id);
}

TransactionStatus TimestampCCManager::get_transaction_status(int transaction_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (transactions_.find(transaction_id) == transactions_.end()) {
        return TransactionStatus::TERMINATED;
    }

    return transactions_[transaction_id]->state;

}  

} // namespace mdbms::ccm
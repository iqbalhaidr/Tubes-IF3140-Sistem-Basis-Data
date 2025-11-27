#include "concurrency_control.h"
#include <algorithm>
#include <iostream>

namespace mdbms::ccm {
MVCCManager::MVCCManager() : current_timestamp_(0) {
    std::cout << "CCM: Protokol MVCC diinisialisasi" << std::endl;
}

MVCCManager::~MVCCManager() {
    object_versions_.clear();
    transactions_.clear();
    read_dependency_.clear();
}

ObjectVersion* MVCCManager::find_latest_version(size_t row_hash, int trans_ts) {
    if (object_versions_.find(row_hash) == object_versions_.end()) return nullptr;
    auto& versions = object_versions_[row_hash];
    for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
    if (it->write_ts <= trans_ts) return &(*it);
    }
    return nullptr;
}

int MVCCManager::begin_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_timestamp_++;
    int transaction_id = current_timestamp_;
    auto transaction = std::make_shared<Transaction>(transaction_id, current_timestamp_);
    transactions_[transaction_id] = transaction;
    read_dependency_[transaction_id] = {};
    std::cout << "CCM: Transaksi " << transaction_id << " dimulai dengan timestamp " << current_timestamp_ << std::endl;
    return transaction_id;
}

void MVCCManager::log_object(const Row& object, int transaction_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transactions_.find(transaction_id) == transactions_.end()) {
    throw std::runtime_error("Transaksi " + std::to_string(transaction_id) + " tidak ditemukan");
    }
    size_t row_hash = generate_row_hash(object);
    if (object_versions_.find(row_hash) == object_versions_.end()) {
    object_versions_[row_hash] = { ObjectVersion{0, 0, 0, object} };
    }
    std::cout << "CCM: Transaksi " << transaction_id << " mencatat akses ke objek (hash: " << row_hash << ") pada tabel " << object.table_name << std::endl;
}

Response MVCCManager::validate_object(const Row& object, int transaction_id, Action action) {
    if (transactions_.find(transaction_id) == transactions_.end()) {
    std::cerr << "[ERROR] Transaksi " << transaction_id << " tidak ditemukan pada validate_object" << std::endl;
    return Response(false, transaction_id);
    }
    auto transaction = transactions_[transaction_id];
    size_t row_hash = generate_row_hash(object);
    ObjectVersion* latest_version = find_latest_version(row_hash, transaction->timestamp);
    if (action == Action::READ) {
    if (!latest_version) {
            log_object(object, transaction_id);
            latest_version = find_latest_version(row_hash, transaction->timestamp);
    }
    if (latest_version->read_ts < transaction->timestamp) {
            latest_version->read_ts = transaction->timestamp;
    }
    std::cout << "CCM: Transaksi " << transaction_id << " READ versi " << latest_version->version << " (r_ts=" << latest_version->read_ts << ", w_ts=" << latest_version->write_ts << ")" << std::endl;
    // Dependency tracking
    if (latest_version->write_ts != 0) {
            read_dependency_[latest_version->write_ts].push_back(transaction_id);
    }
    return Response(true, transaction_id);
    } else if (action == Action::WRITE) {
    if (latest_version && transaction->timestamp < latest_version->read_ts) {
            std::cout << "CCM: Transaksi " << transaction_id << " ABORT karena konflik MVCC pada WRITE (r_ts=" << latest_version->read_ts << ")" << std::endl;
            abort_transaction(transaction_id);
            return Response(false, transaction_id);
    }
    ObjectVersion new_version{transaction->timestamp, transaction->timestamp, transaction->timestamp, object};
    object_versions_[row_hash].push_back(new_version);
    std::cout << "CCM: Transaksi " << transaction_id << " WRITE versi baru " << new_version.version << " (r_ts=" << new_version.read_ts << ", w_ts=" << new_version.write_ts << ")" << std::endl;
    return Response(true, transaction_id);
    }
    return Response(false, transaction_id);
}

void MVCCManager::abort_transaction(int transaction_id) {
    if (transactions_.find(transaction_id) == transactions_.end()) return;
    auto transaction = transactions_[transaction_id];
    transaction->state = TransactionStatus::ABORTED;
    // Cleanup versions written by this transaction
    for (auto& [row_hash, versions] : object_versions_) {
    versions.erase(std::remove_if(versions.begin(), versions.end(), [transaction_id](const ObjectVersion& v) {
            return v.write_ts == transaction_id;
    }), versions.end());
    }
    // Abort dependent transactions
    for (int dep : read_dependency_[transaction_id]) {
    if (transactions_.find(dep) != transactions_.end() && transactions_[dep]->state == TransactionStatus::ACTIVE) {
            std::cout << "CCM: Transaksi " << dep << " ABORT karena dependensi pada transaksi " << transaction_id << std::endl;
            abort_transaction(dep);
    }
    }
    read_dependency_[transaction_id].clear();
    std::cout << "CCM: Transaksi " << transaction_id << " di-abort dan dibersihkan." << std::endl;
}

void MVCCManager::end_transaction(int transaction_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transactions_.find(transaction_id) == transactions_.end()) {
    std::cerr << "[ERROR] Transaksi " << transaction_id << " tidak ditemukan pada end_transaction" << std::endl;
    return;
    }
    transactions_.erase(transaction_id);
    read_dependency_.erase(transaction_id);
    std::cout << "CCM: Cleanup resource transaksi " << transaction_id << std::endl;
}

} // namespace mdbms::ccm
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
}
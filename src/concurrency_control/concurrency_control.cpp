#include "concurrency_control.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>

namespace mdbms::ccm {
    
size_t CCManager::generate_row_hash(const Row& row) {
    // Generate hash berdasarkan nama tabel dan row_id
    std::hash<std::string> string_hasher;
    std::hash<int> int_hasher;

    size_t hash = string_hasher(row.table_name);

    if (row.row_id != -1) {
        // Hash berdasarkan row_id jika ada
        hash ^= int_hasher(row.row_id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    } else {
        // Jika tidak, Hash berdasarkan nilai kolom
        for (const auto& [col_name, col_value] : row.columns) {
            hash ^= string_hasher(col_name) + 0x9e3779b9 + (hash << 6) + (hash >> 2);

            try {
                if (col_value.type() == typeid(int)) {
                    hash ^= int_hasher(std::any_cast<int>(col_value)) + 0x9e3779b9 + (hash << 6) +
                            (hash >> 2);
                } else if (col_value.type() == typeid(std::string)) {
                    hash ^= string_hasher(std::any_cast<std::string>(col_value)) + 0x9e3779b9 +
                            (hash << 6) + (hash >> 2);
                } else if (col_value.type() == typeid(double)) {
                    std::hash<double> double_hasher;
                    hash ^= double_hasher(std::any_cast<double>(col_value)) + 0x9e3779b9 +
                            (hash << 6) + (hash >> 2);
                } else if (col_value.type() == typeid(float)) {
                    std::hash<float> float_hasher;
                    hash ^= float_hasher(std::any_cast<float>(col_value)) + 0x9e3779b9 +
                            (hash << 6) + (hash >> 2);
                }
            } catch (const std::bad_any_cast&) {
                continue;
            }
        }
    }

    return hash;
}

// Inisialisasi static member
std::unique_ptr<ConcurrencyControlManager> ConcurrencyControlManager::instance_ = nullptr;
std::mutex ConcurrencyControlManager::instance_mutex_;

ConcurrencyControlManager& ConcurrencyControlManager::get_instance(const std::string& algorithm) {
    std::lock_guard<std::mutex> lock(instance_mutex_);

    if (instance_ == nullptr) {
        instance_.reset(new ConcurrencyControlManager(algorithm));
        std::cout << "CCM: Instance Singleton dibuat" << std::endl;
    }

    return *instance_;
}

ConcurrencyControlManager::ConcurrencyControlManager(const std::string& algorithm)
    : current_algorithm_(algorithm) {
    switch_algorithm(algorithm);
}

ConcurrencyControlManager::~ConcurrencyControlManager() = default;

void ConcurrencyControlManager::switch_algorithm(const std::string& algorithm) {
    std::string algo_lower = algorithm;
    std::transform(algo_lower.begin(), algo_lower.end(), algo_lower.begin(), ::tolower);

    cc_manager_.reset();

    if (algo_lower == "timestamp") {
        // TODO: Implementasi Timestamp
        // cc_manager_ = std::make_unique<TimestampCCManager>();
        // current_algorithm_ = "timestamp";
    }
    else if (algo_lower == "twophaselocking" || algo_lower == "2pl") {
        cc_manager_ = std::make_unique<TwoPhaseLockingCCManager>();
        current_algorithm_ = "twophaselocking";
    }
    // else if (algo_lower == "mvcc") {
    //     TODO: Implementasi MVCC
    //     cc_manager_ = std::make_unique<MVCCManager>();
    //     current_algorithm_ = "mvcc";
    // }
    else {
        throw std::invalid_argument("Algoritma concurrency control tidak didukung: " + algorithm);
    }

    std::cout << "CCM: Beralih ke protokol concurrency control " << current_algorithm_ << std::endl;
}

int ConcurrencyControlManager::begin_transaction() {
    return cc_manager_->begin_transaction();
}

void ConcurrencyControlManager::log_object(const Row& object, int transaction_id) {
    cc_manager_->log_object(object, transaction_id);
}

Response ConcurrencyControlManager::validate_object(const Row& object, int transaction_id, Action action) {
    return cc_manager_->validate_object(object, transaction_id, action);
}

void ConcurrencyControlManager::end_transaction(int transaction_id) {
    cc_manager_->end_transaction(transaction_id);
}

} // namespace mdbms::ccm

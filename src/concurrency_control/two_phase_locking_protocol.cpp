#include <algorithm>
#include <iostream>

#include "concurrency_control.h"

namespace mdbms::ccm {

TwoPhaseLockingCCManager::TwoPhaseLockingCCManager() : current_transaction_id(0) {
}

TwoPhaseLockingCCManager::~TwoPhaseLockingCCManager() = default;

int TwoPhaseLockingCCManager::begin_transaction() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    current_transaction_id++;
    int tid = current_transaction_id;

    auto transaction_info = std::make_shared<TransactionInfo2PL>(tid);
    transactions[tid] = transaction_info;

    std::cout << "2PL: Transaksi " << tid << " dimulai (fase GROWING)." << std::endl;
    return tid;
}

void TwoPhaseLockingCCManager::log_object(const Row& object, int transaction_id) {
    size_t hash = generate_row_hash(object);
    // std::cout << "2PL: Transaction " << transaction_id << " access record " << hash << std::endl;
}

Response TwoPhaseLockingCCManager::validate_object(const Row& object, int transaction_id,
                                                   Action action) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // cek info transaksi
    if (transactions.find(transaction_id) == transactions.end()) {
        std::cout << "2PL: Transaksi " << transaction_id << " tidak ditemukan." << std::endl;
        return Response(false, transaction_id);
    }

    auto transaction_info = transactions[transaction_id];

    // cek status apakah transaksi di abort
    if (transaction_info->status == TransactionStatus::ABORTED ||
        transaction_info->status == TransactionStatus::FAILED) {
        return Response(false, transaction_id);
    }

    // cek bahwa transaksi tidak sedang berada di fase SHRINKING
    if (transaction_info->phase == TransactionPhase::SHRINKING) {
        std::cout << "2PL: Transaksi " << transaction_id
                  << " berada di fase SHRINKING, tidak dapat memperoleh kunci baru." << std::endl;
        abort_transaction(transaction_id);
        return Response(false, transaction_id);
    }

    size_t record_hash = generate_row_hash(object);
    bool lock_acquired = false;

    if (action == Action::READ) {
        lock_acquired = acquire_s_lock(transaction_id, record_hash);
    } else if (action == Action::WRITE) {
        lock_acquired = acquire_x_lock(transaction_id, record_hash);
    }

    // TODO: implementasi waiting
    // either urusan qp atau bikin queue di sini (bisa juga pake notify_all dari mutex)
    if (!lock_acquired) {
        std::cout << "2PL: Transaksi " << transaction_id << " gagal memperoleh kunci untuk record "
                  << record_hash << std::endl;
        abort_transaction(transaction_id);
        return Response(false, transaction_id);
    }

    return Response(true, transaction_id);
}

bool TwoPhaseLockingCCManager::acquire_s_lock(int transaction_id, size_t record_hash) {
    auto transaction_info = transactions[transaction_id];

    // sudah memiliki slock
    if (transaction_info->locked_records.count(record_hash)) {
        return true;
    }

    // cek konflik dengan xlock
    if (x_lock_table.count(record_hash)) {
        int holding_trx_id = x_lock_table[record_hash];

        if (holding_trx_id != transaction_id) {
            if (detect_deadlock(transaction_id, holding_trx_id)) {
                std::cout << "2PL: Deadlock terdeteksi antara transaksi " << transaction_id
                          << " dan " << holding_trx_id << std::endl;
                abort_transaction(transaction_id);
            } else {
                std::cout << "2PL: Transaksi " << transaction_id << " menunggu X lock pada record "
                          << record_hash << " yang dipegang oleh transaksi " << holding_trx_id
                          << std::endl;
                wait_for_graph[transaction_id].insert(holding_trx_id);
            }
            return false;
        }
    }

    // berikan slock pada transaksi
    s_lock_table[record_hash].insert(transaction_id);
    transactions[transaction_id]->locked_records.insert(record_hash);

    // hapus dari wait-for graph jika ada
    wait_for_graph.erase(transaction_id);

    std::cout << "2PL: Transaksi " << transaction_id << " memperoleh S lock pada record "
              << record_hash << std::endl;
    return true;
}

bool TwoPhaseLockingCCManager::acquire_x_lock(int transaction_id, size_t record_hash) {
    auto transaction_info = transactions[transaction_id];

    // sudah memiliki xlock
    if (x_lock_table.count(record_hash) && x_lock_table[record_hash] == transaction_id) {
        return true;
    }

    // cek konflik dengan xlock
    if (x_lock_table.count(record_hash)) {
        int holding_trx_id = x_lock_table[record_hash];

        if (holding_trx_id != transaction_id) {
            if (detect_deadlock(transaction_id, holding_trx_id)) {
                std::cout << "2PL: Deadlock terdeteksi antara transaksi " << transaction_id
                          << " dan " << holding_trx_id << std::endl;
                abort_transaction(transaction_id);
            } else {
                std::cout << "2PL: Transaksi " << transaction_id << " menunggu X lock pada record "
                          << record_hash << " yang dipegang oleh transaksi " << holding_trx_id
                          << std::endl;
                wait_for_graph[transaction_id].insert(holding_trx_id);
            }
            return false;
        }
    }

    // cek konflik dengan slock
    if (s_lock_table.count(record_hash)) {
        auto& holding_trx_set = s_lock_table[record_hash];

        if (!(holding_trx_set.size() == 1 && holding_trx_set.count(transaction_id))) {
            for (int holding_trx_id : holding_trx_set) {
                if (holding_trx_id != transaction_id) {
                    if (detect_deadlock(transaction_id, holding_trx_id)) {
                        std::cout << "2PL: Deadlock terdeteksi antara transaksi " << transaction_id
                                  << " dan " << holding_trx_id << std::endl;
                        abort_transaction(transaction_id);
                    } else {
                        std::cout << "2PL: Transaksi " << transaction_id
                                  << " menunggu S lock pada record " << record_hash
                                  << " yang dipegang oleh transaksi " << holding_trx_id
                                  << std::endl;
                        wait_for_graph[transaction_id].insert(holding_trx_id);
                    }
                    return false;
                }
            }
        }

        holding_trx_set.erase(transaction_id);
        if (holding_trx_set.empty()) {
            s_lock_table.erase(record_hash);
        }
        std::cout << "2PL: Transaksi " << transaction_id
                  << " mengupgrade S lock ke X lock pada record " << record_hash << std::endl;
    }

    // berikan xlock pada transaksi
    x_lock_table[record_hash] = transaction_id;
    transactions[transaction_id]->locked_records.insert(record_hash);
    wait_for_graph.erase(transaction_id);

    std::cout << "2PL: Transaksi " << transaction_id << " memperoleh X lock pada record "
              << record_hash << std::endl;
    return true;
}

bool TwoPhaseLockingCCManager::check_cycle(int current_id, std::set<int>& visited,
                                           std::set<int>& rec_stack) {
    visited.insert(current_id);
    rec_stack.insert(current_id);

    if (wait_for_graph.count(current_id)) {
        for (int neighbor_trx_id : wait_for_graph[current_id]) {
            if (rec_stack.count(neighbor_trx_id)) {
                return true;
            }
            if (!visited.count(neighbor_trx_id)) {
                if (check_cycle(neighbor_trx_id, visited, rec_stack)) {
                    return true;
                }
            }
        }
    }

    rec_stack.erase(current_id);
    return false;
}

bool TwoPhaseLockingCCManager::detect_deadlock(int waiting_trx_id, int holding_trx_id) {
    wait_for_graph[waiting_trx_id].insert(holding_trx_id);

    std::set<int> visited;
    std::set<int> rec_stack;

    bool deadlock = check_cycle(waiting_trx_id, visited, rec_stack);

    if (!deadlock) {
        wait_for_graph[waiting_trx_id].erase(holding_trx_id);
        if (wait_for_graph[waiting_trx_id].empty()) {
            wait_for_graph.erase(waiting_trx_id);
        }
    } else {
        std::cout << "2PL: Deadlock terdeteksi antara transaksi " << waiting_trx_id << " dan "
                  << holding_trx_id << std::endl;
    }

    return deadlock;
}

void TwoPhaseLockingCCManager::abort_transaction(int transaction_id) {
    if (transactions.find(transaction_id) == transactions.end()) {
        return;
    }

    auto transaction_info = transactions[transaction_id];
    transaction_info->status = TransactionStatus::ABORTED;

    std::cout << "2PL: Transaksi " << transaction_id << " diabort." << std::endl;
    release_all_locks(transaction_id);
}

void TwoPhaseLockingCCManager::end_transaction(int transaction_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (transactions.find(transaction_id) == transactions.end()) {
        return;
    }

    auto transaction_info = transactions[transaction_id];
    transaction_info->phase = TransactionPhase::SHRINKING;

    if (transaction_info->status == TransactionStatus::ABORTED) {
        std::cout << "2PL: Transaksi " << transaction_id
                  << " sudah diabort, tidak dapat diakhiri secara normal." << std::endl;
        return;
    } else {
        transaction_info->status = TransactionStatus::COMMITTED;
        std::cout << "2PL: Transaksi " << transaction_id << " berhasil commit ahayyy." << std::endl;
        return;
    }

    release_all_locks(transaction_id);
}

void TwoPhaseLockingCCManager::release_s_lock(int transaction_id, size_t record_hash) {
    if (s_lock_table.count(record_hash)) {
        auto& holding_trx_set = s_lock_table[record_hash];

        if (holding_trx_set.find(transaction_id) == holding_trx_set.end()) {
            return;
        }

        holding_trx_set.erase(transaction_id);
        if (holding_trx_set.empty()) {
            s_lock_table.erase(record_hash);
        }

        transactions[transaction_id]->locked_records.erase(record_hash);
        std::cout << "2PL: Transaksi " << transaction_id << " melepaskan S lock pada record "
                  << record_hash << std::endl;
    }
}

void TwoPhaseLockingCCManager::release_x_lock(int transaction_id, size_t record_hash) {
    if (x_lock_table.count(record_hash) && x_lock_table[record_hash] == transaction_id) {
        x_lock_table.erase(record_hash);
        transactions[transaction_id]->locked_records.erase(record_hash);
        std::cout << "2PL: Transaksi " << transaction_id << " melepaskan X lock pada record "
                  << record_hash << std::endl;
    }
}

void TwoPhaseLockingCCManager::release_all_locks(int transaction_id) {
    if (transactions.find(transaction_id) == transactions.end()) {
        return;
    }
    auto transaction_info = transactions[transaction_id];

    // lepas semua kunci yang dimiliki transaksi
    std::vector<size_t> to_release(transaction_info->locked_records.begin(),
                                   transaction_info->locked_records.end());
    for (size_t record_hash : to_release) {
        release_s_lock(transaction_id, record_hash);
        release_x_lock(transaction_id, record_hash);
    }

    // bersihkan transaction id dari key wait-for graph
    wait_for_graph.erase(transaction_id);

    // bersihkan transaction id dari value wait-for graph
    auto it = wait_for_graph.begin();
    while (it != wait_for_graph.end()) {
        std::set<int>& trx_set = it->second;
        trx_set.erase(transaction_id);
        if (trx_set.empty()) {
            it = wait_for_graph.erase(it);
        } else {
            ++it;
        }
    }

    std::cout << "2PL: Transaksi " << transaction_id << " melepaskan semua kunci." << std::endl;
}

}  // namespace mdbms::ccm
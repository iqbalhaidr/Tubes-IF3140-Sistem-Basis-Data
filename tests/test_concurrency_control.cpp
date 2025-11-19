#include <any>
#include <iostream>
#include <string>
#include <vector>

#include "types.h"

#include "concurrency_control.h"

using namespace mdbms;
using namespace mdbms::ccm;

Row create_row(int id, std::string val_content) {
    Row r;
    r.row_id = id;
    r.table_name = "TestTable";
    r.columns["data_col"] = val_content;

    return r;
}

void print_response(const std::string& prefix, const Response& resp) {
    std::cout << prefix << "Allowed: " << (resp.allowed ? "True" : "False")
              << ", TID: " << resp.transaction_id << std::endl;
}

void test_twoPL_cc() {
    std::cout << "\nTesting Two-Phase Locking Concurrency Control\n" << std::endl;
    
    auto& ccm = ConcurrencyControlManager::get_instance("twophaselocking");
    ccm.switch_algorithm("twophaselocking");

    Row row1 = create_row(1, "Group1");
    Row row2 = create_row(2, "Group2");

    int tid1 = ccm.begin_transaction();
    ccm.log_object(row1, tid1);
    Response response1 = ccm.validate_object(row1, tid1, Action::READ);
    print_response("Transaction " + std::to_string(tid1) + " READ on Row1: ", response1);

    int tid2 = ccm.begin_transaction();
    Response response2 = ccm.validate_object(row1, tid2, Action::READ);
    print_response("Transaction " + std::to_string(tid2) + " READ on Row1: ", response2);

    // tc: exclusive lock conflict
    // harusnya fail karena tid1 dan tid2 punya slock
    Response response3 = ccm.validate_object(row1, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row1: ", response3);

    // tc: independent operations on different rows
    ccm.log_object(row2, tid2);
    Response response4 = ccm.validate_object(row2, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row2: ", response4);

    // tc: lock upgrade attempt (atau read di row lain)
    int tid3 = ccm.begin_transaction();
    Response response5 = ccm.validate_object(row2, tid3, Action::READ);
    print_response("Transaction " + std::to_string(tid3) + " READ on Row2: ", response5);

    ccm.end_transaction(tid1);

    // tid3 mencoba write row1 (setelah tid1 lepas lock)
    Response response6 = ccm.validate_object(row1, tid3, Action::WRITE);
    print_response("Transaction " + std::to_string(tid3) + " WRITE on Row1: ", response6);

    ccm.end_transaction(tid2);
    ccm.end_transaction(tid3);
}

void test_twoPL_abort() {
    std::cout << "\nTesting Two-Phase Locking Abort Scenarios\n" << std::endl;
    
    auto& ccm = ConcurrencyControlManager::get_instance("twophaselocking");
    ccm.switch_algorithm("twophaselocking");

    Row row1 = create_row(10, "AliceBob");
    Row row2 = create_row(11, "CharlieDiana");

    int tid1 = ccm.begin_transaction();

    ccm.log_object(row1, tid1);
    Response response1 = ccm.validate_object(row1, tid1, Action::READ);
    print_response("Transaction " + std::to_string(tid1) + " READ on Row1: ", response1);

    int tid2 = ccm.begin_transaction();

    ccm.log_object(row2, tid2);
    Response response2 = ccm.validate_object(row2, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row2: ", response2);

    // tid1 commit -> masuk fase SHRINKING
    ccm.end_transaction(tid1);

    // coba ambil lock baru setelah commit (seharusnya gagal/abort)
    Response response3 = ccm.validate_object(row2, tid1, Action::READ);
    print_response("Transaction " + std::to_string(tid1) + " READ on Row2: ",
                   response3);

    ccm.end_transaction(tid2);
}

// karena sekarang kalau transaksi nunggu langsung diabort, belum mungkin terjadi deadlock
void test_twoPL_deadlock() {
    std::cout << "\nTesting Two-Phase Locking Deadlock Scenario\n" << std::endl;
    
    auto& ccm = ConcurrencyControlManager::get_instance("twophaselocking");
    ccm.switch_algorithm("twophaselocking");

    Row row1 = create_row(20, "AliceOnly");
    Row row2 = create_row(21, "BobOnly");

    int tid1 = ccm.begin_transaction();
    ccm.log_object(row1, tid1);
    Response response1 = ccm.validate_object(row1, tid1, Action::READ);
    print_response("Transaction " + std::to_string(tid1) + " READ on Row1: ", response1);

    int tid2 = ccm.begin_transaction();
    ccm.log_object(row2, tid2);
    Response response2 = ccm.validate_object(row2, tid2, Action::READ);
    print_response("Transaction " + std::to_string(tid2) + " READ on Row2: ", response2);

    // T1 write R2 (punya T2) -> sekarang abort, nanti nunggu
    Response response3 = ccm.validate_object(row2, tid1, Action::WRITE);
    print_response("Transaction " + std::to_string(tid1) + " WRITE on Row2: ", response3);

    // T2 write R1 (punya T1) -> sekarang bisa langsung aja, nanti jadi deadlock
    Response response4 = ccm.validate_object(row1, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row1: ", response4);

    // sekarang T1 udah abort, nanti dicek apakah T1 masih bisa lanjut
    Response response5 = ccm.validate_object(row1, tid1, Action::WRITE);
    print_response("Transaction " + std::to_string(tid1) + " WRITE on Row1: ", response5);

    ccm.end_transaction(tid1);
    ccm.end_transaction(tid2);
}

void test_twoPL_deadlock_abort() {
    std::cout << "\nTesting Two-Phase Locking Deadlock with Abort Resolution\n" << std::endl;
    
    auto& ccm = ConcurrencyControlManager::get_instance("twophaselocking");
    ccm.switch_algorithm("twophaselocking");

    Row row1 = create_row(30, "Alice");
    Row row2 = create_row(31, "Bob");

    int tid1 = ccm.begin_transaction();
    ccm.log_object(row1, tid1);
    Response response1 = ccm.validate_object(row1, tid1, Action::WRITE);
    print_response("Transaction " + std::to_string(tid1) + " WRITE on Row1: ", response1);

    int tid2 = ccm.begin_transaction();
    ccm.log_object(row2, tid2);
    Response response2 = ccm.validate_object(row2, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row2: ", response2);

    // T1 write R2 (punya T2) -> sekarang abort, nanti nunggu
    ccm.log_object(row2, tid1);
    Response response3 = ccm.validate_object(row2, tid1, Action::WRITE);
    print_response("Transaction " + std::to_string(tid1) + " WRITE on Row2: ", response3);

    // T2 write R1 (punya T1) -> sekarang bisa langsung aja, nanti jadi deadlock
    ccm.log_object(row1, tid2);
    Response response4 = ccm.validate_object(row1, tid2, Action::WRITE);
    print_response("Transaction " + std::to_string(tid2) + " WRITE on Row1: ", response4);

    ccm.end_transaction(tid1);
    ccm.end_transaction(tid2);
}

int main() {
    try {
        test_twoPL_cc();
        test_twoPL_abort();
        test_twoPL_deadlock();
        test_twoPL_deadlock_abort();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}

/*
buat sekarang pemanggilan ccm masih

auto& ccm = ConcurrencyControlManager::get_instance("twophaselocking");
ccm.switch_algorithm("twophaselocking");

karena get_instance butuh argumen string (keknya default timestamp ga jalan,,,),
-> kalau get_instance isinya kosong ga ke set default jadi timestamp huhu
*/
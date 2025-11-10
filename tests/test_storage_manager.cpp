#include "storage_manager.h"
#include <iostream>
#include <cassert>
#include <filesystem>

void print_rows(const mdbms::Rows& rows) {
    std::cout << "--- Hasil Query (Total: " << rows.rows_count << ") ---" << std::endl;
    for (const auto& row : rows.data) {
        for (const auto& pair : row) {
            std::cout << pair.first << ": ";
            std::visit([](auto&& arg){ std::cout << arg; }, pair.second);
            std::cout << " | ";
        }
        std::cout << std::endl;
    }
    std::cout << "---------------------------------" << std::endl;
}


int main() {
    std::string test_dir = "sm_test_data";
    std::filesystem::create_directory(test_dir);
    std::string student_file = test_dir + "/Student.dat";
    std::string course_file = test_dir + "/Course.dat";

    // Hapus file lama jika ada
    std::remove(student_file.c_str());
    std::remove(course_file.c_str());

    mdbms::sm::StorageEngine sm(test_dir);

    std::cout << "--- TEST 1: INSERT ---" << std::endl;
    mdbms::DataWrite insert_alice;
    insert_alice.table = "Student";
    insert_alice.new_values = {
        {"StudentID", 101},
        {"FullName", std::string("Alice")},
        {"GPA", 3.8f}
    };
    int r1 = sm.write_block(insert_alice);
    assert(r1 == 1);
    
    mdbms::DataWrite insert_bob;
    insert_bob.table = "Student";
    insert_bob.new_values = {
        {"StudentID", 102},
        {"FullName", std::string("Bob")},
        {"GPA", 3.2f}
    };
    r1 = sm.write_block(insert_bob);
    assert(r1 == 1);
    std::cout << "Test 1 Lolos." << std::endl;

    
    std::cout << "\n--- TEST 2: READ (Full Scan) ---" << std::endl;
    mdbms::DataRetrieval read_all;
    read_all.table = "Student";
    read_all.columns = {"*"};
    mdbms::Rows rows_all = sm.read_block(read_all);
    print_rows(rows_all);
    assert(rows_all.rows_count == 2);
    std::cout << "Test 2 Lolos." << std::endl;


    std::cout << "\n--- TEST 3: READ (Conditional) ---" << std::endl;
    mdbms::DataRetrieval read_good;
    read_good.table = "Student";
    read_good.columns = {"FullName", "GPA"};
    read_good.conditions = {
        {"GPA", mdbms::OpType::GT, 3.5f}
    };
    mdbms::Rows rows_good = sm.read_block(read_good);
    print_rows(rows_good);
    assert(rows_good.rows_count == 1);
    std::cout << "Test 3 Lolos." << std::endl;


    std::cout << "\n--- TEST 4: UPDATE ---" << std::endl;
    mdbms::DataWrite update_bob;
    update_bob.table = "Student";
    update_bob.conditions = {
        {"StudentID", mdbms::OpType::EQ, 102}
    };
    update_bob.new_values = {
        {"GPA", 3.4f}
    };
    int r2 = sm.write_block(update_bob);
    assert(r2 == 1);
    std::cout << "Test 4 Lolos." << std::endl;


    std::cout << "\n--- TEST 5: READ (Verify Update) ---" << std::endl;
    mdbms::DataRetrieval read_bob;
    read_bob.table = "Student";
    read_bob.columns = {"GPA"};
    read_bob.conditions = {
        {"StudentID", mdbms::OpType::EQ, 102}
    };
    mdbms::Rows rows_bob = sm.read_block(read_bob);
    print_rows(rows_bob);
    assert(rows_bob.rows_count == 1);
    // Verifikasi nilainya
    assert(std::get<float>(rows_bob.data[0].at("GPA")) == 3.4f);
    std::cout << "Test 5 Lolos." << std::endl;

    std::cout << "\n--- TEST 6: DELETE ---" << std::endl;
    mdbms::DataDeletion delete_bob;
    delete_bob.table = "Student";
    delete_bob.conditions = {
        {"FullName", mdbms::OpType::EQ, std::string("Bob")}
    };
    int r3 = sm.delete_block(delete_bob);
    assert(r3 == 1);
    std::cout << "Test 6 Lolos." << std::endl;

    std::cout << "\n--- TEST 7: READ (Verify Delete) ---" << std::endl;
    rows_all = sm.read_block(read_all);
    print_rows(rows_all);
    assert(rows_all.rows_count == 1);
    std::cout << "Test 7 Lolos." << std::endl;

    std::cout << "\n*** SEMUA TES SM LOLOS! ***" << std::endl;

    // Cleanup
    std::remove(student_file.c_str());
    std::remove(course_file.c_str());
    std::filesystem::remove_all(test_dir);
    return 0;
}
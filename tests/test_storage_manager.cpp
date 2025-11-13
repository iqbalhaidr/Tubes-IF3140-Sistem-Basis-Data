#include "storage_manager.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <any>

void print_rows(const mdbms::Rows<mdbms::Row>& rows) {
    std::cout << "--- Hasil Query (Total: " << rows.rows_count << ") ---" << std::endl;
    for (const auto& row : rows.data) {
        for (const auto& [col_name, value] : row.columns) {
            std::cout << col_name << ": ";
            
            // Try to cast and print different types
            try {
                int val = std::any_cast<int>(value);
                std::cout << val;
            } catch (const std::bad_any_cast&) {
                try {
                    float val = std::any_cast<float>(value);
                    std::cout << val;
                } catch (const std::bad_any_cast&) {
                    try {
                        std::string val = std::any_cast<std::string>(value);
                        std::cout << val;
                    } catch (const std::bad_any_cast&) {
                        std::cout << "(unknown type)";
                    }
                }
            }
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
    mdbms::DataWrite<mdbms::Row> insert_alice;
    insert_alice.table = "Student";
    insert_alice.new_value.table_name = "Student";
    insert_alice.new_value.columns = {
        {"StudentID", 101},
        {"FullName", std::string("Alice")},
        {"GPA", 3.8f}
    };
    int r1 = sm.write_block(insert_alice);
    assert(r1 == 1);
    
    mdbms::DataWrite<mdbms::Row> insert_bob;
    insert_bob.table = "Student";
    insert_bob.new_value.table_name = "Student";
    insert_bob.new_value.columns = {
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
    mdbms::Rows<mdbms::Row> rows_all = sm.read_block(read_all);
    print_rows(rows_all);
    assert(rows_all.rows_count == 2);
    std::cout << "Test 2 Lolos." << std::endl;


    std::cout << "\n--- TEST 3: READ (Conditional) ---" << std::endl;
    mdbms::DataRetrieval read_good;
    read_good.table = "Student";
    read_good.columns = {"FullName", "GPA"};
    read_good.conditions = {
        mdbms::Condition("GPA", ">", 3.5f)
    };
    mdbms::Rows<mdbms::Row> rows_good = sm.read_block(read_good);
    print_rows(rows_good);
    assert(rows_good.rows_count == 1);
    std::cout << "Test 3 Lolos." << std::endl;


    std::cout << "\n--- TEST 4: UPDATE ---" << std::endl;
    mdbms::DataWrite<mdbms::Row> update_bob;
    update_bob.table = "Student";
    update_bob.conditions = {
        mdbms::Condition("StudentID", "=", 102)
    };
    update_bob.columns = {"GPA"};
    update_bob.new_value.columns = {
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
        mdbms::Condition("StudentID", "=", 102)
    };
    mdbms::Rows<mdbms::Row> rows_bob = sm.read_block(read_bob);
    print_rows(rows_bob);
    assert(rows_bob.rows_count == 1);
    // Verifikasi nilainya
    float gpa_value = std::any_cast<float>(rows_bob.data[0].columns.at("GPA"));
    assert(gpa_value == 3.4f);
    std::cout << "Test 5 Lolos." << std::endl;

    std::cout << "\n--- TEST 6: DELETE ---" << std::endl;
    mdbms::DataDeletion delete_bob;
    delete_bob.table = "Student";
    delete_bob.conditions = {
        mdbms::Condition("FullName", "=", std::string("Bob"))
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
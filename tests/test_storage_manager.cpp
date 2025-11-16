#include "storage_manager.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <any>
#include <set>

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

    // Tambah beberapa data tambahan termasuk multiple value dalam satu key hash
    mdbms::DataWrite<mdbms::Row> insert_cat1;
    insert_cat1.table = "Student";
    insert_cat1.new_value.table_name = "Student";
    insert_cat1.new_value.columns = {
        {"StudentID", 103},
        {"FullName", std::string("Catherine Johnson-Delacroix")},
        {"GPA", 3.95f}
    };
    assert(sm.write_block(insert_cat1) == 1);

    mdbms::DataWrite<mdbms::Row> insert_dan;
    insert_dan.table = "Student";
    insert_dan.new_value.table_name = "Student";
    insert_dan.new_value.columns = {
        {"StudentID", 104},
        {"FullName", std::string("Daniel \"Dan\" Robertson the Third")},
        {"GPA", 2.75f}
    };
    assert(sm.write_block(insert_dan) == 1);

    mdbms::DataWrite<mdbms::Row> insert_el;
    insert_el.table = "Student";
    insert_el.new_value.table_name = "Student";
    insert_el.new_value.columns = {
        {"StudentID", 105},
        {"FullName", std::string("Eleanora Maximillian Vanderfeldt")},
        {"GPA", 3.60f}
    };
    assert(sm.write_block(insert_el) == 1);

    // Multiple entries with SAME KEY (StudentID = 103)
    mdbms::DataWrite<mdbms::Row> insert_cat2;
    insert_cat2.table = "Student";
    insert_cat2.new_value.table_name = "Student";
    insert_cat2.new_value.columns = {
        {"StudentID", 103},
        {"FullName", std::string("Cat Johnson Clone A")},
        {"GPA", 3.10f}
    };
    assert(sm.write_block(insert_cat2) == 1);

    mdbms::DataWrite<mdbms::Row> insert_cat3;
    insert_cat3.table = "Student";
    insert_cat3.new_value.table_name = "Student";
    insert_cat3.new_value.columns = {
        {"StudentID", 103},
        {"FullName", std::string("Cat Johnson Clone B - With Very Long Name 1234567890")},
        {"GPA", 3.20f}
    };
    assert(sm.write_block(insert_cat3) == 1);

    std::cout << "Berhasil menambah data tambahan (termasuk multiple StudentID=103).\n" << std::endl;

    // Baca lagi
    rows_all = sm.read_block(read_all);
    print_rows(rows_all);

    // total data: sebelumnya 1 data (Alice), ditambah 5 = total 6
    assert(rows_all.rows_count == 6);
    std::cout << "Berhasil baca semua data." << std::endl;

    // --- TEST 8: SET INDEX (HASH) ---
    std::cout << "\n--- TEST 8: SET INDEX (HASH) ---" << std::endl;
    sm.set_index("Student", "StudentID", mdbms::IndexType::HASH);

    std::string idx_file = test_dir + "/Student.StudentID.hashidx";
    assert(std::filesystem::exists(idx_file));
    std::cout << "Index file ditemukan: " << idx_file << std::endl;

    std::cout << "Test 8 Lolos." << std::endl;

    std::cout << "\n--- TEST 9: VERIFY HASH INDEX CONTENT ---" << std::endl;

    std::ifstream idx(idx_file, std::ios::binary);
    assert(idx.is_open());

    // Magic header
    char magic[4];
    idx.read(magic, 4);
    assert(std::string(magic, 4) == "HIDX");

    // Index type
    uint8_t idx_type = 255;
    idx.read(reinterpret_cast<char*>(&idx_type), 1);
    assert(idx_type == 0);

    // dtype
    uint8_t dtype = 255;
    idx.read(reinterpret_cast<char*>(&dtype), 1);
    assert(dtype == 0);

    // number of unique keys
    uint32_t nkeys = 0;
    idx.read(reinterpret_cast<char*>(&nkeys), sizeof(nkeys));
    assert(nkeys == 4);

    // To store key -> count
    std::map<int32_t, uint32_t> key_counts;
    std::set<int32_t> keys;

    for (uint32_t i = 0; i < nkeys; i++) {
        int32_t key;
        idx.read(reinterpret_cast<char*>(&key), sizeof(key));

        uint32_t cnt;
        idx.read(reinterpret_cast<char*>(&cnt), sizeof(cnt));

        // store count
        key_counts[key] = cnt;

        // skip offsets
        for (uint32_t j = 0; j < cnt; j++) {
            int64_t offset;
            idx.read(reinterpret_cast<char*>(&offset), sizeof(offset));
        }

        keys.insert(key);
    }

    // verify keys exist
    assert(keys.count(101) == 1);
    assert(keys.count(103) == 1);
    assert(keys.count(104) == 1);
    assert(keys.count(105) == 1);

    // verify counts per key
    assert(key_counts[101] == 1);
    assert(key_counts[103] == 3);   // MULTIPLE VALUES!
    assert(key_counts[104] == 1);
    assert(key_counts[105] == 1);
    std::cout << "Test 9 Lolos (including value-count verification)." << std::endl;

    std::cout << "\n*** SEMUA TES SM LOLOS! ***" << std::endl;
    
    // Cleanup
    // std::remove(student_file.c_str());
    // std::remove(course_file.c_str());
    // std::filesystem::remove_all(test_dir);
    return 0;
}
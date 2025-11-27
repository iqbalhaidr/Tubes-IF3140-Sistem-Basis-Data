#include "storage_manager.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <any>
#include <set>

using namespace mdbms;
using namespace mdbms::sm;

void print_rows(const Rows<Row>& rows) {
    std::cout << "--- Hasil Query (Total: " << rows.rows_count << ") ---" << std::endl;

    for (const auto& row : rows.data) {
        for (const auto& [col_name, value] : row.columns) {
            std::cout << col_name << ": ";

            if (value.type() == typeid(int)) {
                std::cout << std::any_cast<int>(value);
            }
            else if (value.type() == typeid(float)) {
                std::cout << std::any_cast<float>(value);
            }
            else if (value.type() == typeid(double)) {
                std::cout << std::any_cast<double>(value);
            }
            else if (value.type() == typeid(std::string)) {
                std::cout << std::any_cast<std::string>(value);
            }
            else {
                std::cout << "(unknown)";
            }

            std::cout << " | ";
        }
        std::cout << std::endl;
    }

    std::cout << "---------------------------------\n";
}

int main() {
    std::string test_dir = "sm_test_data";
    std::filesystem::create_directory(test_dir);
    std::string student_file = test_dir + "/Student.dat";
    std::string course_file  = test_dir + "/Course.dat";

    std::remove(student_file.c_str());
    std::remove(course_file.c_str());

    StorageEngine sm(test_dir);

    // TEST 0: CREATE TABLE
    std::cout << "\n--- TEST 0: CREATE TABLE ---\n";
    TableSchema student_schema;
    student_schema.table_name = "Student";
    student_schema.column_names = {"StudentID", "FullName", "GPA"};
    student_schema.column_types = {DataType::INTEGER, DataType::VARCHAR, DataType::FLOAT};
    student_schema.column_sizes = {0, 50, 0};
    student_schema.primary_key = "StudentID";
    sm.create_table(student_schema);

    TableSchema course_schema;
    course_schema.table_name = "Course";
    course_schema.column_names = {"CourseID", "Year", "CourseName"};
    course_schema.column_types = {DataType::INTEGER, DataType::INTEGER, DataType::VARCHAR};
    course_schema.column_sizes = {0, 0, 50};
    course_schema.primary_key = "CourseID";
    sm.create_table(course_schema);

    std::cout << "Test 0 OK.\n";

    // TEST 1: INSERT
    std::cout << "\n--- TEST 1: INSERT ---\n";

    DataWrite<Row> insert_alice;
    insert_alice.table = "Student";
    insert_alice.new_value.table_name = "Student";
    insert_alice.new_value.columns = {
        {"StudentID", 101},
        {"FullName", std::string("Alice")},
        {"GPA", 3.8f}
    };
    assert(sm.write_block(insert_alice) == 1);

    DataWrite<Row> insert_bob;
    insert_bob.table = "Student";
    insert_bob.new_value.table_name = "Student";
    insert_bob.new_value.columns = {
        {"StudentID", 102},
        {"FullName", std::string("Bob")},
        {"GPA", 3.2f}
    };
    assert(sm.write_block(insert_bob) == 1);

    std::string datafile = test_dir + "/Student.dat";
    if (std::filesystem::exists(datafile)) {
        auto sz = std::filesystem::file_size(datafile);
        std::cout << "DEBUG: " << datafile << " size = " << sz << " bytes\n";
    } else {
        std::cout << "DEBUG: " << datafile << " tidak ada\n";
    }

    std::cout << "Test 1 OK.\n";

    // TEST 2: READ FULL SCAN
    std::cout << "\n--- TEST 2: READ FULL SCAN ---\n";

    DataRetrieval read_all;
    read_all.table = "Student";
    read_all.columns = {"*"};

    Rows<Row> rows_all = sm.read_block(read_all);
    print_rows(rows_all);
    assert(rows_all.rows_count == 2);
    std::cout << "Test 2 OK.\n";

    // TEST 3: CONDITIONAL READ
    std::cout << "\n--- TEST 3: CONDITIONAL READ ---\n";

    DataRetrieval read_good;
    read_good.table = "Student";
    read_good.columns = {"FullName", "GPA"};
    read_good.conditions = { Condition("GPA", ">", 3.5f) };

    Rows<Row> rows_good = sm.read_block(read_good);
    print_rows(rows_good);
    assert(rows_good.rows_count == 1);
    std::cout << "Test 3 OK.\n";

    // TEST 4: UPDATE
    std::cout << "\n--- TEST 4: UPDATE ---\n";

    DataWrite<Row> update_bob;
    update_bob.table = "Student";
    update_bob.conditions = { Condition("StudentID", "=", 102) };
    update_bob.columns = {"GPA"};
    update_bob.new_value.columns = { {"GPA", 3.4f} };

    assert(sm.write_block(update_bob) == 1);
    std::cout << "Test 4 OK.\n";

    // TEST 5: VERIFY UPDATE
    std::cout << "\n--- TEST 5: VERIFY UPDATE ---\n";

    DataRetrieval read_bob;
    read_bob.table = "Student";
    read_bob.columns = {"*"};
    read_bob.conditions = { Condition("StudentID", "=", 102) };

    Rows<Row> rows_bob = sm.read_block(read_bob);
    print_rows(rows_bob);
    assert(rows_bob.rows_count == 1);

    float gpa = std::any_cast<float>(rows_bob.data[0].columns.at("GPA"));
    assert(gpa == 3.4f);

    std::cout << "Test 5 OK.\n";

    // TEST 6: DELETE
    std::cout << "\n--- TEST 6: DELETE ---\n";

    DataDeletion delete_bob;
    delete_bob.table = "Student";
    delete_bob.conditions = { Condition("FullName", "=", std::string("Bob")) };

    assert(sm.delete_block(delete_bob) == 1);
    std::cout << "Test 6 OK.\n";

    // TEST 7: VERIFY DELETE
    std::cout << "\n--- TEST 7: VERIFY DELETE ---\n";

    rows_all = sm.read_block(read_all);
    print_rows(rows_all);
    assert(rows_all.rows_count == 1);
    std::cout << "Test 7 OK.\n";

    // INSERT EXTRA DATA for HASH INDEX testing
    std::cout << "\n--- INSERT EXTRA DATA ---\n";

    // Insert 4 more records + duplicates of StudentID 103
    auto insert_row = [&](int id, std::string name, float gpa) {
        DataWrite<Row> w;
        w.table = "Student";
        w.new_value.table_name = "Student";
        w.new_value.columns = {
            {"StudentID", id},
            {"FullName", name},
            {"GPA", gpa}
        };
        assert(sm.write_block(w) == 1);
    };

    insert_row(103, "Catherine Johnson", 3.95f);
    insert_row(104, "Daniel Robertson", 2.75f);
    insert_row(105, "Eleanora Vanderfeldt", 3.60f);

    // Duplicates for StudentID 103
    insert_row(103, "Cat Clone A", 3.10f);
    insert_row(103, "Cat Clone B", 3.20f);

    if (std::filesystem::exists(datafile)) {
        auto sz = std::filesystem::file_size(datafile);
        std::cout << "DEBUG: " << datafile << " size = " << sz << " bytes\n";
    } else {
        std::cout << "DEBUG: " << datafile << " tidak ada\n";
    }

    std::cout << "Extra data added.\n";

    // TEST 8: BUILD HASH INDEX
    std::cout << "\n--- TEST 8: BUILD INDEX (HASH: StudentID) ---\n";

    sm.set_index("Student", "StudentID", IndexType::HASH);

    std::string hash_index_file = test_dir + "/Student.StudentID.hashidx";
    assert(std::filesystem::exists(hash_index_file));
    std::cout << "Test 8 OK (index file created).\n";

    // TEST 9: INDEX SCAN QUERY
    std::cout << "\n--- TEST 9: INDEX SCAN for StudentID=103 ---\n";

    DataRetrieval read_idx;
    read_idx.table = "Student";
    read_idx.columns = {"FullName", "GPA"};
    read_idx.conditions = { Condition("StudentID", "=", 103) };
    read_idx.search_type = SearchType::INDEX_SCAN;
    read_idx.index_column = "StudentID";

    Rows<Row> rows103 = sm.read_block(read_idx);
    print_rows(rows103);

    // Must match 3 records
    assert(rows103.rows_count == 3);

    std::cout << "Test 9 OK (index scan works).\n";

    // TEST 10: VERIFY INDEX RETURNS EXACT OFFSETS
    std::cout << "\n--- TEST 10: VERIFY MULTIPLE OFFSETS ---\n";

    std::set<std::string> names;
    for (auto& row : rows103.data) {
        names.insert(std::any_cast<std::string>(row.columns.at("FullName")));
    }

    assert(names.count("Catherine Johnson") == 1);
    assert(names.count("Cat Clone A") == 1);
    assert(names.count("Cat Clone B") == 1);

    std::cout << "Test 10 OK.\n";

    // TEST 11: FALLBACK READ AFTER INDEX
    std::cout << "\n--- TEST 11: FALLBACK FULL SCAN ---\n";

    DataRetrieval read_fallback;
    read_fallback.table = "Student";
    read_fallback.columns = {"FullName"};
    read_fallback.conditions = { Condition("GPA", ">", 3.7f) };

    Rows<Row> rows_fallback = sm.read_block(read_fallback);
    print_rows(rows_fallback);

    assert(rows_fallback.rows_count >= 1);
    std::cout << "Test 11 OK.\n";

    // TEST 12: BUILD HASH INDEX on VARCHAR column
    std::cout << "\n--- TEST 12: BUILD INDEX (HASH: FullName VARCHAR) ---\n";

    sm.set_index("Student", "FullName", IndexType::HASH);

    std::string hash_index_file_name = test_dir + "/Student.FullName.hashidx";

    assert(std::filesystem::exists(hash_index_file_name));
    std::cout << "Test 12 OK (varchar hash index created).\n";


    // TEST 13: HASH INDEX SCAN for VARCHAR
    std::cout << "\n--- TEST 13: INDEX SCAN for FullName = 'Cat Clone B' ---\n";

    DataRetrieval read_idx_name;
    read_idx_name.table = "Student";
    read_idx_name.columns = {"*"};
    read_idx_name.conditions = { Condition("FullName", "=", std::string("Cat Clone B")) };
    read_idx_name.search_type = SearchType::INDEX_SCAN;
    read_idx_name.index_column = "FullName";

    Rows<Row> rows_name = sm.read_block(read_idx_name);
    print_rows(rows_name);

    // Should return exactly 1 row
    assert(rows_name.rows_count == 1);

    int returned_id =
        std::any_cast<int>(rows_name.data[0].columns.at("StudentID"));
    float returned_gpa =
        std::any_cast<float>(rows_name.data[0].columns.at("GPA"));

    assert(returned_id == 103);
    assert(returned_gpa == 3.20f);

    std::cout << "Test 13 OK (varchar index scan works).\n";

    // TEST 14: INDEX CONSISTENCY AFTER DELETE
    std::cout << "\n--- TEST 14: INDEX CONSISTENCY AFTER DELETE ---\n";

    // Delete one of the 103 rows ("Cat Clone A")
    DataDeletion delete_cloneA;
    delete_cloneA.table = "Student";
    delete_cloneA.conditions = { Condition("FullName", "=", std::string("Cat Clone A")) };

    assert(sm.delete_block(delete_cloneA) == 1);

    // Now query with index scan again
    DataRetrieval read_idx_after_del;
    read_idx_after_del.table = "Student";
    read_idx_after_del.columns = {"FullName", "GPA"};
    read_idx_after_del.conditions = { Condition("StudentID", "=", 103) };
    read_idx_after_del.search_type = SearchType::INDEX_SCAN;
    read_idx_after_del.index_column = "StudentID";

    Rows<Row> rows103_after = sm.read_block(read_idx_after_del);
    print_rows(rows103_after);

    // Should be exactly 2 rows now (only Catherine Johnson + Cat Clone B)
    assert(rows103_after.rows_count == 2);

    std::set<std::string> names_after;
    for (auto& row : rows103_after.data) {
        names_after.insert(std::any_cast<std::string>(row.columns.at("FullName")));
    }

    // The deleted row must be gone
    assert(names_after.count("Cat Clone A") == 0);
    // The remaining must still exist
    assert(names_after.count("Catherine Johnson") == 1);
    assert(names_after.count("Cat Clone B") == 1);

    std::cout << "Test 14 OK (index rebuilt correctly after delete).\n";

    // TEST 15: INDEX CONSISTENCY AFTER INSERT
    std::cout << "\n--- TEST 15: INDEX CONSISTENCY AFTER INSERT ---\n";

    // Insert new student with StudentID=200 (index on StudentID should update)
    DataWrite<Row> insert_new;
    insert_new.table = "Student";
    insert_new.new_value.table_name = "Student";
    insert_new.new_value.columns = {
        {"StudentID", 200},
        {"FullName", std::string("Zara Index Test")},
        {"GPA", 3.75f}
    };

    assert(sm.write_block(insert_new) == 1);

    // Query using index
    DataRetrieval read_idx_insert;
    read_idx_insert.table = "Student";
    read_idx_insert.columns = {"FullName", "GPA"};
    read_idx_insert.conditions = { Condition("StudentID", "=", 200) };
    read_idx_insert.search_type = SearchType::INDEX_SCAN;
    read_idx_insert.index_column = "StudentID";

    Rows<Row> rows_200 = sm.read_block(read_idx_insert);
    print_rows(rows_200);

    // Must return exactly 1 row
    assert(rows_200.rows_count == 1);
    assert(std::any_cast<std::string>(rows_200.data[0].columns.at("FullName")) == "Zara Index Test");

    std::cout << "Test 15 OK (index updated after insert).\n";

    // TEST 16: INDEX CONSISTENCY AFTER UPDATE (KEY CHANGE)
    std::cout << "\n--- TEST 16: INDEX CONSISTENCY AFTER UPDATE (KEY CHANGE) ---\n";

    // Update StudentID 104 -> 204
    DataWrite<Row> update_id;
    update_id.table = "Student";
    update_id.conditions = { Condition("StudentID", "=", 104) };
    update_id.columns = {"StudentID"};
    update_id.new_value.columns = { {"StudentID", 204} };

    assert(sm.write_block(update_id) == 1);

    // Query using old key (should return 0)
    DataRetrieval read_old_key;
    read_old_key.table = "Student";
    read_old_key.columns = {"*"};
    read_old_key.conditions = { Condition("StudentID", "=", 104) };
    read_old_key.search_type = SearchType::INDEX_SCAN;
    read_old_key.index_column = "StudentID";

    Rows<Row> rows_old = sm.read_block(read_old_key);
    print_rows(rows_old);
    assert(rows_old.rows_count == 0);

    // Query using new key (204)
    DataRetrieval read_new_key;
    read_new_key.table = "Student";
    read_new_key.columns = {"FullName", "GPA"};
    read_new_key.conditions = { Condition("StudentID", "=", 204) };
    read_new_key.search_type = SearchType::INDEX_SCAN;
    read_new_key.index_column = "StudentID";

    Rows<Row> rows_new = sm.read_block(read_new_key);
    print_rows(rows_new);

    // Should find exactly 1 row (same person but new ID)
    assert(rows_new.rows_count == 1);
    float gpa_new = std::any_cast<float>(rows_new.data[0].columns.at("GPA"));
    std::string name_new = std::any_cast<std::string>(rows_new.data[0].columns.at("FullName"));

    // Verify row is still Daniel Robertson
    assert(name_new == "Daniel Robertson");

    std::cout << "Test 16 OK (index updated correctly after key update).\n";

    // TEST 17: DROP Table
    std::cout << "\n--- TEST 17: DROP TABLE ---\n";
    sm.delete_table(student_schema);

    rows_all = sm.read_block(read_all);
    print_rows(rows_all);
    assert(rows_all.rows_count == 0);

    std::cout << "Test 17 OK (table dropped).\n";

    std::cout << "\n===== SEMUA TEST LULUS =====\n";
    return 0;

    
}

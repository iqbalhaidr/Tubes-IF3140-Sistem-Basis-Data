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
    student_schema.foreign_keys = {};
    sm.create_table(student_schema);

    TableSchema course_schema;
    course_schema.table_name = "Course";
    course_schema.column_names = {"CourseID", "Year", "CourseName"};
    course_schema.column_types = {DataType::INTEGER, DataType::INTEGER, DataType::VARCHAR};
    course_schema.column_sizes = {0, 0, 50};
    course_schema.primary_key = "CourseID";
    student_schema.foreign_keys = {};
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
    std::cout << "\nDropping student table\n";
    std::cout << "Can't get student schema error is expected:\n\n";
    sm.delete_table(student_schema);

    rows_all = sm.read_block(read_all);
    print_rows(rows_all);
    assert(rows_all.rows_count == 0);

    std::cout << "Test 17 OK (table dropped).\n";

    // TEST 18: get_tables()
    std::cout << "\n--- TEST 18: GET tables ---\n";
    std::vector<mdbms::TableSchema> schemas = sm.get_tables();
    std::cout << "Tables:" << std::endl;
    for (auto& schema_i : schemas) {
        std::cout << "  " << schema_i.table_name << std::endl;
        std::cout << "  columns:" << std::endl;
        for (int i = 0; i < schema_i.column_names.size(); i++) {
            std::cout << "    ";
            std::cout << schema_i.column_names[i] << " ";
            std::cout << static_cast<int>(schema_i.column_types[i]) << " ";
            std::cout << schema_i.column_sizes[i] << " ";
            std::cout << schema_i.column_names[i] << " ";
            std::cout <<  std::endl;
        }
        std::cout << "  primary key: " << schema_i.primary_key << std::endl;
        std::cout << "  foreign key: " << std::endl;
        for (auto& foreign_key : schema_i.foreign_keys) {
            std::cout << "    ";
            std::cout << foreign_key.first << " ";
            std::cout << foreign_key.second << " ";
            std::cout << std::endl;
        }

        std::cout <<  std::endl;
    }

    std::cout << "Test 18 OK . (maap perlu cek manual si)\n";

    // TEST 19: get_stats()
    std::cout << "\n--- TEST 19: GET STATS ---\n";
    std::map<std::string, mdbms::Statistic> stats = sm.get_stats();
    for (auto& stats_col : stats) {
        std::cout << stats_col.first << std::endl;
        std::cout << "n_r: " << stats_col.second.n_r << std::endl;
        std::cout << "b_r: " << stats_col.second.b_r << std::endl;
        std::cout << "l_r: " << stats_col.second.l_r << std::endl;
        std::cout << "f_r: " << stats_col.second.f_r << std::endl;
        std::cout << "V_a_r: " <<  std::endl;

        for (auto& col_V_a_r : stats_col.second.V_a_r) {
            std::cout << "  " << col_V_a_r.first << ": " << col_V_a_r.second <<  std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "Test 19 OK . (maap perlu cek manual si)\n";
    

    std::cout << "\n\n========================================\n";
    std::cout << "=== B+ TREE INDEX TESTS ===\n";
    std::cout << "========================================\n";

    // TEST 20: CREATE NEW TABLE FOR B+ TREE TESTS
    std::cout << "\n--- TEST 20: CREATE TABLE FOR B+ TREE ---\n";
    
    TableSchema product_schema;
    product_schema.table_name = "Product";
    product_schema.column_names = {"ProductID", "ProductName", "Price", "Stock"};
    product_schema.column_types = {DataType::INTEGER, DataType::VARCHAR, DataType::FLOAT, DataType::INTEGER};
    product_schema.column_sizes = {0, 100, 0, 0};
    product_schema.primary_key = "ProductID";
    product_schema.foreign_keys = {};
    sm.create_table(product_schema);
    
    std::cout << "Test 20 OK.\n";

    // TEST 21: INSERT DATA FOR B+ TREE
    std::cout << "\n--- TEST 21: INSERT PRODUCT DATA ---\n";
    
    auto insert_product = [&](int id, std::string name, float price, int stock) {
        DataWrite<Row> w;
        w.table = "Product";
        w.new_value.table_name = "Product";
        w.new_value.columns = {
            {"ProductID", id},
            {"ProductName", name},
            {"Price", price},
            {"Stock", stock}
        };
        assert(sm.write_block(w) == 1);
    };
    
    // Insert 20 products with various IDs
    insert_product(500, "Laptop", 15000000.0f, 10);
    insert_product(100, "Mouse", 150000.0f, 50);
    insert_product(300, "Keyboard", 500000.0f, 30);
    insert_product(200, "Monitor", 2500000.0f, 15);
    insert_product(400, "Headset", 750000.0f, 25);
    insert_product(150, "USB Cable", 50000.0f, 100);
    insert_product(250, "HDMI Cable", 75000.0f, 80);
    insert_product(350, "Webcam", 1200000.0f, 20);
    insert_product(450, "Microphone", 950000.0f, 18);
    insert_product(550, "Speakers", 1800000.0f, 12);
    
    // Insert duplicates for testing
    insert_product(300, "Keyboard Wireless", 650000.0f, 22);
    insert_product(300, "Keyboard Gaming", 1200000.0f, 15);
    insert_product(100, "Mouse Wireless", 200000.0f, 40);
    
    // More products
    insert_product(600, "Tablet", 5000000.0f, 8);
    insert_product(700, "Smartphone", 8000000.0f, 5);
    insert_product(800, "Smartwatch", 3500000.0f, 10);
    insert_product(50, "Power Bank", 350000.0f, 60);
    insert_product(650, "Router", 850000.0f, 25);
    insert_product(750, "Switch Hub", 450000.0f, 30);
    insert_product(850, "External HDD", 1500000.0f, 18);
    
    std::cout << "Test 21 OK (20 products inserted).\n";

    // TEST 22: BUILD B+ TREE INDEX
    std::cout << "\n--- TEST 22: BUILD B+ TREE INDEX (ProductID) ---\n";
    
    // Flush buffer to disk before building index
    std::cout << "Flushing buffer to disk...\n";
    sm.checkpoint();
    
    sm.set_index("Product", "ProductID", IndexType::BTREE);
    
    std::string bptree_index_file = test_dir + "/Product.ProductID.bpt";
    assert(std::filesystem::exists(bptree_index_file));
    
    std::cout << "Test 22 OK (B+ Tree index file created).\n";

    // TEST 23: B+ TREE INDEX SCAN - SINGLE VALUE
    std::cout << "\n--- TEST 23: B+ TREE INDEX SCAN (ProductID = 300) ---\n";
    
    DataRetrieval read_bpt_single;
    read_bpt_single.table = "Product";
    read_bpt_single.columns = {"ProductName", "Price"};
    read_bpt_single.conditions = { Condition("ProductID", "=", 300) };
    read_bpt_single.search_type = SearchType::INDEX_SCAN;
    read_bpt_single.index_column = "ProductID";
    
    Rows<Row> rows_300 = sm.read_block(read_bpt_single);
    print_rows(rows_300);
    
    // Should return 3 rows (Keyboard, Keyboard Wireless, Keyboard Gaming)
    assert(rows_300.rows_count == 3);
    
    std::set<std::string> product_names;
    for (auto& row : rows_300.data) {
        product_names.insert(std::any_cast<std::string>(row.columns.at("ProductName")));
    }
    
    assert(product_names.count("Keyboard") == 1);
    assert(product_names.count("Keyboard Wireless") == 1);
    assert(product_names.count("Keyboard Gaming") == 1);
    
    std::cout << "Test 23 OK (B+ Tree returns all duplicates).\n";

    // TEST 24: B+ TREE INDEX SCAN - DIFFERENT VALUES
    std::cout << "\n--- TEST 24: B+ TREE INDEX SCAN (ProductID = 500) ---\n";
    
    DataRetrieval read_bpt_500;
    read_bpt_500.table = "Product";
    read_bpt_500.columns = {"ProductName", "Price", "Stock"};
    read_bpt_500.conditions = { Condition("ProductID", "=", 500) };
    read_bpt_500.search_type = SearchType::INDEX_SCAN;
    read_bpt_500.index_column = "ProductID";
    
    Rows<Row> rows_500 = sm.read_block(read_bpt_500);
    print_rows(rows_500);
    
    assert(rows_500.rows_count == 1);
    assert(std::any_cast<std::string>(rows_500.data[0].columns.at("ProductName")) == "Laptop");
    assert(std::any_cast<float>(rows_500.data[0].columns.at("Price")) == 15000000.0f);
    
    std::cout << "Test 24 OK (B+ Tree single value lookup).\n";

    // TEST 25: B+ TREE INDEX SCAN - NON-EXISTENT VALUE
    std::cout << "\n--- TEST 25: B+ TREE INDEX SCAN (ProductID = 9999) ---\n";
    
    DataRetrieval read_bpt_none;
    read_bpt_none.table = "Product";
    read_bpt_none.columns = {"*"};
    read_bpt_none.conditions = { Condition("ProductID", "=", 9999) };
    read_bpt_none.search_type = SearchType::INDEX_SCAN;
    read_bpt_none.index_column = "ProductID";
    
    Rows<Row> rows_none = sm.read_block(read_bpt_none);
    print_rows(rows_none);
    
    assert(rows_none.rows_count == 0);
    
    std::cout << "Test 25 OK (B+ Tree handles non-existent keys).\n";

    // TEST 26: B+ TREE CONSISTENCY AFTER INSERT
    std::cout << "\n--- TEST 26: B+ TREE CONSISTENCY AFTER INSERT ---\n";
    
    insert_product(900, "Gaming Chair", 2500000.0f, 5);
    insert_product(950, "Standing Desk", 3500000.0f, 3);
    
    DataRetrieval read_bpt_900;
    read_bpt_900.table = "Product";
    read_bpt_900.columns = {"ProductName", "Price"};
    read_bpt_900.conditions = { Condition("ProductID", "=", 900) };
    read_bpt_900.search_type = SearchType::INDEX_SCAN;
    read_bpt_900.index_column = "ProductID";
    
    Rows<Row> rows_900 = sm.read_block(read_bpt_900);
    print_rows(rows_900);
    
    assert(rows_900.rows_count == 1);
    assert(std::any_cast<std::string>(rows_900.data[0].columns.at("ProductName")) == "Gaming Chair");
    
    std::cout << "Test 26 OK (B+ Tree index updated after insert).\n";

    // TEST 27: B+ TREE CONSISTENCY AFTER DELETE
    std::cout << "\n--- TEST 27: B+ TREE CONSISTENCY AFTER DELETE ---\n";
    
    // Delete one of the ProductID=300 entries
    DataDeletion delete_kbd_wireless;
    delete_kbd_wireless.table = "Product";
    delete_kbd_wireless.conditions = { Condition("ProductName", "=", std::string("Keyboard Wireless")) };
    
    assert(sm.delete_block(delete_kbd_wireless) == 1);
    
    // Query again for ProductID=300
    Rows<Row> rows_300_after = sm.read_block(read_bpt_single);
    print_rows(rows_300_after);
    
    // Should now return only 2 rows
    assert(rows_300_after.rows_count == 2);
    
    std::set<std::string> names_after_del;
    for (auto& row : rows_300_after.data) {
        names_after_del.insert(std::any_cast<std::string>(row.columns.at("ProductName")));
    }
    
    assert(names_after_del.count("Keyboard Wireless") == 0);
    assert(names_after_del.count("Keyboard") == 1);
    assert(names_after_del.count("Keyboard Gaming") == 1);
    
    std::cout << "Test 27 OK (B+ Tree index updated after delete).\n";

    // TEST 28: B+ TREE CONSISTENCY AFTER UPDATE (KEY CHANGE)
    std::cout << "\n--- TEST 28: B+ TREE CONSISTENCY AFTER UPDATE ---\n";
    
    // Update ProductID 100 -> 1000
    DataWrite<Row> update_mouse_id;
    update_mouse_id.table = "Product";
    update_mouse_id.conditions = { Condition("ProductName", "=", std::string("Mouse")) };
    update_mouse_id.columns = {"ProductID"};
    update_mouse_id.new_value.columns = { {"ProductID", 1000} };
    
    assert(sm.write_block(update_mouse_id) == 1);
    
    // Query with old key (should return only "Mouse Wireless")
    DataRetrieval read_bpt_100;
    read_bpt_100.table = "Product";
    read_bpt_100.columns = {"ProductName", "Price"};
    read_bpt_100.conditions = { Condition("ProductID", "=", 100) };
    read_bpt_100.search_type = SearchType::INDEX_SCAN;
    read_bpt_100.index_column = "ProductID";
    
    Rows<Row> rows_100_old = sm.read_block(read_bpt_100);
    print_rows(rows_100_old);
    
    assert(rows_100_old.rows_count == 1);
    assert(std::any_cast<std::string>(rows_100_old.data[0].columns.at("ProductName")) == "Mouse Wireless");
    
    // Query with new key
    DataRetrieval read_bpt_1000;
    read_bpt_1000.table = "Product";
    read_bpt_1000.columns = {"ProductName", "Price"};
    read_bpt_1000.conditions = { Condition("ProductID", "=", 1000) };
    read_bpt_1000.search_type = SearchType::INDEX_SCAN;
    read_bpt_1000.index_column = "ProductID";
    
    Rows<Row> rows_1000 = sm.read_block(read_bpt_1000);
    print_rows(rows_1000);
    
    assert(rows_1000.rows_count == 1);
    assert(std::any_cast<std::string>(rows_1000.data[0].columns.at("ProductName")) == "Mouse");
    
    std::cout << "Test 28 OK (B+ Tree index updated after key change).\n";

    // TEST 29: BUILD B+ TREE INDEX ON VARCHAR COLUMN
    std::cout << "\n--- TEST 29: BUILD B+ TREE INDEX (ProductName VARCHAR) ---\n";
    
    // Flush buffer to disk
    sm.checkpoint();
    
    sm.set_index("Product", "ProductName", IndexType::BTREE);
    
    std::string bptree_name_file = test_dir + "/Product.ProductName.bpt";
    assert(std::filesystem::exists(bptree_name_file));
    
    std::cout << "Test 29 OK (B+ Tree VARCHAR index created).\n";

    // TEST 30: B+ TREE INDEX SCAN ON VARCHAR
    std::cout << "\n--- TEST 30: B+ TREE INDEX SCAN (ProductName = 'Laptop') ---\n";
    
    DataRetrieval read_bpt_name;
    read_bpt_name.table = "Product";
    read_bpt_name.columns = {"ProductID", "Price", "Stock"};
    read_bpt_name.conditions = { Condition("ProductName", "=", std::string("Laptop")) };
    read_bpt_name.search_type = SearchType::INDEX_SCAN;
    read_bpt_name.index_column = "ProductName";
    
    Rows<Row> rows_laptop = sm.read_block(read_bpt_name);
    print_rows(rows_laptop);
    
    assert(rows_laptop.rows_count == 1);
    assert(std::any_cast<int>(rows_laptop.data[0].columns.at("ProductID")) == 500);
    assert(std::any_cast<float>(rows_laptop.data[0].columns.at("Price")) == 15000000.0f);
    
    std::cout << "Test 30 OK (B+ Tree VARCHAR index works).\n";

    // TEST 31: COMPARE B+ TREE VS HASH INDEX PERFORMANCE
    std::cout << "\n--- TEST 31: B+ TREE VS HASH INDEX COMPARISON ---\n";
    
    // Create another table for comparison
    TableSchema test_schema;
    test_schema.table_name = "TestPerf";
    test_schema.column_names = {"ID", "Value"};
    test_schema.column_types = {DataType::INTEGER, DataType::INTEGER};
    test_schema.column_sizes = {0, 0};
    test_schema.primary_key = "ID";
    test_schema.foreign_keys = {};
    sm.create_table(test_schema);
    
    // Insert 100 records
    for (int i = 1; i <= 100; i++) {
        DataWrite<Row> w;
        w.table = "TestPerf";
        w.new_value.table_name = "TestPerf";
        w.new_value.columns = {
            {"ID", i},
            {"Value", i * 10}
        };
        sm.write_block(w);
    }
    
    // Build both indexes (flush first to ensure data is on disk)
    sm.checkpoint();
    sm.set_index("TestPerf", "ID", IndexType::BTREE);
    sm.set_index("TestPerf", "Value", IndexType::HASH);
    
    std::string bpt_perf = test_dir + "/TestPerf.ID.bpt";
    std::string hash_perf = test_dir + "/TestPerf.Value.hashidx";
    
    assert(std::filesystem::exists(bpt_perf));
    assert(std::filesystem::exists(hash_perf));
    
    // Test B+ Tree lookup
    DataRetrieval read_bpt_perf;
    read_bpt_perf.table = "TestPerf";
    read_bpt_perf.columns = {"Value"};
    read_bpt_perf.conditions = { Condition("ID", "=", 50) };
    read_bpt_perf.search_type = SearchType::INDEX_SCAN;
    read_bpt_perf.index_column = "ID";
    
    Rows<Row> bpt_result = sm.read_block(read_bpt_perf);
    assert(bpt_result.rows_count == 1);
    assert(std::any_cast<int>(bpt_result.data[0].columns.at("Value")) == 500);
    
    // Test Hash lookup
    DataRetrieval read_hash_perf;
    read_hash_perf.table = "TestPerf";
    read_hash_perf.columns = {"ID"};
    read_hash_perf.conditions = { Condition("Value", "=", 500) };
    read_hash_perf.search_type = SearchType::INDEX_SCAN;
    read_hash_perf.index_column = "Value";
    
    Rows<Row> hash_result = sm.read_block(read_hash_perf);
    assert(hash_result.rows_count == 1);
    assert(std::any_cast<int>(hash_result.data[0].columns.at("ID")) == 50);
    
    std::cout << "Test 31 OK (Both B+ Tree and Hash indexes work correctly).\n";

    // TEST 32: B+ TREE WITH FLOAT KEYS
    std::cout << "\n--- TEST 32: B+ TREE INDEX ON FLOAT COLUMN ---\n";
    
    // Flush buffer to disk
    sm.checkpoint();
    
    sm.set_index("Product", "Price", IndexType::BTREE);
    
    std::string bptree_price_file = test_dir + "/Product.Price.bpt";
    assert(std::filesystem::exists(bptree_price_file));
    
    // Search by price
    DataRetrieval read_bpt_price;
    read_bpt_price.table = "Product";
    read_bpt_price.columns = {"ProductName", "Stock"};
    read_bpt_price.conditions = { Condition("Price", "=", 750000.0f) };
    read_bpt_price.search_type = SearchType::INDEX_SCAN;
    read_bpt_price.index_column = "Price";
    
    Rows<Row> rows_price = sm.read_block(read_bpt_price);
    print_rows(rows_price);
    
    assert(rows_price.rows_count >= 1);
    
    std::cout << "Test 32 OK (B+ Tree FLOAT index works).\n";

    std::cout << "\n========================================\n";
    std::cout << "=== ALL B+ TREE TESTS PASSED! ===\n";
    std::cout << "========================================\n";

    std::cout << "\n===== SEMUA TEST LULUS (HASH + B+ TREE) =====\n";
    return 0;
    
}

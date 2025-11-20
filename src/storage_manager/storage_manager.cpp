#include "storage_manager.h"

#include <algorithm>
#include <any>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace mdbms::sm {

StorageEngine::StorageEngine() : data_dir_("data") {}

StorageEngine::StorageEngine(const std::string& data_dir) : data_dir_(data_dir) {}

Rows<Row> StorageEngine::read_block(const DataRetrieval& retrieval) {
    Rows<Row> result;
    TableSchema schema;
    try {
        schema = getSchema(retrieval.table);
    } catch (const std::runtime_error& e) {
        std::cerr << "SM: Error - " << e.what() << std::endl;
        return result;
    }

    // Variabel for index search
    std::vector<int64_t> offsets;
    bool used_index = false;

    // Equality condition on colum (if exists)
    const Condition* eq_cond = nullptr;
    for (const auto &c : retrieval.conditions) {
        if (c.operation == "=") {
            eq_cond = &c;
            break;
        }
    }

    // Search using INDEX
    if (retrieval.search_type == SearchType::INDEX_SCAN && !retrieval.index_column.empty()) {
        // Condition matched with index column
        const Condition* cond_for_index = nullptr;
        for (const auto &c : retrieval.conditions) {
            if (c.column == retrieval.index_column && c.operation == "=") {
                cond_for_index = &c;
                break;
            }
        }
        if (cond_for_index) {
            if (lookup_index(retrieval.table, retrieval.index_column, cond_for_index->operand, offsets)) {
                used_index = true;
            } else {
                used_index = false; // fallback
            }
        } else {
            used_index = false; // no equality condition -> fallback
        } 
    } else {
        //Search with index if there's equality condition
        if (eq_cond) {
            if (lookup_index(retrieval.table, eq_cond->column, eq_cond->operand, offsets)) {
                used_index = true;
            } else {
                used_index = false;
            }
        }
    }

    if (used_index && !offsets.empty()) {
        return read_using_offsets(retrieval, offsets);
    }

    return full_scan(retrieval); // fallback
}

Rows<Row> StorageEngine::read_using_offsets(const DataRetrieval& retrieval, const std::vector<int64_t>& offsets) {
    Rows<Row> result;
    TableSchema schema;
    try {
        schema = getSchema(retrieval.table);
    } catch (const std::runtime_error& e) {
        std::cerr << "SM: Error - " << e.what() << std::endl;
        return result;
    }

    std::string datafile = data_dir_ + "/" + retrieval.table + ".dat";
    std::ifstream din(datafile, std::ios::binary);
    if (!din.is_open()) {
        std::cerr << "SM: Gagal membuka file data: " << datafile << std::endl;
        return result;
    }

    bool select_all = std::find(retrieval.columns.begin(), retrieval.columns.end(), "*") != retrieval.columns.end();

    for (int64_t off : offsets) {
        if (off < 0) continue;
        din.clear();
        din.seekg(off, std::ios::beg);
        if (!din.good()) continue;

        Row r = deserialize_row(din, schema);
        if (r.columns.empty()) continue;

        if (!check_conditions(r, retrieval.conditions)) continue;

        Row projected;
        projected.table_name = retrieval.table;
        if (select_all) {
            projected = r;
        } else {
            for (const auto &col : retrieval.columns) {
                if (r.columns.count(col)) projected.columns[col] = r.columns.at(col);
            }
        }
        result.data.push_back(projected);
    }
    din.close();
    result.rows_count = result.data.size();
    return result;
}

Rows<Row> StorageEngine::full_scan(const DataRetrieval& retrieval) {
    Rows<Row> result;
    TableSchema schema;
    try {
        schema = getSchema(retrieval.table);
    } catch (const std::runtime_error& e) {
        std::cerr << "SM: Error - " << e.what() << std::endl;
        return result;
    }

    std::string filename = data_dir_ + "/" + retrieval.table + ".dat";
    std::ifstream file(filename, std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "SM: Gagal membuka file: " << filename << std::endl;
        return result;
    }

    bool select_all = std::find(retrieval.columns.begin(), retrieval.columns.end(), "*") != retrieval.columns.end();

    while (file.peek() != EOF) {
        Row row = deserialize_row(file, schema);
        if (row.columns.empty()) {
            if (file.eof()) break;
            std::cerr << "SM: Error membaca baris data." << std::endl;
            break;
        }

        if (check_conditions(row, retrieval.conditions)) {
            Row projected_row;
            projected_row.table_name = retrieval.table;

            if (select_all) {
                projected_row = row;
            } else {
                for (const auto& col_name : retrieval.columns) {
                    if (row.columns.count(col_name)) {
                        projected_row.columns[col_name] = row.columns.at(col_name);
                    }
                }
            }
            result.data.push_back(projected_row);
        }
    }

    file.close();
    result.rows_count = result.data.size();
    return result;
}

int StorageEngine::write_block(const DataWrite<Row>& write) {
    TableSchema schema;
    try {
        schema = getSchema(write.table);
    } catch (const std::runtime_error& e) {
        std::cerr << "SM: Error - " << e.what() << std::endl;
        return 0;
    }

    std::string filename = data_dir_ + "/" + write.table + ".dat";

    // INSERT no condition
    if (write.conditions.empty()) {
        std::ofstream file(filename, std::ios::binary | std::ios::app);

        if (!file.is_open()) {
            std::cerr << "SM: Gagal membuka file: " << filename << std::endl;
            return 0;
        }

        file.seekp(0, std::ios::end);
        int64_t row_offset = file.tellp();
        serialize_row(file, write.new_value, schema);
        file.close();

        if (table_index.count(write.table)) {
            std::string col = table_index[write.table];
            update_index_after_insert(
                write.table, col, row_offset, write.new_value
            );
        }

        return 1;
    }

    // UPDATE
    std::string temp_filename = data_dir_ + "/" + write.table + ".tmp";

    std::ifstream infile(filename, std::ios::binary);
    std::ofstream outfile(temp_filename, std::ios::binary);

    if (!infile.is_open() || !outfile.is_open()) {
        std::cerr << "SM: Gagal membuka file (temp) untuk UPDATE" << std::endl;
        return 0;
    }

    int affected_rows = 0;
    int64_t current_offset = 0;

    while (true) {
        current_offset = infile.tellg();
        if (!infile.good()) break;

        Row old_row = deserialize_row(infile, schema);
        if (!infile.good()) break;

        Row new_row = old_row;

        if (check_conditions(old_row, write.conditions)) {
            // UPDATE
            for (const auto& col_name : write.columns) {
                if (write.new_value.columns.count(col_name)) {
                    new_row.columns[col_name] =
                        write.new_value.columns.at(col_name);
                }
            }
            affected_rows++;

            if (this->table_index.count(write.table)) {
                std::string col = table_index[write.table];
                update_index_after_update(
                    write.table, col, current_offset, old_row, new_row
                );
            }
        }
        serialize_row(outfile, new_row, schema);
    }

    infile.close();
    outfile.close();

    std::remove(filename.c_str());
    std::rename(temp_filename.c_str(), filename.c_str());

    return affected_rows;
}

int StorageEngine::delete_block(const DataDeletion& deletion) {
    TableSchema schema;
    try {
        schema = getSchema(deletion.table);
    } catch (const std::runtime_error& e) {
        std::cerr << "SM: Error - " << e.what() << std::endl;
        return 0;
    }

    std::string table = deletion.table;
    std::string filename = data_dir_ + "/" + table + ".dat";
    std::string temp_filename = data_dir_ + "/" + table + ".tmp";

    std::ifstream infile(filename, std::ios::binary);
    std::ofstream outfile(temp_filename, std::ios::binary);

    if (!infile.is_open() || !outfile.is_open()) {
        std::cerr << "SM: Gagal membuka file (temp) untuk DELETE" << std::endl;
        return 0;
    }

    int affected_rows = 0;
    int64_t row_offset = 0;

    while (true) {
        row_offset = infile.tellg();
        if (infile.peek() == EOF) break;

        Row row = deserialize_row(infile, schema);

        if (row.columns.empty()) {
            if (infile.eof()) break;
            continue;
        }

        if (check_conditions(row, deletion.conditions)) {
            // Update Index
            if (table_index.count(table)) {
                std::string col = table_index[table];
                update_index_after_delete(table, col, row_offset, row);
            }

            affected_rows++;
        } else {
            serialize_row(outfile, row, schema);
        }
    }

    infile.close();
    outfile.close();

    if (std::remove(filename.c_str()) != 0) {
        std::cerr << "SM: Gagal menghapus file asli: " << filename << std::endl;
        std::remove(temp_filename.c_str());
        return -1;
    }
    if (std::rename(temp_filename.c_str(), filename.c_str()) != 0) {
        std::cerr << "SM: Gagal mengganti nama file temp: " << temp_filename << std::endl;
        return -1;
    }

    return affected_rows;
}

TableSchema StorageEngine::getSchema(const std::string& table) {
    // Hardcoded untuk testing
    if (table == "Student") {
        TableSchema schema;
        schema.table_name = "Student";
        schema.column_names = {"StudentID", "FullName", "GPA"};
        schema.column_types = {DataType::INTEGER, DataType::VARCHAR, DataType::FLOAT};
        schema.column_sizes = {0, 50, 0};
        schema.primary_key = "StudentID";
        return schema;
    }
    if (table == "Course") {
        TableSchema schema;
        schema.table_name = "Course";
        schema.column_names = {"CourseID", "Year", "CourseName"};
        schema.column_types = {DataType::INTEGER, DataType::INTEGER, DataType::VARCHAR};
        schema.column_sizes = {0, 0, 50};
        schema.primary_key = "CourseID";
        return schema;
    }
    throw std::runtime_error("Skema tidak ditemukan untuk tabel: " + table);
}

void StorageEngine::serialize_row(std::ostream& out, const Row& row, const TableSchema& schema) {
    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        const std::string& col_name = schema.column_names[i];
        DataType col_type = schema.column_types[i];

        if (!row.columns.count(col_name)) {
            std::cerr << "SM: Kolom hilang saat serialisasi: " << col_name << std::endl;
            throw std::runtime_error("Kolom hilang saat serialisasi: " + col_name);
        }

        const std::any& value = row.columns.at(col_name);

        try {
            if (col_type == DataType::INTEGER) {
                int32_t val = std::any_cast<int>(value);
                out.write(reinterpret_cast<const char*>(&val), sizeof(val));
                if (!out) {
                    std::cerr << "SM: Error menulis INTEGER ke file." << std::endl;
                    throw std::runtime_error("Gagal menulis INTEGER ke file");
                }
            }
            else if (col_type == DataType::FLOAT) {
                float val = std::any_cast<float>(value);
                out.write(reinterpret_cast<const char*>(&val), sizeof(val));
                if (!out) {
                    std::cerr << "SM: Error menulis FLOAT ke file." << std::endl;
                    throw std::runtime_error("Gagal menulis FLOAT ke file");
                }
            }
            else if (col_type == DataType::VARCHAR || col_type == DataType::CHAR) {
                std::string str = std::any_cast<std::string>(value);
                uint32_t len = static_cast<uint32_t>(str.length());
                out.write(reinterpret_cast<const char*>(&len), sizeof(len));
                if (!out) {
                    std::cerr << "SM: Error menulis panjang string ke file." << std::endl;
                    throw std::runtime_error("Gagal menulis panjang string ke file");
                }
                out.write(str.c_str(), len);
                if (!out) {
                    std::cerr << "SM: Error menulis data string ke file." << std::endl;
                    throw std::runtime_error("Gagal menulis data string ke file");
                }
            }
        } catch (const std::bad_any_cast& e) {
            std::cerr << "SM: Tipe data tidak cocok untuk kolom " << col_name << ": " << e.what() << std::endl;
            throw std::runtime_error("Tipe data tidak cocok untuk kolom " + col_name);
        }
    }
}

Row StorageEngine::deserialize_row(std::istream& in, const TableSchema& schema) {
    Row row;
    row.table_name = schema.table_name;

    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        const std::string& col_name = schema.column_names[i];
        DataType col_type = schema.column_types[i];

    if (!in) {
        if (!in.eof()) {
            std::cerr << "SM: Stream error saat baca kolom " << col_name << std::endl;
        }
        return Row();
    }

    if (in.peek() == EOF) {
        return Row(); // EOF normal
    }

        try {
            if (col_type == DataType::INTEGER) {
                int32_t val;
                in.read(reinterpret_cast<char*>(&val), sizeof(val));
                if (in.gcount() != sizeof(val) || !in) {
                    std::cerr << "SM: Error membaca INTEGER pada kolom " << col_name << std::endl;
                    return Row();
                }
                row.columns[col_name] = static_cast<int>(val);
            }
            else if (col_type == DataType::FLOAT) {
                float val;
                in.read(reinterpret_cast<char*>(&val), sizeof(val));
                if (in.gcount() != sizeof(val) || !in) {
                    std::cerr << "SM: Error membaca FLOAT pada kolom " << col_name << std::endl;
                    return Row();
                }
                row.columns[col_name] = val;
            }
            else if (col_type == DataType::VARCHAR || col_type == DataType::CHAR) {
                uint32_t len;
                in.read(reinterpret_cast<char*>(&len), sizeof(len));
                if (in.gcount() != sizeof(len) || !in) {
                    std::cerr << "SM: Error membaca panjang string pada kolom " << col_name << std::endl;
                    return Row();
                }
                std::string str(len, '\0');
                in.read(&str[0], len);
                if (in.gcount() != static_cast<std::streamsize>(len) || !in) {
                    std::cerr << "SM: Error membaca data string pada kolom " << col_name << std::endl;
                    return Row();
                }
                row.columns[col_name] = str;
            }
        } catch (...) {
            std::cerr << "SM: Exception saat deserialisasi kolom " << col_name << std::endl;
            return Row();
        }
    }
    return row;
}

bool StorageEngine::check_conditions(const Row& row, const std::vector<Condition>& conditions) {
    if (conditions.empty()) {
        return true; 
    }
    
    for (const auto& cond : conditions) {
        if (!row.columns.count(cond.column)) return false;

        try {
            const std::any& left_value = row.columns.at(cond.column);
            const std::any& right_value = cond.operand;

            bool match = false;

            // Try INTEGER comparison
            try {
                int left_int = std::any_cast<int>(left_value);
                int right_int = std::any_cast<int>(right_value);
                
                if (cond.operation == "=") match = (left_int == right_int);
                else if (cond.operation == "!=") match = (left_int != right_int);
                else if (cond.operation == ">") match = (left_int > right_int);
                else if (cond.operation == ">=") match = (left_int >= right_int);
                else if (cond.operation == "<") match = (left_int < right_int);
                else if (cond.operation == "<=") match = (left_int <= right_int);
                
                if (!match) return false;
                continue;
            } catch (const std::bad_any_cast&) {}

            // Try FLOAT comparison
            try {
                float left_float = std::any_cast<float>(left_value);
                float right_float = std::any_cast<float>(right_value);
                
                if (cond.operation == "=") match = (left_float == right_float);
                else if (cond.operation == "!=") match = (left_float != right_float);
                else if (cond.operation == ">") match = (left_float > right_float);
                else if (cond.operation == ">=") match = (left_float >= right_float);
                else if (cond.operation == "<") match = (left_float < right_float);
                else if (cond.operation == "<=") match = (left_float <= right_float);
                
                if (!match) return false;
                continue;
            } catch (const std::bad_any_cast&) {}

            // Try STRING comparison
            try {
                std::string left_str = std::any_cast<std::string>(left_value);
                std::string right_str = std::any_cast<std::string>(right_value);
                
                if (cond.operation == "=") match = (left_str == right_str);
                else if (cond.operation == "!=") match = (left_str != right_str);
                else if (cond.operation == ">") match = (left_str > right_str);
                else if (cond.operation == ">=") match = (left_str >= right_str);
                else if (cond.operation == "<") match = (left_str < right_str);
                else if (cond.operation == "<=") match = (left_str <= right_str);
                
                if (!match) return false;
                continue;
            } catch (const std::bad_any_cast&) {}

            // Bila tidak ada type yang cocok, return false
            std::cerr << "SM: Tipe data tidak dapat dibandingkan untuk kolom " << cond.column << std::endl;
            return false;

        } catch (const std::exception& e) {
            std::cerr << "SM: Error saat evaluasi kondisi: " << e.what() << std::endl;
            return false;
        }
    }
    return true; 
}

void StorageEngine::update_index_after_insert(const std::string& table, const std::string& column, int64_t row_offset, const Row& row) {
    auto it = row.columns.find(column);
    if (it == row.columns.end()) return;

    std::string keystr;
    int32_t iv; float fv;
    const std::any &v = it->second;

    if (any_to_string(v, keystr)) {}
    else if (any_to_int32(v, iv)) keystr = std::to_string(iv);
    else if (any_to_float(v, fv)) keystr = std::to_string(fv);
    else return;

    std::string idxfile = data_dir_ + "/" + table + "." + column + ".hashidx";
    std::fstream out(idxfile, std::ios::binary | std::ios::in | std::ios::out);
    if (!out.is_open()) return;

    // Skip header
    out.seekg(0);
    char header[4];
    out.read(header, 4);

    uint8_t idx_type; out.read((char*)&idx_type, 1);
    uint8_t dtype;    out.read((char*)&dtype, 1);

    uint32_t bucket_count;
    out.read((char*)&bucket_count, 4);

    // Directory start
    int64_t dir_start = out.tellg();

    std::vector<int64_t> bucket_pos(bucket_count);
    for (uint32_t i = 0; i < bucket_count; i++)
        out.read((char*)&bucket_pos[i], 8);

    // Determine bucket
    size_t h = std::hash<std::string>{}(keystr);
    uint32_t bucket = h % bucket_count;

    int64_t pos = bucket_pos[bucket];

    // If empty bucket, create new bucket at end
    if (pos == 0) {
        out.seekp(0, std::ios::end);
        pos = out.tellp();
        bucket_pos[bucket] = pos;

        uint32_t key_count = 1;
        out.write((char*)&key_count, 4);

        uint32_t len = keystr.size();
        out.write((char*)&len, 4);
        out.write(keystr.data(), len);

        uint32_t cnt = 1;
        out.write((char*)&cnt, 4);
        out.write((char*)&row_offset, 8);
    }
    else {
        out.seekg(pos);

        uint32_t key_count;
        out.read((char*)&key_count, 4);

        // Buffer bucket content
        struct Item { std::string key; std::vector<int64_t> offs; };
        std::vector<Item> items;

        for (uint32_t i = 0; i < key_count; i++) {
            uint32_t len;
            out.read((char*)&len, 4);

            std::string k(len, '\0');
            out.read(&k[0], len);

            uint32_t cnt;
            out.read((char*)&cnt, 4);

            std::vector<int64_t> offs(cnt);
            for (uint32_t j = 0; j < cnt; j++)
                out.read((char*)&offs[j], 8);

            items.push_back({k, offs});
        }

        // Inject new offset
        bool found = false;
        for (auto& it : items) {
            if (it.key == keystr) {
                it.offs.push_back(row_offset);
                found = true;
                break;
            }
        }
        if (!found) {
            items.push_back({keystr, {row_offset}});
            key_count++;
        }

        // Rewrite bucket
        out.seekp(pos);

        out.write((char*)&key_count, 4);
        for (auto& it : items) {
            uint32_t len = it.key.size();
            out.write((char*)&len, 4);
            out.write(it.key.data(), len);

            uint32_t cnt = it.offs.size();
            out.write((char*)&cnt, 4);
            for (auto off : it.offs)
                out.write((char*)&off, 8);
        }
    }

    // Rewrite directory
    out.seekp(dir_start);
    for (auto p : bucket_pos)
        out.write((char*)&p, 8);

    out.close();
}

void StorageEngine::update_index_after_update(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row, const Row& new_row){
    auto A = old_row.columns.at(column);
    auto B = new_row.columns.at(column);

    // Skip jika column yang diupdate tidak mempengaruhi index
    if (A.type() == B.type()) {
        if (A.type() == typeid(int)    && std::any_cast<int>(A) == std::any_cast<int>(B)) return;
        if (A.type() == typeid(float)  && std::any_cast<float>(A) == std::any_cast<float>(B)) return;
        if (A.type() == typeid(std::string) && std::any_cast<std::string>(A) == std::any_cast<std::string>(B)) return;
    }

    // Key changed -> remove + insert
    update_index_after_delete(table, column, row_offset, old_row);
    update_index_after_insert(table, column, row_offset, new_row);
}

void StorageEngine::update_index_after_delete(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row) {
    auto it = old_row.columns.find(column);
    if (it == old_row.columns.end()) return;

    std::string keystr;
    int32_t iv; 
    float fv;

    const std::any &v = it->second;

    if (any_to_string(v, keystr)) {}
    else if (any_to_int32(v, iv)) keystr = std::to_string(iv);
    else if (any_to_float(v, fv)) keystr = std::to_string(fv);
    else return;

    std::string idxfile = data_dir_ + "/" + table + "." + column + ".hashidx";
    std::fstream io(idxfile, std::ios::binary | std::ios::in | std::ios::out);
    if (!io.is_open()) return;

    // Readh Header
    io.seekg(0);
    char magic[4];
    io.read(magic, 4);

    uint8_t idx_type;
    io.read((char*)&idx_type, 1);

    uint8_t dtype;
    io.read((char*)&dtype, 1);

    uint32_t bucket_count;
    io.read((char*)&bucket_count, 4);

    int64_t dir_start = io.tellg();

    std::vector<int64_t> bucket_pos(bucket_count);
    for (uint32_t i = 0; i < bucket_count; i++)
        io.read((char*)&bucket_pos[i], 8);

    // Compute bucket
    size_t h = std::hash<std::string>{}(keystr);
    uint32_t bucket = h % bucket_count;

    int64_t pos = bucket_pos[bucket];
    if (pos == 0) {
        io.close();
        return; // bucket kosong
    }

    // Read Bucket Content
    io.seekg(pos);

    uint32_t key_count;
    io.read((char*)&key_count, 4);

    struct Item { std::string key; std::vector<int64_t> offs; };
    std::vector<Item> items;

    for (uint32_t i = 0; i < key_count; i++) {
        uint32_t len;
        io.read((char*)&len, 4);

        std::string k(len, '\0');
        io.read(&k[0], len);

        uint32_t cnt;
        io.read((char*)&cnt, 4);

        std::vector<int64_t> offs(cnt);
        for (uint32_t j = 0; j < cnt; j++)
            io.read((char*)&offs[j], 8);

        items.push_back({ k, offs });
    }

    // Delete Offset
    bool modified = false;

    for (auto &it : items) {
        if (it.key == keystr) {
            auto &vec = it.offs;

            vec.erase(
                std::remove(vec.begin(), vec.end(), row_offset),
                vec.end()
            );

            modified = true;

            if (vec.empty()) {
                items.erase(
                    std::remove_if(
                        items.begin(), items.end(),
                        [&](const Item& x){ return x.key == keystr; }
                    ),
                    items.end()
                );
            }
            break;
        }
    }

    if (!modified) {
        io.close();
        return; // tidak berubah
    }

    // Rewrite Bucket
    io.seekp(pos, std::ios::beg);

    uint32_t new_key_count = items.size();
    io.write((char*)&new_key_count, 4);

    for (auto &it : items) {
        uint32_t len = it.key.size();
        io.write((char*)&len, 4);
        io.write(it.key.data(), len);

        uint32_t cnt = it.offs.size();
        io.write((char*)&cnt, 4);

        for (auto off : it.offs)
            io.write((char*)&off, 8);
    }

    // Rewrite Directory
    io.seekp(dir_start);
    for (auto p : bucket_pos)
        io.write((char*)&p, 8);

    io.close();
}

// Implementasi Indexing
void StorageEngine::set_index(const std::string& table, const std::string& column, const IndexType index_type) {
    table_index[table] = column;
    table_index_type[table] = index_type;

    TableSchema schema;
    try {
        schema = getSchema(table);
    } catch (const std::exception &e) {
        std::cerr << "SM: Schema error.\n";
        return;
    }

    if (index_type == IndexType::HASH) {
        build_hash_index(schema, table, column);
    } else {
        // TODO: call b+tree here
    };
}

static constexpr uint32_t HASH_BUCKET_COUNT = 1024;

void StorageEngine::build_hash_index(const TableSchema& schema, const std::string& table, const std::string& column) {
    std::string datafile = data_dir_ + "/" + table + ".dat";
    std::string idxfile  = data_dir_ + "/" + table + "." + column + ".hashidx";

    std::ifstream file(datafile, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "SM: Gagal membuka " << datafile << " untuk index.\n";
        return;
    }

    // Find column
    int col_idx = -1;
    DataType type = DataType::VARCHAR;

    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        if (schema.column_names[i] == column) {
            col_idx = i;
            type    = schema.column_types[i];
            break;
        }
    }

    if (col_idx == -1) {
        std::cerr << "SM: Kolom tidak ditemukan.\n";
        return;
    }

    // Buckets
    std::vector<std::unordered_map<std::string, std::vector<int64_t>>> buckets(HASH_BUCKET_COUNT);

    while (true) {
        std::streampos pos = file.tellg();
        if (!file.good()) break;

        Row row = deserialize_row(file, schema);
        if (!file.good()) break;

        auto it = row.columns.find(column);
        if (it == row.columns.end()) continue;

        std::string keystr;
        int32_t iv; float fv;

        const std::any &v = it->second;

        if (any_to_string(v, keystr)) {
            // ok
        } else if (any_to_int32(v, iv)) {
            keystr = std::to_string(iv);
        } else if (any_to_float(v, fv)) {
            keystr = std::to_string(fv);
        } else {
            continue;
        }

        size_t h = std::hash<std::string>{}(keystr);
        uint32_t bucket_id = h % HASH_BUCKET_COUNT;

        buckets[bucket_id][keystr].push_back((int64_t)pos);
    }

    file.close();

    // Make file index
    std::ofstream out(idxfile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "SM: Tidak bisa membuat index file.\n";
        return;
    }

    // HEADER
    out.write("HIDX", 4);

    uint8_t idx_type = 0;    // 0 = hash
    out.write((char*)&idx_type, 1);

    uint8_t dtype = (type == DataType::INTEGER) ? 0 :
                    (type == DataType::FLOAT)   ? 1 : 2;
    out.write((char*)&dtype, 1);

    uint32_t bucket_count = HASH_BUCKET_COUNT;
    out.write((char*)&bucket_count, 4);

    // Bucket directory
    std::vector<int64_t> bucket_pos(bucket_count, 0);
    int64_t dir_start = out.tellp();

    for (uint32_t i = 0; i < bucket_count; i++) {
        int64_t zero = 0;
        out.write((char*)&zero, 8);
    }

    // BUCKET DATA
    for (uint32_t b = 0; b < bucket_count; b++) {
        if (buckets[b].empty()) continue;

        bucket_pos[b] = (int64_t)out.tellp();

        uint32_t key_count = buckets[b].size();
        out.write((char*)&key_count, 4);

        for (auto &kv : buckets[b]) {
            const std::string &key = kv.first;
            const std::vector<int64_t> &offs = kv.second;

            uint32_t len = key.size();
            out.write((char*)&len, 4);
            out.write(key.data(), len);

            uint32_t cnt = offs.size();
            out.write((char*)&cnt, 4);

            for (int64_t o : offs) {
                out.write((char*)&o, 8);
            }
        }
    }

    // UPDATE DIRECTORY
    int64_t endpos = out.tellp();
    out.seekp(dir_start);

    for (auto p : bucket_pos) {
        out.write((char*)&p, 8);
    }

    out.seekp(endpos);
    out.close();

    std::cout << "SM: Hash index selesai: " << idxfile << "\n";
}


bool StorageEngine::lookup_index(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets) {
    // Check which index file exists for the table.column
    std::string hashfile = data_dir_ + "/" + table + "." + column + ".hashidx";
    std::ifstream htest(hashfile, std::ios::binary);
    if (htest.is_open()) {
        htest.close();
        return lookup_hash(table, column, operand, out_offsets);
    }

    std::string bptfile = data_dir_ + "/" + table + "." + column + ".bpt";
    std::ifstream btest(bptfile, std::ios::binary);
    if (btest.is_open()) {
        btest.close();
        // TO DO: return lookup for b+tree
    }
    return false; // no index
}

bool StorageEngine::lookup_hash(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets) {
    out_offsets.clear();
    std::string idxfile = data_dir_ + "/" + table + "." + column + ".hashidx";
    std::ifstream in(idxfile, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    // Read header
    char magic[4];
    in.read(magic, 4);
    if (in.gcount() != 4 || std::string(magic, 4) != "HIDX") {
        in.close();
        return false;
    }

    uint8_t index_type_id = 0;
    in.read(reinterpret_cast<char*>(&index_type_id), 1);
    if (!in) { in.close(); return false; }

    uint8_t dtype = 2;
    in.read(reinterpret_cast<char*>(&dtype), 1);
    if (!in) { in.close(); return false; }

    uint32_t bucket_count = 0;
    in.read(reinterpret_cast<char*>(&bucket_count), 4);
    if (!in) { in.close(); return false; }
    if (bucket_count == 0) { in.close(); return false; }

    // Read directory
    std::vector<int64_t> directory(bucket_count);
    for (uint32_t i = 0; i < bucket_count; ++i) {
        int64_t off = 0;
        in.read(reinterpret_cast<char*>(&off), 8);
        directory[i] = off;
    }

    // Convert operand to string key
    std::string keystr;
    if (!any_to_string(operand, keystr)) {
        int32_t iv; float fv;
        if (any_to_int32(operand, iv)) keystr = std::to_string(iv);
        else if (any_to_float(operand, fv)) {
            keystr = std::to_string(fv);
        } else {
            in.close();
            return false;
        }
    }

    // Compute bucket
    size_t h = std::hash<std::string>{}(keystr);
    uint32_t bucket_id = static_cast<uint32_t>(h % bucket_count);

    int64_t bucket_pos = directory[bucket_id];
    if (bucket_pos == 0) { in.close(); return false; } // empty bucket

    // Seek to bucket
    in.seekg(bucket_pos, std::ios::beg);
    if (!in.good()) { in.close(); return false; }

    // Read key_count
    uint32_t key_count = 0;
    in.read(reinterpret_cast<char*>(&key_count), 4);
    if (!in) { in.close(); return false; }

    for (uint32_t k = 0; k < key_count; ++k) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), 4);
        if (!in) { in.close(); return false; }

        std::string kstr(len, '\0');
        if (len > 0) in.read(&kstr[0], len);
        if (!in) { in.close(); return false; }

        uint32_t cnt = 0;
        in.read(reinterpret_cast<char*>(&cnt), 4);
        if (!in) { in.close(); return false; }

        std::vector<int64_t> offsets(cnt);
        for (uint32_t j = 0; j < cnt; ++j) {
            in.read(reinterpret_cast<char*>(&offsets[j]), 8);
            if (!in) { in.close(); return false; }
        }

        if (kstr == keystr) {
            out_offsets = std::move(offsets);
            in.close();
            return true;
        }
    }

    in.close();
    return false; // not found in bucket
}


// Helper untuk Index
bool StorageEngine::any_to_int32(const std::any &a, int32_t &out) {
    if (!a.has_value()) return false;
    try {
        if (a.type() == typeid(int32_t)) { out = std::any_cast<int32_t>(a); return true; }
        if (a.type() == typeid(int)) { out = static_cast<int32_t>(std::any_cast<int>(a)); return true; }
        if (a.type() == typeid(int64_t)) { out = static_cast<int32_t>(std::any_cast<int64_t>(a)); return true; }
        if (a.type() == typeid(uint32_t)) { out = static_cast<int32_t>(std::any_cast<uint32_t>(a)); return true; }
    } catch (const std::bad_any_cast&) { return false; }
    return false;
}

bool StorageEngine::any_to_float(const std::any &a, float &out) {
    if (!a.has_value()) return false;
    try {
        if (a.type() == typeid(float)) { out = std::any_cast<float>(a); return true; }
        if (a.type() == typeid(double)) { out = static_cast<float>(std::any_cast<double>(a)); return true; }
    } catch (const std::bad_any_cast&) { return false; }
    return false;
}

bool StorageEngine::any_to_string(const std::any &a, std::string &out) {
    if (!a.has_value()) return false;
    try {
        if (a.type() == typeid(std::string)) { out = std::any_cast<std::string>(a); return true; }
        if (a.type() == typeid(const char*)) { out = std::any_cast<const char*>(a); return true; }
    } catch (const std::bad_any_cast&) { return false; }
    return false;
}
} // namespace mdbms::sm
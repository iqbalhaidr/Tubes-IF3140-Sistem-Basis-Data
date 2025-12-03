#include "storage_manager.h"

#include <algorithm>
#include <cstdio>
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
#include <unordered_set>
#include <filesystem>

namespace mdbms::sm {

StorageEngine::StorageEngine() : data_dir_("data") {
    hash_index_engine = HashIndexEngine();
}

StorageEngine::StorageEngine(const std::string& data_dir) : data_dir_(data_dir) {
    hash_index_engine = HashIndexEngine(data_dir);
}

Rows<Row> StorageEngine::read_block(const DataRetrieval& retrieval) {
    Rows<Row> result;
    TableSchema schema;
    try {
        schema = get_table_schema(retrieval.table);
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
            if (hash_index_engine.lookup_index(retrieval.table, retrieval.index_column, cond_for_index->operand, offsets)) {
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
            if (hash_index_engine.lookup_index(retrieval.table, eq_cond->column, eq_cond->operand, offsets)) {
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
        schema = get_table_schema(retrieval.table);
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

    if (select_all) {
        result.column_names = schema.column_names;
    } else {
        result.column_names = retrieval.columns;
    }

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
        schema = get_table_schema(retrieval.table);
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

    if (select_all) {
        result.column_names = schema.column_names;
    } else {
        result.column_names = retrieval.columns;
    }

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
        schema = get_table_schema(write.table);
    } catch (const std::runtime_error& e) {
        std::cerr << "SM: Error - " << e.what() << std::endl;
        return 0;
    }

    std::string filename = data_dir_ + "/" + write.table + ".dat";
    std::cout << "[DEBUG] SM write_block: filename = " << filename << std::endl;
    std::cout << "[DEBUG] SM write_block: schema.column_names.size() = " << schema.column_names.size() << std::endl;
    std::cout << "[DEBUG] SM write_block: write.new_value.columns.size() = " << write.new_value.columns.size() << std::endl;
    std::cout << "[DEBUG] SM write_block: write.new_value.columns: ";
    
    for (const auto& col_name : schema.column_names) {
        if (write.new_value.columns.count(col_name)) {
            std::cout << col_name << " ";
        }
    }
    std::cout << std::endl;
    std::cout << "[DEBUG] SM write_block: schema.column_names order: ";
    for (const auto& col : schema.column_names) {
        std::cout << col << " ";
    }
    std::cout << std::endl;

    // INSERT no condition
    if (write.conditions.empty()) {
        std::cout << "[DEBUG] SM write_block: Opening file for append..." << std::endl;
        std::ofstream file(filename, std::ios::binary | std::ios::app);

        if (!file.is_open()) {
            std::cerr << "[DEBUG] SM: Gagal membuka file: " << filename << std::endl;
            std::cerr << "[DEBUG] SM: Error details - file may not exist or directory may not exist" << std::endl;
            return 0;
        }
        std::cout << "[DEBUG] SM write_block: File opened successfully" << std::endl;

        file.seekp(0, std::ios::end);
        int64_t row_offset = file.tellp();
        serialize_row(file, write.new_value, schema);
        file.close();

        if (hash_index_engine.table_index.count(write.table)) {
            std::string col = hash_index_engine.table_index[write.table];
            hash_index_engine.update_index_after_insert(
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

            if (this->hash_index_engine.table_index.count(write.table)) {
                std::string col = hash_index_engine.table_index[write.table];
                hash_index_engine.update_index_after_update(
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
        schema = get_table_schema(deletion.table);
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
            if (hash_index_engine.table_index.count(table)) {
                std::string col = hash_index_engine.table_index[table];
                hash_index_engine.update_index_after_delete(table, col, row_offset, row);
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

// belom ada pengecekan buat apakah size valid, primary key valid, etc, blom ada pengecekan pokoknya, 
// not sure we need one but just keep that in mind
TableSchema StorageEngine::get_table_schema(const std::string& table) {
    std::string schema_filename = data_dir_ + "/" + table + ".schema";
    std::ifstream infile(schema_filename, std::ios::binary);

    TableSchema result;
    result.table_name = table;

    if (!infile.is_open()) {
        std::cerr << "SM: Gagal membuka file schema" << std::endl;
        throw std::runtime_error("File skema tidak ditemukan untuk tabel: " + table);
    }

    // read number of columns
    uint16_t columns_len;
    infile.read(reinterpret_cast<char*>(&columns_len), sizeof(columns_len));
    if (infile.gcount() != sizeof(columns_len) || !infile) {
        std::cerr << "SM: Error membaca banyak kolom pada file schema " << table << std::endl;
        throw std::runtime_error("File skema korup untuk tabel: " + table);
    }
    
    for (uint16_t i = 0; i < columns_len; i++) {
        
        uint32_t col_name_len;
        infile.read(reinterpret_cast<char*>(&col_name_len), sizeof(col_name_len));
        if (infile.gcount() != sizeof(col_name_len) || !infile) {
            std::cerr << "SM: Error membaca panjang nama kolom pada file schema " << table << std::endl;
            throw std::runtime_error("File skema korup untuk tabel: " + table);
        }
        
        std::string column_name(col_name_len, '\0');
        infile.read(&column_name[0], col_name_len);
        if (infile.gcount() != static_cast<std::streamsize>(col_name_len) || !infile) {
            std::cerr << "SM: Error membaca nama kolom pada file schema " << table << std::endl;
            throw std::runtime_error("File skema korup untuk tabel: " + table);
        }
        result.column_names.push_back(column_name);
        
        uint8_t column_type;
        infile.read(reinterpret_cast<char*>(&column_type), sizeof(column_type));
        if (infile.gcount() != sizeof(column_type) || !infile) {
            std::cerr << "SM: Error membaca tipe data kolom pada file schema " << table << std::endl;
            throw std::runtime_error("File skema korup untuk tabel: " + table);
        }
        result.column_types.push_back(static_cast<DataType>(column_type));
        
        uint32_t column_size;
        infile.read(reinterpret_cast<char*>(&column_size), sizeof(column_size));
        if (infile.gcount() != sizeof(column_size) || !infile) {
            std::cerr << "SM: Error membaca besar kolom pada file schema " << table << std::endl;
            throw std::runtime_error("File skema korup untuk tabel: " + table);
        }
        result.column_sizes.push_back(column_size);
    }
    uint32_t primary_key_len;
    infile.read(reinterpret_cast<char*>(&primary_key_len), sizeof(primary_key_len));
    if (infile.gcount() != sizeof(primary_key_len) || !infile) {
        std::cerr << "SM: Error membaca panjang nama primary key pada file schema " << table << std::endl;
        throw std::runtime_error("File skema korup untuk tabel: " + table);
    }
    std::string primary_key(primary_key_len, '\0');
    infile.read(&primary_key[0], primary_key_len);
    if (infile.gcount() != static_cast<std::streamsize>(primary_key_len) || !infile) {
        std::cerr << "SM: Error membaca nama primary key pada file schema " << table << std::endl;
        throw std::runtime_error("File skema korup untuk tabel: " + table);
    }
    result.primary_key = primary_key;

    std::map<std::string, std::string> foreign_keys = {};
    uint32_t foreign_keys_len;
    infile.read(reinterpret_cast<char*>(&foreign_keys_len), sizeof(foreign_keys_len));
    if (infile.gcount() != sizeof(foreign_keys_len) || !infile) {
        std::cerr << "SM: Error membaca banyak foreign key pada file schema " << table << std::endl;
        throw std::runtime_error("File skema korup untuk tabel: " + table);
    }
    for (uint32_t i = 0; i < foreign_keys_len; i++) {
        uint32_t foreign_keys_name_len;
        infile.read(reinterpret_cast<char*>(&foreign_keys_name_len), sizeof(foreign_keys_name_len));
        if (infile.gcount() != sizeof(foreign_keys_name_len) || !infile) {
            std::cerr << "SM: Error membaca panjang nama foreign key pada file schema " << table << std::endl;
            throw std::runtime_error("File skema korup untuk tabel: " + table);
        }

        std::string foreign_keys_name(foreign_keys_name_len, '\0');
        infile.read(&foreign_keys_name[0], foreign_keys_name_len);
        if (infile.gcount() != static_cast<std::streamsize>(foreign_keys_name_len) || !infile) {
            std::cerr << "SM: Error membaca nama foreign key pada file schema " << table << std::endl;
            throw std::runtime_error("File skema korup untuk tabel: " + table);
        }

        uint32_t references_name_len;
        infile.read(reinterpret_cast<char*>(&references_name_len), sizeof(references_name_len));
        if (infile.gcount() != sizeof(references_name_len) || !infile) {
            std::cerr << "SM: Error membaca panjang nama kolom reference dari foreign key pada file schema " << table << std::endl;
            throw std::runtime_error("File skema korup untuk tabel: " + table);
        }
        
        std::string references_name(references_name_len, '\0');
        infile.read(&references_name[0], references_name_len);
        if (infile.gcount() != static_cast<std::streamsize>(references_name_len) || !infile) {
            std::cerr << "SM: Error membaca nama kolom reference dari foreign key pada file schema " << table << std::endl;
            throw std::runtime_error("File skema korup untuk tabel: " + table);
        }
        foreign_keys.insert({foreign_keys_name, references_name});
    }
    result.foreign_keys = foreign_keys;

    infile.close();
    return result;
}

bool StorageEngine::create_table(const TableSchema& schema) {
    if (!check_and_update_tables_metadata(schema)) {
        return false;
    }

    std::string schema_filename = data_dir_ + "/" + schema.table_name + ".schema";
    std::ofstream outfile(schema_filename, std::ios::binary | std::ios::trunc);
    if (!outfile.is_open()) {
        std::cerr << "SM: Gagal membuka file schema untuk tabel: " << schema.table_name << std::endl;
        throw std::runtime_error("Gagal membuka file schema");
    }
    
    // write number of columns (aint no way number of columns exceed 2^16)
    uint16_t columns_len = schema.column_names.size();
    outfile.write(reinterpret_cast<const char*>(&columns_len), sizeof(columns_len));
    if (!outfile) {
        std::cerr << "SM: Error menulis jumlah kolom ke file." << std::endl;
        throw std::runtime_error("Gagal menulis jumlah kolom ke file");
    }

    for (int i = 0; i < columns_len; i++) {
        uint32_t col_name_len = static_cast<uint32_t>(schema.column_names[i].length());
        outfile.write(reinterpret_cast<const char*>(&col_name_len), sizeof(col_name_len));
        if (!outfile) {
            std::cerr << "SM: Error menulis panjang nama kolom ke file." << std::endl;
            throw std::runtime_error("Gagal menulis panjang nama kolom ke file");
        }
        outfile.write(schema.column_names[i].c_str(), col_name_len);
        if (!outfile) {
            std::cerr << "SM: Error menulis nama kolom ke file." << std::endl;
            throw std::runtime_error("Gagal menulis nama kolom ke file");
        }
        uint8_t column_type = static_cast<uint8_t>(schema.column_types[i]);
        outfile.write(reinterpret_cast<const char*>(&column_type), sizeof(column_type));
        if (!outfile) {
            std::cerr << "SM: Error menulis tipe data kolom ke file." << std::endl;
            throw std::runtime_error("Gagal menulis tipe data kolom ke file");
        }
        uint32_t column_size = static_cast<uint32_t>(schema.column_sizes[i]);
        outfile.write(reinterpret_cast<const char*>(&column_size), sizeof(column_size));
        if (!outfile) {
            std::cerr << "SM: Error menulis panjang nama kolom ke file." << std::endl;
            throw std::runtime_error("Gagal menulis panjang nama kolom ke file");
        }
    }
    uint32_t primary_key_len = static_cast<uint32_t>(schema.primary_key.length());
    outfile.write(reinterpret_cast<const char*>(&primary_key_len), sizeof(primary_key_len));
    if (!outfile) {
        std::cerr << "SM: Error menulis panjang nama primary key ke file." << std::endl;
        throw std::runtime_error("Gagal menulis panjang nama primary key ke file");
    }
    outfile.write(schema.primary_key.c_str(), primary_key_len);
    if (!outfile) {
        std::cerr << "SM: Error menulis nama primary key ke file." << std::endl;
        throw std::runtime_error("Gagal menulis nama primary key ke file");
    }

    uint32_t foreign_keys_len = static_cast<uint32_t>(schema.foreign_keys.size());
    outfile.write(reinterpret_cast<const char*>(&foreign_keys_len), sizeof(foreign_keys_len));
    if (!outfile) {
        std::cerr << "SM: Error menulis banyak foreign key ke file." << std::endl;
        throw std::runtime_error("Gagal menulis banyak foreign key ke file");
    }
    for (auto& foreign_key : schema.foreign_keys) {
        uint32_t foreign_key_name_len = static_cast<uint32_t>(foreign_key.first.length());
        outfile.write(reinterpret_cast<const char*>(&foreign_key_name_len), sizeof(foreign_key_name_len));
        if (!outfile) {
            std::cerr << "SM: Error menulis panjang nama foreign key ke file." << std::endl;
            throw std::runtime_error("Gagal menulis panjang nama foreign key ke file");
        }
        outfile.write(foreign_key.first.c_str(), foreign_key_name_len);
        if (!outfile) {
            std::cerr << "SM: Error menulis nama foreign key ke file." << std::endl;
            throw std::runtime_error("Gagal menulis nama foreign key ke file");
        }
        uint32_t reference_name_len = static_cast<uint32_t>(foreign_key.second.length());
        outfile.write(reinterpret_cast<const char*>(&reference_name_len), sizeof(reference_name_len));
        if (!outfile) {
            std::cerr << "SM: Error menulis panjang nama reference dari foreign key ke file." << std::endl;
            throw std::runtime_error("Gagal menulis panjang nama reference dari foreign key ke file");
        }
        outfile.write(foreign_key.second.c_str(), reference_name_len);
        if (!outfile) {
            std::cerr << "SM: Error menulis nama reference dari foreign key ke file." << std::endl;
            throw std::runtime_error("Gagal menulis nama reference dari foreign key ke file");
        }
    }
   
    outfile.close();
    return true;
}

// chech if table exist if so update tables.meta
bool StorageEngine::check_and_update_tables_metadata(const TableSchema& schema) {
    std::string tables_in_filename = data_dir_ + "/tables.meta";
    std::string tables_out_filename = data_dir_ + "/tables_temp.meta";

    if (!std::filesystem::exists(tables_in_filename)) { // if its the first table in a database
        std::ofstream tables_iofile(tables_in_filename, std::ios::binary);
        if (!tables_iofile.is_open()) {
            std::cerr << "SM: Gagal membuka file tables.meta" << std::endl;
            throw std::runtime_error("Gagal membuka file tables.meta");
        }

        uint16_t tables_len = 1;
        tables_iofile.write(reinterpret_cast<const char*>(&tables_len), sizeof(tables_len));
        if (!tables_iofile) {
            std::cerr << "SM: Error menulis jumlah kolom ke file." << std::endl;
            throw std::runtime_error("Gagal menulis jumlah kolom ke file");
        }
        uint32_t table_name_len = static_cast<uint32_t>(schema.table_name.length());
        tables_iofile.write(reinterpret_cast<const char*>(&table_name_len), sizeof(table_name_len));
        if (!tables_iofile) {
            std::cerr << "SM: Error menulis panjang nama kolom ke file." << std::endl;
            throw std::runtime_error("Gagal menulis panjang nama kolom ke file");
        }
        tables_iofile.write(schema.table_name.c_str(), table_name_len);
        if (!tables_iofile) {
            std::cerr << "SM: Error menulis nama kolom ke file." << std::endl;
            throw std::runtime_error("Gagal menulis nama kolom ke file");
        }

        tables_iofile.close();
        return true;
    }
    else {
        std::ifstream tables_infile(tables_in_filename, std::ios::binary);
        std::ofstream tables_outfile(tables_out_filename, std::ios::binary);
        bool result = true;


        if (!tables_infile.is_open()) {
            std::cerr << "SM: Gagal membuka file tables.meta" << std::endl;
            throw std::runtime_error("Gagal membuka file tables.meta");
        }
        if (!tables_outfile.is_open()) {
            std::cerr << "SM: Gagal membuka file tables_temp.meta" << std::endl;
            throw std::runtime_error("Gagal membuka file tables_temp.meta");
        }
        
        if (tables_infile.peek() == EOF) {
            // File exists but is empty, treat as if no tables exist
            tables_infile.close();
            tables_outfile.close();
            std::remove(tables_out_filename.c_str());
            std::ofstream new_tables_file(tables_in_filename, std::ios::binary | std::ios::trunc);
            if (!new_tables_file.is_open()) {
                throw std::runtime_error("Gagal membuka file tables.meta");
            }
            uint16_t tables_len = 1;
            new_tables_file.write(reinterpret_cast<const char*>(&tables_len), sizeof(tables_len));
            uint32_t table_name_len = static_cast<uint32_t>(schema.table_name.length());
            new_tables_file.write(reinterpret_cast<const char*>(&table_name_len), sizeof(table_name_len));
            new_tables_file.write(schema.table_name.c_str(), table_name_len);
            new_tables_file.close();
            return true;
        }

        // read existing tables
        uint16_t tables_len;
        tables_infile.read(reinterpret_cast<char*>(&tables_len), sizeof(tables_len));
        if (tables_infile.gcount() != sizeof(tables_len) || !tables_infile) {
            std::cerr << "SM: Error membaca jumlah nama table pada file tables.meta" << std::endl;
            tables_infile.close();
            tables_outfile.close();
            throw std::runtime_error("File tables.meta korup dan gagal dibaca");
        }

        std::vector<std::string> tables_name;
        for (int i = 0; i < tables_len; i++) {
            uint32_t table_name_len;
            tables_infile.read(reinterpret_cast<char*>(&table_name_len), sizeof(table_name_len));
            if (tables_infile.gcount() != sizeof(table_name_len) || !tables_infile) {
                std::cerr << "SM: Error membaca panjang nama tabel pada file tables.meta" << std::endl;
                throw std::runtime_error("File skema korup untuk tables.meta");
            }
            
            std::string column_name(table_name_len, '\0');
            tables_infile.read(&column_name[0], table_name_len);
            if (tables_infile.gcount() != static_cast<std::streamsize>(table_name_len) || !tables_infile) {
                std::cerr << "SM: Error membaca nama tabel pada file tables.meta" << std::endl;
                throw std::runtime_error("File skema korup untuk tables.meta");
            }
            tables_name.push_back(column_name);
        }

        // add table
        auto it = std::find(tables_name.begin(), tables_name.end(), schema.table_name);
        if (it != tables_name.end()) {
            tables_infile.close();
            tables_outfile.close();
            std::remove(tables_out_filename.c_str());
            return false;
        }
        else {
            tables_name.push_back(schema.table_name);
            tables_len += 1;
        }

        tables_outfile.write(reinterpret_cast<const char*>(&tables_len), sizeof(tables_len));
        if (!tables_outfile) {
            std::cerr << "SM: Error menulis nama tabel ke file tables.meta" << std::endl;
            tables_infile.close();
            tables_outfile.close();
            std::remove(tables_out_filename.c_str());
            throw std::runtime_error("Gagal menulis nama tabel ke file tables.meta");
        }

        for (auto& table_name : tables_name) {
            uint32_t table_name_len = static_cast<uint32_t>(table_name.length());
            tables_outfile.write(reinterpret_cast<const char*>(&table_name_len), sizeof(table_name_len));
            if (!tables_outfile) {
                std::cerr << "SM: Error menulis panjang nama kolom ke file." << std::endl;
                tables_infile.close();
                tables_outfile.close();
                std::remove(tables_out_filename.c_str());
                throw std::runtime_error("Gagal menulis panjang nama kolom ke file");
            }
            tables_outfile.write(table_name.c_str(), table_name_len);
            if (!tables_outfile) {
                std::cerr << "SM: Error menulis nama kolom ke file." << std::endl;
                tables_infile.close();
                tables_outfile.close();
                std::remove(tables_out_filename.c_str());
                throw std::runtime_error("Gagal menulis nama kolom ke file");
            }
        }

        tables_infile.close();
        tables_outfile.close();

        if (std::remove(tables_in_filename.c_str()) != 0) {
            std::perror("SM: Error deleting data file when deleting old tables.meta");
            std::remove(tables_out_filename.c_str());
            throw std::runtime_error("SM: Error deleting data file when deleting old tables.meta");
        }
        std::rename(tables_out_filename.c_str(), tables_in_filename.c_str());
        return true;
    }
}

bool StorageEngine::delete_table(const TableSchema& schema) {
    std::string schema_filename = data_dir_ + "/" + schema.table_name + ".schema";
    std::string data_filename = data_dir_ + "/" + schema.table_name + ".dat";
    std::string tables_in_filename = data_dir_ + "/tables.meta";
    std::string tables_out_filename = data_dir_ + "/tables_temp.meta";

    std::ifstream tables_infile(tables_in_filename, std::ios::binary);
    std::ofstream tables_outfile(tables_out_filename, std::ios::binary);

    bool result = true;

    if (!tables_infile.is_open()) {
        std::cerr << "SM: Gagal membuka file tables.meta" << std::endl;
        throw std::runtime_error("Gagal membuka file tables.meta");
    }
    if (!tables_outfile.is_open()) {
        std::cerr << "SM: Gagal membuka file tables_temp.meta" << std::endl;
        throw std::runtime_error("Gagal membuka file tables_temp.meta");
    }
    

    // read existing tables
    uint16_t tables_len;
    tables_infile.read(reinterpret_cast<char*>(&tables_len), sizeof(tables_len));
    if (tables_infile.gcount() != sizeof(tables_len) || !tables_infile) {
        std::cerr << "SM: Error membaca jumlah nama table pada file tables.meta" << std::endl;
        throw std::runtime_error("File tables.meta korup dan gagal dibaca");
    }

    std::vector<std::string> tables_name;
    for (int i = 0; i < tables_len; i++) {
        uint32_t table_name_len;
        tables_infile.read(reinterpret_cast<char*>(&table_name_len), sizeof(table_name_len));
        if (tables_infile.gcount() != sizeof(table_name_len) || !tables_infile) {
            std::cerr << "SM: Error membaca panjang nama tabel pada file tables.meta" << std::endl;
            throw std::runtime_error("File skema korup untuk tables.meta");
        }
        
        std::string column_name(table_name_len, '\0');
        tables_infile.read(&column_name[0], table_name_len);
        if (tables_infile.gcount() != static_cast<std::streamsize>(table_name_len) || !tables_infile) {
            std::cerr << "SM: Error membaca nama tabel pada file tables.meta" << std::endl;
            throw std::runtime_error("File skema korup untuk tables.meta");
        }
        tables_name.push_back(column_name);
    }

    // remove table
    auto it = std::find(tables_name.begin(), tables_name.end(), schema.table_name);
    if (it == tables_name.end()) result = false; // table doesnt exist
    else {
        tables_name.erase(it);
        tables_len -= 1;
    }

    if (tables_len != 0) {
        tables_outfile.write(reinterpret_cast<const char*>(&tables_len), sizeof(tables_len));
        if (!tables_outfile) {
            std::cerr << "SM: Error menulis nama tabel ke file tables.meta" << std::endl;
            throw std::runtime_error("Gagal menulis nama tabel ke file tables.meta");
        }

        for (auto& table_name : tables_name) {
            uint32_t table_name_len = static_cast<uint32_t>(table_name.length());
            tables_outfile.write(reinterpret_cast<const char*>(&table_name_len), sizeof(table_name_len));
            if (!tables_outfile) {
                std::cerr << "SM: Error menulis panjang nama kolom ke file." << std::endl;
                throw std::runtime_error("Gagal menulis panjang nama kolom ke file");
            }
            tables_outfile.write(table_name.c_str(), table_name_len);
            if (!tables_outfile) {
                std::cerr << "SM: Error menulis nama kolom ke file." << std::endl;
                throw std::runtime_error("Gagal menulis nama kolom ke file");
            }
        }
    }

    tables_infile.close();
    tables_outfile.close();

    // delete table data dan schema
    if (std::remove(schema_filename.c_str()) != 0) {
        std::perror("SM: Error deleting schema file when deleting table");
        throw std::runtime_error("SM: Error deleting schema file when deleting tabl");
    }
    if (std::remove(data_filename.c_str()) != 0) {
        std::perror("SM: Error deleting data file when deleting table");
        throw std::runtime_error("SM: Error deleting data file when deleting table");
    }
    if (std::remove(tables_in_filename.c_str()) != 0) {
        std::perror("SM: Error deleting data file when deleting old tables.meta");
        throw std::runtime_error("SM: Error deleting data file when deleting old tables.meta");
    }

    // delete index file
    // eh ngawur cuks masih aneh hash index
    std::rename(tables_out_filename.c_str(), tables_in_filename.c_str());
    return result;
}

std::map<std::string, Statistic> StorageEngine::get_stats() {
    int BLOCK_SIZE = 4096; 

    std::vector<TableSchema> tables = get_tables();
    std::map<std::string, Statistic> result;

    for (const TableSchema& table : tables) {
        Statistic stats;
        std::string filename = data_dir_ + "/" + table.table_name + ".dat";

        // edge case, empty 
        if (!std::filesystem::exists(filename)) {
            int tuple_size = 0;
            for (int i = 0; i < table.column_types.size(); i++) {
                if (table.column_types[i] == DataType::INTEGER) tuple_size += sizeof(int32_t);
                else if (table.column_types[i] == DataType::FLOAT) tuple_size += sizeof(float);
                else if (table.column_types[i] == DataType::VARCHAR || table.column_types[i] == DataType::CHAR) {
                    tuple_size += sizeof(uint32_t);

                    // jujur bingung emg kalo CHAR, panjangnya bisa lebih dari 1 kah? 
                    // tapi di TableSchema katanya column_sizes buat varchar DAN char, so for now gini dulu
                    tuple_size += sizeof(char) * table.column_sizes[i]; 
                }
            }
            stats.n_r = 0;
            stats.b_r = 0;
            stats.l_r = tuple_size;
            stats.f_r = BLOCK_SIZE / tuple_size;
            std::map<std::string, int> distinct_values; 
            for (int i = 0; i < table.column_names.size(); i++) {
                distinct_values.insert({table.column_names[i], 0});
            }
            stats.V_a_r = distinct_values;

            result.insert({table.table_name, stats});
            continue;
        }

        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "SM: Gagal membuka file: " << filename << std::endl;
            return result;
        }

        int file_size = std::filesystem::file_size(filename);
        stats.b_r = (file_size / BLOCK_SIZE) + 1; // should be always positive so its fine to truncate toward zero

        int tuple_size = 0;
        for (int i = 0; i < table.column_types.size(); i++) {
            if (table.column_types[i] == DataType::INTEGER) tuple_size += sizeof(int32_t);
            else if (table.column_types[i] == DataType::FLOAT) tuple_size += sizeof(float);
            else if (table.column_types[i] == DataType::VARCHAR || table.column_types[i] == DataType::CHAR) {
                tuple_size += sizeof(uint32_t);

                // jujur bingung emg kalo CHAR, panjangnya bisa lebih dari 1 kah? 
                // tapi di TableSchema katanya column_sizes buat varchar DAN char, so for now gini dulu
                tuple_size += sizeof(char) * table.column_sizes[i]; 
            }
        }
        stats.l_r = tuple_size;
        stats.f_r = BLOCK_SIZE / tuple_size;

        std::map<std::string, int> distinct_values; 
        std::unordered_map<std::string, std::unordered_set<std::string>> distinct_values_count;

        int tuple_amount = 0;
        while (file.peek() != EOF) {
            Row row = deserialize_row(file, table);
            if (row.columns.empty()) {
                if (file.eof()) break;
                std::cerr << "SM: Error membaca baris data." << std::endl;
                break;
            }

            for (const auto& col_name : table.column_names) {
                if (row.columns.count(col_name)) {
                    std::string hash_key = col_name + to_string_any(row.columns.at(col_name));
                    auto it = distinct_values_count.find(hash_key);
                    if (it != distinct_values_count.end()) {
                        it->second.insert(col_name);
                    }
                    else {
                        distinct_values_count[hash_key].insert(col_name);
                    }
                }
            }
            tuple_amount += 1;
        }
        
        for (auto& distinct_val : distinct_values_count) {
            for (auto& distinct_val_col : distinct_val.second) {
                distinct_values[distinct_val_col]++; 
            }
        }
        stats.V_a_r = distinct_values;
        stats.n_r = tuple_amount;
        result.insert({table.table_name, stats});
        file.close();
    }

    

    return result;
}

std::vector<TableSchema> StorageEngine::get_tables() {
    std::string tables_filename = data_dir_ + "/tables.meta";
    std::vector<TableSchema> result;
    std::vector<std::string> tables;

    if (!std::filesystem::exists(tables_filename)) {
        return result;
    }

    std::ifstream tables_infile(tables_filename, std::ios::binary);
    if (!tables_infile.is_open()) {
        std::cerr << "SM: Gagal membuka file tables.meta" << std::endl;
        return result;
    }

    if (tables_infile.peek() == EOF) {
        tables_infile.close();
        return result;
    }

    uint16_t tables_len;
    tables_infile.read(reinterpret_cast<char*>(&tables_len), sizeof(tables_len));
    if (tables_infile.gcount() != sizeof(tables_len) || !tables_infile) {
        std::cerr << "SM: Error membaca panjang nama table pada file tables.meta" << std::endl;
        tables_infile.close();
        throw std::runtime_error("File tables.meta korup dan gagal dibaca");
    }

    // read tables' name
    for (uint16_t i = 0; i < tables_len; i++) {
        uint32_t table_name_len;
        tables_infile.read(reinterpret_cast<char*>(&table_name_len), sizeof(table_name_len));
        if (tables_infile.gcount() != sizeof(table_name_len) || !tables_infile) {
            std::cerr << "SM: Error membaca panjang nama table pada file tables.meta" << std::endl;
            throw std::runtime_error("File tables.meta korup");
        }
        std::string table_name(table_name_len, '\0');
        tables_infile.read(&table_name[0], table_name_len);
        if (tables_infile.gcount() != static_cast<std::streamsize>(table_name_len) || !tables_infile) {
            std::cerr << "SM: Error membaca nama kolom pada file tables.meta " << std::endl;
            throw std::runtime_error("File tables.meta korup");
        }
        tables.push_back(table_name);
    }

    // read table schema
    for (const std::string& table : tables) {
        try {
            // Check if schema file exists before trying to load it
            std::string schema_filename = data_dir_ + "/" + table + ".schema";
            if (!std::filesystem::exists(schema_filename)) {
                std::cerr << "SM: Warning - Table '" << table << "' listed in tables.meta but schema file not found. Skipping." << std::endl;
                continue;
            }
            result.push_back(get_table_schema(table));
        } catch (const std::runtime_error& e) {
            // If schema file exists but is corrupted, log error and skip
            std::cerr << "SM: Warning - Failed to load schema for table '" << table << "': " << e.what() << ". Skipping." << std::endl;
            continue;
        }
    }

    tables_infile.close();
    return result;
}

void StorageEngine::serialize_row(std::ostream& out, const Row& row, const TableSchema& schema) {
    std::cout << "[DEBUG] SM serialize_row: Starting serialization for " << schema.column_names.size() << " columns" << std::endl;
    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        const std::string& col_name = schema.column_names[i];
        DataType col_type = schema.column_types[i];

        std::cout << "[DEBUG] SM serialize_row: Processing column[" << i << "] = " << col_name 
                  << " (expected type: " << (col_type == DataType::INTEGER ? "INTEGER" : 
                                              col_type == DataType::FLOAT ? "FLOAT" : "VARCHAR") << ")" << std::endl;

        if (!row.columns.count(col_name)) {
            std::cerr << "[DEBUG] SM: Kolom hilang saat serialisasi: " << col_name << std::endl;
            throw std::runtime_error("Kolom hilang saat serialisasi: " + col_name);
        }

        const std::any& value = row.columns.at(col_name);
        
        // Debug: Print actual type of value
        if (value.type() == typeid(int)) {
            std::cout << "[DEBUG] SM serialize_row: Value type is int, value = " << std::any_cast<int>(value) << std::endl;
        } else if (value.type() == typeid(float)) {
            std::cout << "[DEBUG] SM serialize_row: Value type is float, value = " << std::any_cast<float>(value) << std::endl;
        } else if (value.type() == typeid(double)) {
            std::cout << "[DEBUG] SM serialize_row: Value type is double, value = " << std::any_cast<double>(value) << std::endl;
        } else if (value.type() == typeid(std::string)) {
            std::cout << "[DEBUG] SM serialize_row: Value type is string, value = " << std::any_cast<std::string>(value) << std::endl;
        } else {
            std::cout << "[DEBUG] SM serialize_row: Value type is unknown: " << value.type().name() << std::endl;
        }

        try {
            if (col_type == DataType::INTEGER) {
                int32_t val = std::any_cast<int>(value);
                std::cout << "[DEBUG] SM serialize_row: Successfully cast to int: " << val << std::endl;
                out.write(reinterpret_cast<const char*>(&val), sizeof(val));
                if (!out) {
                    std::cerr << "[DEBUG] SM: Error menulis INTEGER ke file." << std::endl;
                    throw std::runtime_error("Gagal menulis INTEGER ke file");
                }
            }
            else if (col_type == DataType::FLOAT) {
                float val;
                // Try to cast as float first, then double (common parser issue)
                try {
                    val = std::any_cast<float>(value);
                } catch (const std::bad_any_cast&) {
                    try {
                        val = static_cast<float>(std::any_cast<double>(value));
                        std::cout << "[DEBUG] SM serialize_row: Cast double to float: " << val << std::endl;
                    } catch (const std::bad_any_cast&) {
                        throw; // Re-throw if neither works
                    }
                }
                std::cout << "[DEBUG] SM serialize_row: Successfully cast to float: " << val << std::endl;
                out.write(reinterpret_cast<const char*>(&val), sizeof(val));
                if (!out) {
                    std::cerr << "[DEBUG] SM: Error menulis FLOAT ke file." << std::endl;
                    throw std::runtime_error("Gagal menulis FLOAT ke file");
                }
            }
            else if (col_type == DataType::VARCHAR || col_type == DataType::CHAR) {
                std::string str = std::any_cast<std::string>(value);
                std::cout << "[DEBUG] SM serialize_row: Successfully cast to string: " << str << std::endl;
                uint32_t len = static_cast<uint32_t>(str.length());
                out.write(reinterpret_cast<const char*>(&len), sizeof(len));
                if (!out) {
                    std::cerr << "[DEBUG] SM: Error menulis panjang string ke file." << std::endl;
                    throw std::runtime_error("Gagal menulis panjang string ke file");
                }
                out.write(str.c_str(), len);
                if (!out) {
                    std::cerr << "[DEBUG] SM: Error menulis data string ke file." << std::endl;
                    throw std::runtime_error("Gagal menulis data string ke file");
                }
            }
        } catch (const std::bad_any_cast& e) {
            std::cerr << "[DEBUG] SM: Tipe data tidak cocok untuk kolom " << col_name << ": " << e.what() << std::endl;
            std::cerr << "[DEBUG] SM: Expected type: " << (col_type == DataType::INTEGER ? "INTEGER" : 
                                                           col_type == DataType::FLOAT ? "FLOAT" : "VARCHAR") << std::endl;
            std::cerr << "[DEBUG] SM: Actual value type: " << value.type().name() << std::endl;
            throw std::runtime_error("Tipe data tidak cocok untuk kolom " + col_name);
        }
    }
    std::cout << "[DEBUG] SM serialize_row: Serialization completed successfully" << std::endl;
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

void StorageEngine::set_index(const std::string& table, const std::string& column, const IndexType index_type) {
    hash_index_engine.table_index[table] = column;
    hash_index_engine.table_index_type[table] = index_type;

    TableSchema schema;
    try {
        schema = get_table_schema(table);
    } catch (const std::exception &e) {
        std::cerr << "SM: Schema error.\n";
        return;
    }

    if (index_type == IndexType::HASH) {
        hash_index_engine.build_hash_index(schema, table, column);
    } else {
        // TODO: call b+tree here
    };
}

std::string StorageEngine::to_string_any(const std::any& a) {
    if (a.type() == typeid(std::string)) return std::any_cast<std::string>(a);
    if (a.type() == typeid(const char*)) return std::string(std::any_cast<const char*>(a));
    if (a.type() == typeid(int)) return std::to_string(std::any_cast<int>(a));
    if (a.type() == typeid(double)) return std::to_string(std::any_cast<double>(a));
    if (a.type() == typeid(int32_t))  return std::to_string(std::any_cast<int32_t>(a));
    if (a.type() == typeid(float))  return std::to_string(std::any_cast<float>(a));
    if (a.type() == typeid(int64_t))  return std::to_string(std::any_cast<int64_t>(a));
    if (a.type() == typeid(uint32_t))  return std::to_string(std::any_cast<uint32_t>(a));

    throw std::runtime_error("SM: unsupported type in helper function to_string_any");
}

std::vector<std::string> StorageEngine::get_column_names(const std::string& table) {
    try {
        TableSchema schema = get_table_schema(table);
        return schema.column_names;
    } catch (const std::exception& e) {
        std::cerr << "SM: Error getting column names for table " << table << ": " << e.what() << std::endl;
        return {};
    }
}

bool StorageEngine::drop_table(const std::string& table) {
    try {
        TableSchema schema = get_table_schema(table);
        return delete_table(schema);
    } catch (const std::exception& e) {
        std::cerr << "SM: Error dropping table " << table << ": " << e.what() << std::endl;
        return false;
    }
}

StorageEngine& StorageEngine::get_instance() {
    static StorageEngine instance;
    return instance;
}

} // namespace mdbms::sm

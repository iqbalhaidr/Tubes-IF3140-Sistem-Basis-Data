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

    std::string filename = data_dir_ + "/" + retrieval.table + ".dat";
    // Buka file dalam mode binary
    std::ifstream file(filename, std::ios::binary); 

    if (!file.is_open()) {
        std::cerr << "SM: Gagal membuka file: " << filename << std::endl;
        return result;
    }

    // Terus baca selama belum End-of-File
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
            bool select_all = std::find(retrieval.columns.begin(), retrieval.columns.end(), "*") != retrieval.columns.end();

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

    if (write.conditions.empty()) {
        // INSERT operation
        std::ofstream file(filename, std::ios::binary | std::ios::app); 

        if (!file.is_open()) {
            std::cerr << "SM: Gagal membuka file: " << filename << std::endl;
            return 0;
        }

        serialize_row(file, write.new_value, schema);
        file.close();
        return 1; // 1 baris terpengaruh

    } else {
        // UPDATE operation
        std::string temp_filename = data_dir_ + "/" + write.table + ".tmp";

        // Buka file dalam mode binary
        std::ifstream infile(filename, std::ios::binary);
        std::ofstream outfile(temp_filename, std::ios::binary);

        if (!infile.is_open() || !outfile.is_open()) {
            std::cerr << "SM: Gagal membuka file (temp) untuk UPDATE" << std::endl;
            return 0;
        }

        int affected_rows = 0;
        while (infile.peek() != EOF) {
            Row row = deserialize_row(infile, schema);
            if (row.columns.empty()) {
                 if (infile.eof()) break;
                 continue;
            }

            if (check_conditions(row, write.conditions)) {
                // Update kolom yang ada di write.columns dengan nilai dari write.new_value
                for (const auto& col_name : write.columns) {
                    if (write.new_value.columns.count(col_name)) {
                        row.columns[col_name] = write.new_value.columns.at(col_name);
                    }
                }
                affected_rows++;
            }
            serialize_row(outfile, row, schema);
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
}

int StorageEngine::delete_block(const DataDeletion& deletion) {
    TableSchema schema;
    try {
        schema = getSchema(deletion.table);
    } catch (const std::runtime_error& e) {
        std::cerr << "SM: Error - " << e.what() << std::endl;
        return 0;
    }

    std::string filename = data_dir_ + "/" + deletion.table + ".dat";
    std::string temp_filename = data_dir_ + "/" + deletion.table + ".tmp";

    // Buka file dalam mode binary
    std::ifstream infile(filename, std::ios::binary);
    std::ofstream outfile(temp_filename, std::ios::binary);

    if (!infile.is_open() || !outfile.is_open()) {
        std::cerr << "SM: Gagal membuka file (temp) untuk DELETE" << std::endl;
        return 0;
    }

    int affected_rows = 0;
    while (infile.peek() != EOF) {
        Row row = deserialize_row(infile, schema);
        if (row.columns.empty()) {
            if (infile.eof()) break;
            continue;
        }

        if (check_conditions(row, deletion.conditions)) {
            affected_rows++;
        } else {
            serialize_row(outfile, row, schema);
        }
    }

    infile.close();
    outfile.close();

    // Remove dan rename serta cek error pada operasi file system
    if (std::remove(filename.c_str()) != 0) {
        std::cerr << "SM: Gagal menghapus file asli: " << filename << std::endl;
        // Jika gagal, hapus file temp agar tidak tertinggal
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

        if (in.eof() || in.peek() == EOF) {
            std::cerr << "SM: EOF atau file korup saat deserialisasi kolom " << col_name << std::endl;
            return Row();
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

// Implementasi Indexing
void StorageEngine::set_index(const std::string& table, const std::string& column, const IndexType index_type) {
    // load schema
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
        // klo default b+tree
        // TODO: call b+tree di sini
    };
}

void StorageEngine::build_hash_index(const TableSchema& schema, const std::string& table, const std::string& column) {
    std::string datafile  = data_dir_ + "/" + table + ".dat";
    std::string idxfile = data_dir_ + "/" + table + "." + column + ".hashidx";

    std::ifstream file(datafile, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "SM: Tidak dapat membuka file untuk indexing: " << datafile << "\n";
        return;
    }

    // Find kolom yang akan diindex
    int col_idx = -1;
    DataType type = DataType::VARCHAR; // default
    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        if (schema.column_names[i] == column) {
            col_idx = i;
            type = schema.column_types[i];
            break;
        }
    }
    if (col_idx == -1) {
        std::cerr << "SM: Kolom untuk indexing tidak ditemukan.";
        return;
    }

    // Build map
    std::unordered_map<int32_t, std::vector<int64_t>> int_map;
    std::unordered_map<uint32_t, std::vector<int64_t>> float_map_bits;
    std::unordered_map<std::string, std::vector<int64_t>> str_map;

    int32_t rows_scanned = 0;

    while (file.peek() != EOF) {
        std::streampos pos = file.tellg(); // posisi byte sblm membaca row
        if (pos < 0) break; // safety

        Row row = deserialize_row(file, schema);

        const std::string &col_name = schema.column_names[col_idx];
        auto it = row.columns.find(col_name);
        if (it == row.columns.end()) {
            // null values tidak di-index
            ++rows_scanned;
            continue;
        }

        try {
            if (type == DataType::INTEGER) {
                int32_t key = 0;
                if (!any_to_int32(it->second, key)) {
                    std::cerr << "SM: Warning - gagal convert kolom '" << col_name << "' ke integer pada offset baris " << pos << "\n";
                } else {
                    int_map[key].push_back(static_cast<int64_t>(pos));
                }
            } 
            else if (type == DataType::FLOAT) {
                float fv = 0.0f;
                if (!any_to_float(it->second, fv)) {
                    std::cerr << "SM: Warning - gagal convert kolom '" << col_name << "' ke float pada offset baris " << pos << "\n";
                } else {
                    // convert float to raw bits to use as key
                    uint32_t bits = 0;
                    static_assert(sizeof(bits) == sizeof(fv), "ukuran float tidak cocok");
                    std::memcpy(&bits, &fv, sizeof(bits));
                    float_map_bits[bits].push_back(static_cast<int64_t>(pos));
                }
            } else {
                std::string sv;
                if (!any_to_string(it->second, sv)) {
                    std::cerr << "SM: Warning - failed to convert column '" << col_name << "' to string at row offset " << pos << "\n";
                } else {
                    str_map[sv].push_back(static_cast<int64_t>(pos));
                }
            }
        } catch (...) {}

        ++rows_scanned;
    };

    file.close();

    // Buat index file
    std::ofstream out(idxfile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "SM: Gagal membuat file index.\n";
        return;
    }

    out.write("HIDX", 4);
    // TULIS Index Type, 0 = HASH
    uint8_t index_type_id = 0;
    out.write(reinterpret_cast<char*>(&index_type_id), 1);
    // Tulis Data Type
    uint8_t dtype = (type == DataType::INTEGER) ? 0 : (type == DataType::FLOAT) ? 1 : 2;
    out.write(reinterpret_cast<char*>(&dtype), 1);

    // nkeys = banyaknya key
    if (dtype == 0) {
        uint32_t nkeys = static_cast<uint32_t>(int_map.size());
        out.write(reinterpret_cast<char*>(&nkeys), sizeof(nkeys));
        for (auto &kv : int_map) {
            int32_t key = kv.first;
            out.write(reinterpret_cast<char*>(&key), sizeof(key)); // key
            uint32_t cnt = static_cast<uint32_t>(kv.second.size());
            out.write(reinterpret_cast<char*>(&cnt), sizeof(cnt)); // banyaknya value
            for (int64_t offset : kv.second) {
                out.write(reinterpret_cast<char*>(&offset), sizeof(offset)); // offset
            }
        }
    } else if (dtype == 1) {
        uint32_t nkeys = static_cast<uint32_t>(float_map_bits.size());
        out.write(reinterpret_cast<char*>(&nkeys), sizeof(nkeys));
        for (auto &kv : float_map_bits) {
            uint32_t keybits = kv.first;
            out.write(reinterpret_cast<char*>(&keybits), sizeof(keybits)); // key in bits
            uint32_t cnt = static_cast<uint32_t>(kv.second.size());
            out.write(reinterpret_cast<char*>(&cnt), sizeof(cnt)); // banyaknya value
            for (int64_t offset : kv.second) {
                out.write(reinterpret_cast<char*>(&offset), sizeof(offset)); // ofset
            };
        }
    } else {
        uint32_t nkeys = static_cast<uint32_t>(str_map.size());
        out.write(reinterpret_cast<char*>(&nkeys), sizeof(nkeys));
        for (auto &kv : str_map) {
            uint32_t len = static_cast<uint32_t>(kv.first.size());
            out.write(reinterpret_cast<char*>(&len), sizeof(len)); // panjang string
            out.write(kv.first.data(), len); // key in string
            uint32_t cnt = static_cast<uint32_t>(kv.second.size());
            out.write(reinterpret_cast<char*>(&cnt), sizeof(cnt)); // banyaknya value
            for (int64_t offset : kv.second) {
                out.write(reinterpret_cast<char*>(&offset), sizeof(offset)); // offset
            };
        }
    }
    out.close();
    std::cerr << "SM: HASH index telah dibuat: " << idxfile  << " (" << rows_scanned << " baris dipindai)\n";
}

// Helper untuk build index
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
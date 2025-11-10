#include "storage_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <string>

namespace mdbms::sm {

StorageEngine::StorageEngine(const std::string& data_dir) : data_dir_(data_dir) {
    
}

Rows StorageEngine::read_block(const DataRetrieval& retrieval) {
    Rows result;
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
        RowData row = deserialize_row(file, schema);
        if (row.empty()) {
            if (file.eof()) break;
            std::cerr << "SM: Error membaca baris data." << std::endl;
            break;
        }

        if (check_conditions(row, retrieval.conditions)) {
            RowData projected_row;
            bool select_all = std::find(retrieval.columns.begin(), retrieval.columns.end(), "*") != retrieval.columns.end();

            if (select_all) {
                projected_row = row;
            } else {
                for (const auto& col_name : retrieval.columns) {
                    if (row.count(col_name)) {
                        projected_row[col_name] = row[col_name];
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

int StorageEngine::write_block(const DataWrite& write) {
    TableSchema schema;
    try {
        schema = getSchema(write.table);
    } catch (const std::runtime_error& e) {
        std::cerr << "SM: Error - " << e.what() << std::endl;
        return 0;
    }
    
    std::string filename = data_dir_ + "/" + write.table + ".dat";

    if (write.conditions.empty()) {
        std::ofstream file(filename, std::ios::binary | std::ios::app); 

        if (!file.is_open()) {
            std::cerr << "SM: Gagal membuka file: " << filename << std::endl;
            return 0;
        }

        serialize_row(file, write.new_values, schema);
        file.close();
        return 1; // 1 baris terpengaruh

    } else {
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
            RowData row = deserialize_row(infile, schema);
            if (row.empty()) {
                 if (infile.eof()) break;
                 continue;
            }

            if (check_conditions(row, write.conditions)) {
                for (const auto& pair : write.new_values) {
                    if (row.count(pair.first)) {
                        row[pair.first] = pair.second;
                    }
                }
                affected_rows++;
            }
            serialize_row(outfile, row, schema);
        }

        infile.close();
        outfile.close();

        std::remove(filename.c_str());
        std::rename(temp_filename.c_str(), filename.c_str());

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
        RowData row = deserialize_row(infile, schema);
        if (row.empty()) {
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

    std::remove(filename.c_str());
    std::rename(temp_filename.c_str(), filename.c_str());

    return affected_rows;
}

TableSchema StorageEngine::getSchema(const std::string& table) {
    // hardcoded utk testing saja
    if (table == "Student") {
        return {
            {"StudentID", DataType::INT, 0},
            {"FullName", DataType::STRING, 50},
            {"GPA", DataType::FLOAT, 0}
        };
    }
    if (table == "Course") {
        return {
            {"CourseID", DataType::INT, 0},
            {"Year", DataType::INT, 0},
            {"CourseName", DataType::STRING, 50}
        };
    }
    throw std::runtime_error("Skema tidak ditemukan untuk tabel: " + table);
}

void StorageEngine::serialize_row(std::ostream& out, const RowData& row, const TableSchema& schema) {
    // Implementasi serializer biner
    for (const auto& col : schema) {
        if (!row.count(col.name)) {
            throw std::runtime_error("Kolom hilang saat serialisasi: " + col.name);
        }
        
        const auto& value = row.at(col.name);

        try {
            if (col.type == DataType::INT) {
                int32_t val = std::get<int>(value);
                out.write(reinterpret_cast<const char*>(&val), sizeof(val));
            } 
            else if (col.type == DataType::FLOAT) {
                float val = std::get<float>(value);
                out.write(reinterpret_cast<const char*>(&val), sizeof(val));
            } 
            else if (col.type == DataType::STRING) {
                const std::string& str = std::get<std::string>(value);
                uint32_t len = static_cast<uint32_t>(str.length());
                out.write(reinterpret_cast<const char*>(&len), sizeof(len)); // Tulis panjang string
                out.write(str.c_str(), len); // Tulis data string
            }
        } catch (const std::bad_variant_access& e) {
            throw std::runtime_error("Tipe data tidak cocok untuk kolom " + col.name);
        }
    }
}

RowData StorageEngine::deserialize_row(std::istream& in, const TableSchema& schema) {
    // Implementasi deserializer biner
    RowData row;
    for (const auto& col : schema) {
        if (in.eof() || in.peek() == EOF) return {};

        try {
            if (col.type == DataType::INT) {
                int32_t val;
                in.read(reinterpret_cast<char*>(&val), sizeof(val));
                if (in.gcount() != sizeof(val)) return {}; // file korup
                row[col.name] = static_cast<int>(val);
            } 
            else if (col.type == DataType::FLOAT) {
                float val;
                in.read(reinterpret_cast<char*>(&val), sizeof(val));
                if (in.gcount() != sizeof(val)) return {}; // file korup
                row[col.name] = val;
            } 
            else if (col.type == DataType::STRING) {
                uint32_t len;
                in.read(reinterpret_cast<char*>(&len), sizeof(len));
                if (in.gcount() != sizeof(len)) return {}; // file korup

                std::string str(len, '\0');
                in.read(&str[0], len);
                if (in.gcount() != len) return {}; // file korup
                row[col.name] = str;
            }
        } catch (...) {
             return {};
        }
    }
    return row;
}

bool StorageEngine::check_conditions(const RowData& row, const std::vector<Condition>& conditions) {
    if (conditions.empty()) {
        return true; 
    }
    
    for (const auto& cond : conditions) {
        if (!row.count(cond.column)) return false; 

        bool match = std::visit([&](auto&& left_arg) -> bool {
            try {
                return std::visit([&](auto&& right_arg) -> bool {
                    using R = std::decay_t<decltype(right_arg)>;
                    using L = std::decay_t<decltype(left_arg)>;

                    if constexpr (std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) {
                        double left;
                        if constexpr (std::is_same_v<std::decay_t<decltype(left_arg)>, std::string>) {
                            // Jika tipenya string
                            left = std::stod(left_arg);
                        } else {
                            // Jika tipenya lain (int, float)
                            left = static_cast<double>(left_arg);
                        }
                        double right = static_cast<double>(right_arg);

                        switch (cond.operation) {
                            case OpType::EQ: return left == right;
                            case OpType::NEQ: return left != right;
                            case OpType::GT: return left > right;
                            case OpType::GTE: return left >= right;
                            case OpType::LT: return left < right;
                            case OpType::LTE: return left <= right;
                        }
                    } 
                    else if constexpr (std::is_same_v<L, std::string> && std::is_same_v<R, std::string>) {
                        switch (cond.operation) {
                            case OpType::EQ: return left_arg == right_arg;
                            case OpType::NEQ: return left_arg != right_arg;
                            default: return false;
                        }
                    }
                    return false;
                }, cond.operand);
            } catch (const std::bad_variant_access& e) {
                return false;
            }
        }, row.at(cond.column));

        if (!match) return false; 
    }
    return true; 
}

} // namespace mdbms::sm
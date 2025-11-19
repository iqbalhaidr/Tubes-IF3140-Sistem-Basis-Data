#include "failure_recovery.h"
#include <iostream>

namespace mdbms::fr {

void FailureRecoveryManager::write_log(const ExecutionResult& info) {
    std::cout << "FRM: Menulis log untuk query: " << info.query << " (stub)..." << std::endl;
}

void FailureRecoveryManager::save_checkpoint() {
    std::cout << "FRM: Menyimpan checkpoint (stub)..." << std::endl;
}

void FailureRecoveryManager::recover(const RecoverCriteria&) {
    std::cout << "FRM: Melakukan recovery (stub)..." << std::endl;
}

std::string FailureRecoveryManager::any_to_string(const std::any& value) {
    if (!value.has_value()) return "NULL";

    try {
        if (value.type() == typeid(int)) {
            return std::to_string(std::any_cast<int>(value));
        } else if (value.type() == typeid(float)) {
            return std::to_string(std::any_cast<float>(value));
        } else if (value.type() == typeid(std::string)) {
            return std::any_cast<std::string>(value);
        }
    } catch (...) {
        return "ERROR";
    }
    return "UNKNOWN_TYPE";
}

std::any FailureRecoveryManager::string_to_any(const std::string& str, DataType type) {
    if (type == DataType::INTEGER) return std::stoi(str);
    if (type == DataType::FLOAT) return std::stof(str);
    return str;
}

std::string FailureRecoveryManager::row_to_string(const Row& row) {
    if (row.row_id == -1 && row.columns.empty()) return "EMPTY"; 
    
    std::stringstream ss;

    ss << row.row_id << "#";

    ss << "{";
    bool first = true;
    for (const auto& [atr, val] : row.columns) {
        if (!first) ss << ",";
        ss << atr << ":" << any_to_string(val);
        first = false;
    }
    ss << "}";

    return ss.str();
}

Row FailureRecoveryManager::string_to_row(const std::string& row_string, const std::string& table_name) {
    Row row;
    row.table_name = table_name;
    std::cout << "table_name: " << row.table_name << std::endl;

    if (row_string == "EMPTY") {
        row.row_id = -1;
        return row;
    }

    std::stringstream ss(row_string);
    std::string segment;
    std::vector<std::string> parts;

    size_t delimiter_pos = row_string.find('#'); // Cari pemisah ID dan Data
    if (delimiter_pos == std::string::npos) {
        row.row_id = -1; 
        return row;
    }

    // Parse Row ID
    std::string id_str = row_string.substr(0, delimiter_pos);
    row.row_id = std::stoi(id_str);
    std::cout << "row_id: " << row.row_id << std::endl;

    // Parse Columns
    std::string content = row_string.substr(delimiter_pos + 1);
    content = content.substr(1, content.size() - 2);

    std::stringstream content_ss(content);
    std::string pair_str;
    
    while (std::getline(content_ss, pair_str, ',')) {
        size_t kv_sep = pair_str.find(':');
        
        if (kv_sep != std::string::npos) {
            std::string atr = pair_str.substr(0, kv_sep);
            std::string val = pair_str.substr(kv_sep + 1);

            row.columns[atr] = string_to_any(val, DataType::VARCHAR);
            std::cout << "{" << atr << ", " << val << "}" << std::endl;
        }
    }

    return row;
}

std::string FailureRecoveryManager::operation_to_string(Operation op) {
    switch (op) {
        case Operation::BEGIN: return "BEGIN";
        case Operation::COMMIT: return "COMMIT";
        case Operation::ABORT: return "ABORT";
        case Operation::UPDATE: return "UPDATE";
        case Operation::INSERT: return "INSERT";
        case Operation::DELETE: return "DELETE";
        default: return "UNKNOWN";
    }
}

Operation FailureRecoveryManager::string_to_operation(const std::string& str) {
    if (str == "BEGIN") return Operation::BEGIN;
    if (str == "COMMIT") return Operation::COMMIT;
    if (str == "ABORT") return Operation::ABORT;
    if (str == "UPDATE") return Operation::UPDATE;
    if (str == "INSERT") return Operation::INSERT;
    if (str == "DELETE") return Operation::DELETE;
    return Operation::ABORT; // Default safe fallback
}

std::string FailureRecoveryManager::serialize_log(const LogEntry& entry) {
    std::stringstream ss;

    ss << entry.log_id << "|";
    ss << entry.transaction_id << "|";
    ss << static_cast<long long>(entry.timestamp) << "|";
    ss << operation_to_string(entry.operation) << "|";
    ss << (entry.table_name.empty() ? "NON_TABLE" : entry.table_name) << "|";
    ss << row_to_string(entry.old_value) << "|";
    ss << row_to_string(entry.new_value) << "|";
    ss << entry.query;

    return ss.str();
}

LogEntry FailureRecoveryManager::deserialize_log(const std::string& serialized_log) {
    LogEntry entry;

    std::stringstream ss(serialized_log);
    std::string segment;
    std::vector<std::string> parts;

    while (std::getline(ss, segment, '|')) {
        parts.push_back(segment);
    }

    if (parts.size() < 8) {
        std::cerr << "FRM Error: Log korup/tidak lengkap -> " << serialized_log << std::endl;
        entry.log_id = -1;
        return entry;
    }

    try {
        entry.log_id = std::stoi(parts[0]);
        std::cout << "log_id " << entry.log_id << std::endl;

        entry.transaction_id = std::stoi(parts[1]);
        std::cout << "tx_id " << entry.transaction_id << std::endl;

        entry.timestamp = static_cast<std::time_t>(std::stoll(parts[2]));
        std::cout << "timestamp " << entry.timestamp << std::endl;

        entry.operation = string_to_operation(parts[3]);
        std::cout << "op " << parts[3] << std::endl;

        entry.table_name = (parts[4] == "NON_TABLE") ? "" : parts[4];
        std::cout << "log_id " << entry.table_name << std::endl;

        entry.old_value = string_to_row(parts[5], entry.table_name);
        std::cout << "old value keluar " << std::endl;

        entry.new_value = string_to_row(parts[6], entry.table_name);
        std::cout << "new value keluar" << std::endl;

        entry.query = parts[7];
        std::cout << "log_id " << entry.log_id << std::endl;


    } catch (const std::exception& e) {
        std::cerr << "FRM Error: Gagal parsing log entry -> " << e.what() << std::endl;
        entry.log_id = -1;
    }

    return entry;
}

void FailureRecoveryManager::append_log_to_file(const std::string& serialized_log, const std::string& file_path) {
    std::ofstream outfile(file_path, std::ios::app);

    if (!outfile.is_open()) {
        std::cerr << "FRM Critical Error: Gagal membuka file log untuk ditulis -> " << file_path << std::endl;
        return;
    }

    outfile << serialized_log << "\n";
    outfile.flush();
    outfile.close();
}

std::vector<LogEntry> FailureRecoveryManager::read_all_logs(const std::string& file_path) {
    std::vector<LogEntry> logs;
    std::ifstream infile(file_path);

    if (!infile.is_open()) {
        std::cout << "FRM Info: File log tidak ditemukan (" << file_path << "). Memulai dengan log kosong." << std::endl;
        return logs;
    }

    std::string line;
    int line_number = 0;

    while (std::getline(infile, line)) {
        line_number++;

        if (line.empty()) continue;
        LogEntry entry = deserialize_log(line);
        
        if (entry.log_id != -1) {
            logs.push_back(entry);
        } else {
            std::cerr << "FRM Warning: Skipping corrupt entry at line " << line_number << std::endl;
        }
    }

    infile.close();
    std::cout << "FRM: Berhasil memuat " << logs.size() << " entri log dari disk." << std::endl;
    return logs;
}

} // namespace mdbms::fr

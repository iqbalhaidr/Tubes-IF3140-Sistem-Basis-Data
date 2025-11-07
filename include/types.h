#pragma once

#include <string>
#include <vector>
#include <any>

namespace mdbms {

// Tempat untuk struct/tipe data yang dipakai bersama
// Hanya contoh sementara, silakan diubah sesuai kebutuhan

struct Rows {
    int rows_count;
    std::vector<std::any> data; // Sesuaikan tipe datanya
};

struct ExecutionResult {
    int transaction_id;
    std::string message;
    std::string query;
    Rows data;
};

} // namespace mdbms

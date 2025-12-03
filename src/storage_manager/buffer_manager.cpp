#include "storage_manager.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstring>
#include <filesystem>

namespace mdbms::sm {

BufferManager::BufferManager(const std::string& data_dir, size_t capacity)
    : data_dir_(data_dir), capacity_(capacity) {
    std::cout << "[BufferManager] Initialized with capacity: " << capacity << std::endl;
}

BufferManager::~BufferManager() {
    std::cout << "[BufferManager] Flushing all dirty pages before shutdown..." << std::endl;
    flush_all();
}

int64_t BufferManager::get_current_time() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void BufferManager::register_schema(const std::string& table_name, const TableSchema& schema) {
    schema_cache_[table_name] = schema;
}

int BufferManager::get_max_page_id(const std::string& table_name) {
    int max_page_id = -1;
    
    // Check buffer pool
    for (const auto& entry : buffer_pool_) {
        if (entry.first.first == table_name) {
            max_page_id = std::max(max_page_id, entry.first.second);
        }
    }
    
    // Check disk
    std::string filename = data_dir_ + "/" + table_name + ".dat";
    if (std::filesystem::exists(filename)) {
        auto file_size = std::filesystem::file_size(filename);
        if (file_size > 0) {
            int disk_max_page_id = static_cast<int>((file_size + PAGE_SIZE - 1) / PAGE_SIZE) - 1;
            max_page_id = std::max(max_page_id, disk_max_page_id);
        }
    }
    
    return max_page_id;
}

TableSchema BufferManager::get_schema(const std::string& table_name) {
    // Check cache first
    auto it = schema_cache_.find(table_name);
    if (it != schema_cache_.end()) {
        return it->second;
    }

    // Schema not in cache - this shouldn't happen in normal operation
    // Return empty schema as fallback
    std::cerr << "[BufferManager] WARNING: Schema not found for table: " << table_name << std::endl;
    return TableSchema();
}

BufferPage* BufferManager::get_page(const std::string& table_name, int page_id, const TableSchema& schema) {
    auto key = std::make_pair(table_name, page_id);
    
    // Check if page is already in buffer
    auto it = buffer_pool_.find(key);
    if (it != buffer_pool_.end()) {
        it->second.pin_count++;
        it->second.last_access_time = get_current_time();
        std::cout << "[BufferManager] Page hit: " << table_name << " page " << page_id 
                  << " (pin_count=" << it->second.pin_count << ")" << std::endl;
        return &(it->second);
    }

    // Page not in buffer - need to load from disk
    std::cout << "[BufferManager] Page miss: " << table_name << " page " << page_id << std::endl;

    // Check if buffer is full
    if (buffer_pool_.size() >= capacity_) {
        evict_page();
    }

    // Create new page entry
    BufferPage new_page;
    new_page.table_name = table_name;
    new_page.page_id = page_id;
    new_page.pin_count = 1;
    new_page.is_dirty = false;
    new_page.last_access_time = get_current_time();

    // Load from disk
    load_page_internal(table_name, page_id, new_page, schema);

    // Insert into buffer pool
    buffer_pool_[key] = new_page;
    
    return &buffer_pool_[key];
}

BufferPage* BufferManager::new_page(const std::string& table_name) {
    // Find the highest page_id for this table
    int max_page_id = -1;
    for (const auto& entry : buffer_pool_) {
        if (entry.first.first == table_name) {
            max_page_id = std::max(max_page_id, entry.first.second);
        }
    }

    // Check disk for existing pages
    std::string filename = data_dir_ + "/" + table_name + ".dat";
    if (std::filesystem::exists(filename)) {
        auto file_size = std::filesystem::file_size(filename);
        int disk_max_page_id = static_cast<int>(file_size / PAGE_SIZE);
        max_page_id = std::max(max_page_id, disk_max_page_id - 1);
    }

    int new_page_id = max_page_id + 1;
    
    std::cout << "[BufferManager] Creating new page: " << table_name 
              << " page " << new_page_id << std::endl;

    // Check if buffer is full
    if (buffer_pool_.size() >= capacity_) {
        evict_page();
    }

    // Create new page
    BufferPage new_page;
    new_page.table_name = table_name;
    new_page.page_id = new_page_id;
    new_page.pin_count = 1;
    new_page.is_dirty = true; // New page is always dirty
    new_page.last_access_time = get_current_time();

    auto key = std::make_pair(table_name, new_page_id);
    buffer_pool_[key] = new_page;
    
    return &buffer_pool_[key];
}

void BufferManager::unpin_page(const std::string& table_name, int page_id, bool mark_dirty) {
    auto key = std::make_pair(table_name, page_id);
    auto it = buffer_pool_.find(key);
    
    if (it != buffer_pool_.end()) {
        if (it->second.pin_count > 0) {
            it->second.pin_count--;
        }
        if (mark_dirty) {
            it->second.is_dirty = true;
        }
        std::cout << "[BufferManager] Unpinned: " << table_name << " page " << page_id 
                  << " (pin_count=" << it->second.pin_count 
                  << ", dirty=" << it->second.is_dirty << ")" << std::endl;
    }
}

void BufferManager::evict_page() {
    // LRU eviction: find unpinned page with oldest last_access_time
    auto victim = buffer_pool_.end();
    int64_t oldest_time = std::numeric_limits<int64_t>::max();

    for (auto it = buffer_pool_.begin(); it != buffer_pool_.end(); ++it) {
        if (it->second.pin_count == 0 && it->second.last_access_time < oldest_time) {
            oldest_time = it->second.last_access_time;
            victim = it;
        }
    }

    if (victim == buffer_pool_.end()) {
        std::cerr << "[BufferManager] ERROR: All pages are pinned! Cannot evict." << std::endl;
        throw std::runtime_error("Buffer pool full and all pages pinned");
    }

    std::cout << "[BufferManager] Evicting page: " << victim->first.first 
              << " page " << victim->first.second 
              << " (dirty=" << victim->second.is_dirty << ")" << std::endl;

    // Flush if dirty
    if (victim->second.is_dirty) {
        try {
            TableSchema schema = get_schema(victim->first.first);
            flush_page_internal(victim->second, schema);
        } catch (const std::exception& e) {
            std::cerr << "[BufferManager] ERROR flushing page during eviction: " << e.what() << std::endl;
        }
    }

    // Remove from buffer pool
    buffer_pool_.erase(victim);
}

void BufferManager::flush_all() {
    for (auto& entry : buffer_pool_) {
        if (entry.second.is_dirty) {
            try {
                TableSchema schema = get_schema(entry.first.first);
                flush_page_internal(entry.second, schema);
                entry.second.is_dirty = false;
            } catch (const std::exception& e) {
                std::cerr << "[BufferManager] ERROR flushing page " << entry.first.first 
                          << " page " << entry.first.second << ": " << e.what() << std::endl;
            }
        }
    }
}

void BufferManager::flush_page(const std::string& table_name, int page_id) {
    auto key = std::make_pair(table_name, page_id);
    auto it = buffer_pool_.find(key);
    
    if (it != buffer_pool_.end() && it->second.is_dirty) {
        TableSchema schema = get_schema(table_name);
        flush_page_internal(it->second, schema);
        it->second.is_dirty = false;
    }
}

void BufferManager::load_page_internal(const std::string& table_name, int page_id, 
                                       BufferPage& page, const TableSchema& schema) {
    std::string filename = data_dir_ + "/" + table_name + ".dat";
    
    // Check if file exists
    if (!std::filesystem::exists(filename)) {
        std::cout << "[BufferManager] File doesn't exist, returning empty page: " << filename << std::endl;
        return;
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[BufferManager] Failed to open file: " << filename << std::endl;
        return;
    }

    // Seek to page location
    int64_t page_offset = page_id_to_offset(page_id);
    file.seekg(page_offset, std::ios::beg);
    
    if (!file.good()) {
        std::cout << "[BufferManager] Page beyond file size, returning empty page" << std::endl;
        file.close();
        return;
    }

    // Read PAGE_SIZE bytes
    std::vector<uint8_t> buffer(PAGE_SIZE);
    file.read(reinterpret_cast<char*>(buffer.data()), PAGE_SIZE);
    std::streamsize bytes_read = file.gcount();
    file.close();

    if (bytes_read == 0) {
        std::cout << "[BufferManager] No data in page" << std::endl;
        return;
    }

    // Deserialize rows from buffer
    const uint8_t* ptr = buffer.data();
    const uint8_t* end = ptr + bytes_read;

    while (ptr < end) {
        // Check if enough bytes for at least one field
        if (ptr + sizeof(int32_t) > end) break;

        Row row = deserialize_row_from_buffer(ptr, end, schema);
        if (row.columns.empty()) {
            break; // No more valid rows
        }
        page.rows.push_back(row);
    }

    std::cout << "[BufferManager] Loaded " << page.rows.size() << " rows from page " << page_id << std::endl;
}

void BufferManager::flush_page_internal(BufferPage& page, const TableSchema& schema_param) {
    std::string filename = data_dir_ + "/" + page.table_name + ".dat";
    
    std::cout << "[BufferManager] Flushing page: " << page.table_name 
              << " page " << page.page_id << " (" << page.rows.size() << " rows)" << std::endl;

    // Load schema if not provided
    TableSchema schema = schema_param;
    if (schema.column_names.empty()) {
        // Load schema from file
        std::string schema_file = data_dir_ + "/" + page.table_name + ".schema";
        if (!std::filesystem::exists(schema_file)) {
            std::cerr << "[BufferManager] WARNING: Schema file not found, skipping flush" << std::endl;
            return;
        }
        // For now, we'll skip flushing if schema is not provided
        // In production, you'd implement schema loading here
        std::cerr << "[BufferManager] WARNING: Schema not provided for flush, skipping" << std::endl;
        return;
    }

    // Serialize all rows to buffer
    std::vector<uint8_t> buffer;
    buffer.reserve(PAGE_SIZE);

    for (const auto& row : page.rows) {
        serialize_row_to_buffer(buffer, row, schema);
    }

    // Pad to PAGE_SIZE if needed
    if (buffer.size() < PAGE_SIZE) {
        buffer.resize(PAGE_SIZE, 0);
    } else if (buffer.size() > PAGE_SIZE) {
        std::cerr << "[BufferManager] WARNING: Page size exceeded!" << std::endl;
        buffer.resize(PAGE_SIZE); // Truncate
    }

    // Write to disk
    std::fstream file(filename, std::ios::binary | std::ios::in | std::ios::out);
    
    if (!file.is_open()) {
        // File doesn't exist, create it
        file.open(filename, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create file: " + filename);
        }
    }

    int64_t page_offset = page_id_to_offset(page.page_id);
    file.seekp(page_offset, std::ios::beg);
    file.write(reinterpret_cast<const char*>(buffer.data()), PAGE_SIZE);
    
    if (!file.good()) {
        throw std::runtime_error("Failed to write page to disk");
    }

    file.close();
}

void BufferManager::serialize_row_to_buffer(std::vector<uint8_t>& buffer, const Row& row, 
                                            const TableSchema& schema) {
    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        const std::string& col_name = schema.column_names[i];
        DataType col_type = schema.column_types[i];

        if (!row.columns.count(col_name)) {
            throw std::runtime_error("Missing column in row: " + col_name);
        }

        const std::any& value = row.columns.at(col_name);

        if (col_type == DataType::INTEGER) {
            int32_t val = std::any_cast<int>(value);
            uint8_t* bytes = reinterpret_cast<uint8_t*>(&val);
            buffer.insert(buffer.end(), bytes, bytes + sizeof(int32_t));
        }
        else if (col_type == DataType::FLOAT) {
            float val;
            try {
                val = std::any_cast<float>(value);
            } catch (const std::bad_any_cast&) {
                val = static_cast<float>(std::any_cast<double>(value));
            }
            uint8_t* bytes = reinterpret_cast<uint8_t*>(&val);
            buffer.insert(buffer.end(), bytes, bytes + sizeof(float));
        }
        else if (col_type == DataType::VARCHAR || col_type == DataType::CHAR) {
            std::string str = std::any_cast<std::string>(value);
            uint32_t len = static_cast<uint32_t>(str.length());
            
            uint8_t* len_bytes = reinterpret_cast<uint8_t*>(&len);
            buffer.insert(buffer.end(), len_bytes, len_bytes + sizeof(uint32_t));
            buffer.insert(buffer.end(), str.begin(), str.end());
        }
    }
}

Row BufferManager::deserialize_row_from_buffer(const uint8_t*& ptr, const uint8_t* end, 
                                               const TableSchema& schema) {
    Row row;
    row.table_name = schema.table_name;

    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        const std::string& col_name = schema.column_names[i];
        DataType col_type = schema.column_types[i];

        if (col_type == DataType::INTEGER) {
            if (ptr + sizeof(int32_t) > end) return Row();
            int32_t val;
            std::memcpy(&val, ptr, sizeof(int32_t));
            ptr += sizeof(int32_t);
            row.columns[col_name] = static_cast<int>(val);
        }
        else if (col_type == DataType::FLOAT) {
            if (ptr + sizeof(float) > end) return Row();
            float val;
            std::memcpy(&val, ptr, sizeof(float));
            ptr += sizeof(float);
            row.columns[col_name] = val;
        }
        else if (col_type == DataType::VARCHAR || col_type == DataType::CHAR) {
            if (ptr + sizeof(uint32_t) > end) return Row();
            uint32_t len;
            std::memcpy(&len, ptr, sizeof(uint32_t));
            ptr += sizeof(uint32_t);
            
            if (ptr + len > end) return Row();
            std::string str(reinterpret_cast<const char*>(ptr), len);
            ptr += len;
            row.columns[col_name] = str;
        }
    }

    return row;
}

} // namespace mdbms::sm

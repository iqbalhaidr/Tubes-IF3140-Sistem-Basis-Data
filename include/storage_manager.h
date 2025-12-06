#pragma once
#include "types.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <any>
#include <optional>
#include <map>
#include <chrono>

namespace mdbms::sm {

// Buffer Manager Constants
const int PAGE_SIZE = 4096;
const size_t DEFAULT_BUFFER_CAPACITY = 50;

// Buffer Page Structure
struct BufferPage {
    std::string table_name;
    int page_id;
    std::vector<Row> rows;
    bool is_dirty;
    int pin_count;
    int64_t last_access_time;

    BufferPage() : page_id(-1), is_dirty(false), pin_count(0), last_access_time(0) {}
};

// Buffer Manager Class
class BufferManager {
public:
    BufferManager(const std::string& data_dir, size_t capacity = DEFAULT_BUFFER_CAPACITY);
    ~BufferManager();

    // Core functions called by StorageEngine
    BufferPage* get_page(const std::string& table_name, int page_id, const TableSchema& schema);
    BufferPage* new_page(const std::string& table_name);
    void unpin_page(const std::string& table_name, int page_id, bool mark_dirty = false);
    void flush_all();
    void flush_page(const std::string& table_name, int page_id);
    
    // Register schema for a table (called by StorageEngine)
    void register_schema(const std::string& table_name, const TableSchema& schema);
    
    // Get maximum page_id for a table (includes buffer + disk)
    int get_max_page_id(const std::string& table_name);
    
    // Checkpoint: flush all dirty pages to disk
    void checkpoint();
    
    // Evict all pages for a table from buffer WITHOUT flushing (for delete_table)
    void evict_table_without_flush(const std::string& table_name);
    
    // Check if buffer is near full (80% capacity)
    bool is_near_full() const;
    
    // Clear all buffer pool (for testing purposes)
    void clear_buffer();

    // Calculate page_id from absolute offset
    static int offset_to_page_id(int64_t offset) { return static_cast<int>(offset / PAGE_SIZE); }
    static int64_t page_id_to_offset(int page_id) { return static_cast<int64_t>(page_id) * PAGE_SIZE; }

private:
    std::string data_dir_;
    size_t capacity_;
    std::map<std::pair<std::string, int>, BufferPage> buffer_pool_;
    std::unordered_map<std::string, TableSchema> schema_cache_; // Cache schemas

    // Helper disk I/O
    void flush_page_internal(BufferPage& page, const TableSchema& schema);
    void load_page_internal(const std::string& table_name, int page_id, BufferPage& page, const TableSchema& schema);
    void evict_page();
    int64_t get_current_time();
    TableSchema get_schema(const std::string& table_name); // Load schema if not cached

    // Serialization helpers
    void serialize_row_to_buffer(std::vector<uint8_t>& buffer, const Row& row, const TableSchema& schema);
    Row deserialize_row_from_buffer(const uint8_t*& ptr, const uint8_t* end, const TableSchema& schema);
};

class HashIndexEngine {
public:
    HashIndexEngine();
    HashIndexEngine(const std::string& data_dir);

    std::unordered_map<std::string, std::string> table_index;      // table -> column
    std::unordered_map<std::string, IndexType> table_index_type;   // table -> type

    // Hash Index methods
    void build_hash_index(const TableSchema& schema, const std::string& table, const std::string& column);
    bool lookup_hash(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets);
    
    // B+ Tree Index methods
    void build_bptree_index(const TableSchema& schema, const std::string& table, const std::string& column);
    bool lookup_bptree(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets);
    void update_bptree_after_insert(const std::string& table, const std::string& column, int64_t row_offset, const Row& row);
    void update_bptree_after_delete(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row);
    void update_bptree_after_update(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row, const Row& new_row);
    
    // Generic index methods (auto-detect index type)
    void update_index_after_insert(const std::string& table, const std::string& column, int64_t row_offset, const Row& row);
    void update_index_after_update(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row, const Row& new_row);
    void update_index_after_delete(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row);
    bool lookup_index(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets);

private:
    std::string data_dir_;
    
    // Helper
    bool any_to_int32(const std::any &a, int32_t &out);
    bool any_to_float(const std::any &a, float &out);
    bool any_to_string(const std::any &a, std::string &out);
    Row deserialize_row(std::istream& in, const TableSchema& schema); // duplicate because im not sure what the best way for this
};

class StorageEngine {
public:
    // Singleton: delete copy constructor and assignment operator
    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;
    
    // Singleton: get instance
    static StorageEngine& get_instance();
    
    ~StorageEngine();

    // CRUD - Public API (DO NOT CHANGE SIGNATURES)
    Rows<Row> read_block(const DataRetrieval& retrieval);
    int write_block(const DataWrite<Row>& write);
    int delete_block(const DataDeletion& deletion);
    
    // schema manager
    std::vector<TableSchema> get_tables();
    TableSchema get_table_schema(const std::string& table);
    bool create_table(const TableSchema& schema);
    bool delete_table(const TableSchema& schema);
    
    // other
    std::map<std::string, Statistic> get_stats();
    void set_index(const std::string& table, const std::string& column, const IndexType index_type);
    
    // Helper methods for integration
    std::vector<std::string> get_column_names(const std::string& table);
    bool drop_table(const std::string& table);
    
    // Checkpoint: flush all dirty pages in buffer to disk
    void checkpoint();
    
    // Check if buffer is near full
    bool is_buffer_near_full() const;
    
    // For testing: clear buffer pool
    void clear_buffer_for_testing();
    
    // Helper for failure recovery: convert row to conditions using primary key
    std::vector<Condition> row_to_conditions(const Row& row, const std::string& table_name);

    
private:
    // Singleton: private constructor
    StorageEngine();
    StorageEngine(const std::string& data_dir);
    std::string data_dir_;
    BufferManager buffer_manager_;
    HashIndexEngine hash_index_engine;

    // Internal read helpers (now use buffer)
    Rows<Row> read_using_offsets(const DataRetrieval& retrieval, const std::vector<int64_t>& offsets);
    Rows<Row> full_scan(const DataRetrieval& retrieval);

    // CRUD helper
    void serialize_row(std::ostream& out, const Row& row, const TableSchema& schema);
    Row deserialize_row(std::istream& in, const TableSchema& schema);
    bool check_conditions(const Row& row, const std::vector<Condition>& conditions);
    bool check_and_update_tables_metadata(const TableSchema& schema);

    // Buffer-related helpers
    int64_t calculate_row_offset_in_page(const BufferPage& page, size_t row_index, const TableSchema& schema);
    size_t estimate_row_size(const Row& row, const TableSchema& schema);

    // Other Helper
    std::string to_string_any(const std::any& a);

};

} // namespace mdbms::sm

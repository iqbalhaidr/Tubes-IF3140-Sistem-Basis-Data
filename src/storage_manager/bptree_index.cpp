#include "storage_manager.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstring>
#include <memory>

namespace mdbms::sm {

// B+ Tree Configuration
constexpr int BPTREE_ORDER = 64; // Maximum children per node
constexpr int MIN_KEYS = (BPTREE_ORDER - 1) / 2;
constexpr int MAX_KEYS = BPTREE_ORDER - 1;

// B+ Tree Node Types
enum class BPTreeNodeType : uint8_t {
    INTERNAL = 0,
    LEAF = 1
};

// B+ Tree Node Structure (in-memory representation)
struct BPTreeNode {
    BPTreeNodeType type;
    int key_count;
    std::vector<std::string> keys;
    std::vector<int64_t> children;  // For internal: child node offsets, For leaf: row offsets
    std::vector<std::vector<int64_t>> leaf_values; // For leaf nodes with duplicate keys
    int64_t next_leaf;  // For leaf nodes only (linked list)
    int64_t node_offset;  // Position in file
    
    BPTreeNode(BPTreeNodeType t = BPTreeNodeType::LEAF) 
        : type(t), key_count(0), next_leaf(-1), node_offset(-1) {
        keys.reserve(MAX_KEYS);
        if (t == BPTreeNodeType::LEAF) {
            leaf_values.reserve(MAX_KEYS);
        } else {
            children.reserve(BPTREE_ORDER);
        }
    }
};

// B+ Tree Manager Class
class BPTreeManager {
public:
    BPTreeManager(const std::string& index_file) 
        : index_file_(index_file), root_offset_(-1), node_count_(0) {}
    
    // Build B+ Tree from scratch
    void build_tree(const std::vector<std::pair<std::string, std::vector<int64_t>>>& sorted_data);
    
    // Insert a key-value pair
    void insert(const std::string& key, int64_t value);
    
    // Search for a key
    bool search(const std::string& key, std::vector<int64_t>& out_values);
    
    // Delete a key-value pair
    void remove(const std::string& key, int64_t value);
    
    // Load tree from file
    bool load_tree();
    
    // Save tree to file
    void save_tree();
    
private:
    std::string index_file_;
    int64_t root_offset_;
    int node_count_;
    std::map<int64_t, std::unique_ptr<BPTreeNode>> node_cache_;
    
    // Helper functions
    BPTreeNode* get_node(int64_t offset);
    int64_t allocate_node(BPTreeNodeType type);
    void write_node(BPTreeNode* node);
    BPTreeNode* read_node(int64_t offset);
    
    // Insert helpers
    int64_t insert_internal(int64_t node_offset, const std::string& key, int64_t value, std::string& split_key, int64_t& split_child);
    void split_leaf(BPTreeNode* leaf, std::string& split_key, BPTreeNode** new_leaf);
    void split_internal(BPTreeNode* internal, std::string& split_key, BPTreeNode** new_internal);
    
    // Search helpers
    BPTreeNode* find_leaf(const std::string& key);
    int find_key_position(BPTreeNode* node, const std::string& key);
    
    // Bulk load helpers
    BPTreeNode* build_leaf_level(const std::vector<std::pair<std::string, std::vector<int64_t>>>& data, std::vector<std::pair<std::string, int64_t>>& parent_entries);
    int64_t build_internal_level(std::vector<std::pair<std::string, int64_t>>& entries);
    
    // File I/O helpers
    void write_header();
    bool read_header();
    void serialize_node(std::ostream& out, BPTreeNode* node);
    BPTreeNode* deserialize_node(std::istream& in, int64_t offset);
};

// ============================================================================
// B+ Tree Manager Implementation
// ============================================================================

void BPTreeManager::build_tree(const std::vector<std::pair<std::string, std::vector<int64_t>>>& sorted_data) {
    if (sorted_data.empty()) {
        // Empty tree
        root_offset_ = -1;
        node_count_ = 0;
        save_tree();
        return;
    }
    
    node_cache_.clear();
    node_count_ = 0;
    
    // Build leaf level first
    std::vector<std::pair<std::string, int64_t>> parent_entries;
    BPTreeNode* first_leaf = build_leaf_level(sorted_data, parent_entries);
    
    if (parent_entries.empty()) {
        // Only one leaf node - it becomes root
        root_offset_ = first_leaf->node_offset;
    } else {
        // Build internal levels bottom-up
        root_offset_ = build_internal_level(parent_entries);
    }
    
    save_tree();
}

BPTreeNode* BPTreeManager::build_leaf_level(
    const std::vector<std::pair<std::string, std::vector<int64_t>>>& data,
    std::vector<std::pair<std::string, int64_t>>& parent_entries) {
    
    BPTreeNode* first_leaf = nullptr;
    BPTreeNode* prev_leaf = nullptr;
    
    size_t i = 0;
    while (i < data.size()) {
        BPTreeNode* leaf = new BPTreeNode(BPTreeNodeType::LEAF);
        leaf->node_offset = allocate_node(BPTreeNodeType::LEAF);
        
        if (!first_leaf) first_leaf = leaf;
        
        // Fill leaf node
        while (i < data.size() && leaf->key_count < MAX_KEYS) {
            leaf->keys.push_back(data[i].first);
            leaf->leaf_values.push_back(data[i].second);
            leaf->key_count++;
            i++;
        }
        
        // Link leaves
        if (prev_leaf) {
            prev_leaf->next_leaf = leaf->node_offset;
        }
        
        // Add first key to parent entries
        if (leaf->key_count > 0) {
            parent_entries.push_back({leaf->keys[0], leaf->node_offset});
        }
        
        node_cache_[leaf->node_offset] = std::unique_ptr<BPTreeNode>(leaf);
        prev_leaf = leaf;
    }
    
    if (prev_leaf) {
        prev_leaf->next_leaf = -1;
    }
    
    return first_leaf;
}

int64_t BPTreeManager::build_internal_level(std::vector<std::pair<std::string, int64_t>>& entries) {
    while (entries.size() > 1) {
        std::vector<std::pair<std::string, int64_t>> next_level;
        
        size_t i = 0;
        while (i < entries.size()) {
            BPTreeNode* internal = new BPTreeNode(BPTreeNodeType::INTERNAL);
            internal->node_offset = allocate_node(BPTreeNodeType::INTERNAL);
            
            // First child
            internal->children.push_back(entries[i].second);
            i++;
            
            // Fill internal node
            while (i < entries.size() && internal->key_count < MAX_KEYS) {
                internal->keys.push_back(entries[i].first);
                internal->children.push_back(entries[i].second);
                internal->key_count++;
                i++;
            }
            
            // Add to next level
            if (!internal->keys.empty()) {
                next_level.push_back({internal->keys[0], internal->node_offset});
            }
            
            node_cache_[internal->node_offset] = std::unique_ptr<BPTreeNode>(internal);
        }
        
        entries = std::move(next_level);
    }
    
    return entries.empty() ? -1 : entries[0].second;
}

void BPTreeManager::insert(const std::string& key, int64_t value) {
    if (root_offset_ == -1) {
        // Empty tree - create root leaf
        BPTreeNode* root = new BPTreeNode(BPTreeNodeType::LEAF);
        root->node_offset = allocate_node(BPTreeNodeType::LEAF);
        root->keys.push_back(key);
        root->leaf_values.push_back({value});
        root->key_count = 1;
        root->next_leaf = -1;
        
        root_offset_ = root->node_offset;
        node_cache_[root->node_offset] = std::unique_ptr<BPTreeNode>(root);
        write_node(root);
        write_header();
        return;
    }
    
    std::string split_key;
    int64_t split_child = -1;
    
    int64_t result = insert_internal(root_offset_, key, value, split_key, split_child);
    
    if (split_child != -1) {
        // Root was split - create new root
        BPTreeNode* new_root = new BPTreeNode(BPTreeNodeType::INTERNAL);
        new_root->node_offset = allocate_node(BPTreeNodeType::INTERNAL);
        new_root->children.push_back(result);
        new_root->keys.push_back(split_key);
        new_root->children.push_back(split_child);
        new_root->key_count = 1;
        
        root_offset_ = new_root->node_offset;
        node_cache_[new_root->node_offset] = std::unique_ptr<BPTreeNode>(new_root);
        write_node(new_root);
    }
    
    write_header();
}

int64_t BPTreeManager::insert_internal(int64_t node_offset, const std::string& key, int64_t value, 
                                        std::string& split_key, int64_t& split_child) {
    BPTreeNode* node = get_node(node_offset);
    split_child = -1;
    
    if (node->type == BPTreeNodeType::LEAF) {
        // Find position to insert
        int pos = find_key_position(node, key);
        
        // Check if key already exists
        if (pos < node->key_count && node->keys[pos] == key) {
            // Add to existing key's values
            node->leaf_values[pos].push_back(value);
        } else {
            // Insert new key
            node->keys.insert(node->keys.begin() + pos, key);
            node->leaf_values.insert(node->leaf_values.begin() + pos, std::vector<int64_t>{value});
            node->key_count++;
        }
        
        // Check if split needed
        if (node->key_count > MAX_KEYS) {
            BPTreeNode* new_leaf = nullptr;
            split_leaf(node, split_key, &new_leaf);
            split_child = new_leaf->node_offset;
        }
        
        write_node(node);
        return node->node_offset;
    } else {
        // Internal node - find child to insert into
        int pos = find_key_position(node, key);
        int child_index = (pos < node->key_count && key >= node->keys[pos]) ? pos + 1 : pos;
        
        std::string child_split_key;
        int64_t child_split_child = -1;
        
        int64_t child_offset = insert_internal(node->children[child_index], key, value, 
                                                child_split_key, child_split_child);
        
        if (child_split_child != -1) {
            // Child was split - insert split key
            node->keys.insert(node->keys.begin() + child_index, child_split_key);
            node->children[child_index] = child_offset;
            node->children.insert(node->children.begin() + child_index + 1, child_split_child);
            node->key_count++;
            
            // Check if this node needs split
            if (node->key_count > MAX_KEYS) {
                BPTreeNode* new_internal = nullptr;
                split_internal(node, split_key, &new_internal);
                split_child = new_internal->node_offset;
            }
        }
        
        write_node(node);
        return node->node_offset;
    }
}

void BPTreeManager::split_leaf(BPTreeNode* leaf, std::string& split_key, BPTreeNode** new_leaf_ptr) {
    BPTreeNode* new_leaf = new BPTreeNode(BPTreeNodeType::LEAF);
    new_leaf->node_offset = allocate_node(BPTreeNodeType::LEAF);
    
    int mid = (leaf->key_count + 1) / 2;
    
    // Move half to new leaf
    new_leaf->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
    new_leaf->leaf_values.assign(leaf->leaf_values.begin() + mid, leaf->leaf_values.end());
    new_leaf->key_count = leaf->key_count - mid;
    
    // Update old leaf
    leaf->keys.resize(mid);
    leaf->leaf_values.resize(mid);
    leaf->key_count = mid;
    
    // Update links
    new_leaf->next_leaf = leaf->next_leaf;
    leaf->next_leaf = new_leaf->node_offset;
    
    // Split key is first key of new leaf
    split_key = new_leaf->keys[0];
    
    node_cache_[new_leaf->node_offset] = std::unique_ptr<BPTreeNode>(new_leaf);
    write_node(new_leaf);
    *new_leaf_ptr = new_leaf;
}

void BPTreeManager::split_internal(BPTreeNode* internal, std::string& split_key, BPTreeNode** new_internal_ptr) {
    BPTreeNode* new_internal = new BPTreeNode(BPTreeNodeType::INTERNAL);
    new_internal->node_offset = allocate_node(BPTreeNodeType::INTERNAL);
    
    int mid = internal->key_count / 2;
    
    // Split key goes up
    split_key = internal->keys[mid];
    
    // Move right half to new internal
    new_internal->keys.assign(internal->keys.begin() + mid + 1, internal->keys.end());
    new_internal->children.assign(internal->children.begin() + mid + 1, internal->children.end());
    new_internal->key_count = internal->key_count - mid - 1;
    
    // Update old internal
    internal->keys.resize(mid);
    internal->children.resize(mid + 1);
    internal->key_count = mid;
    
    node_cache_[new_internal->node_offset] = std::unique_ptr<BPTreeNode>(new_internal);
    write_node(new_internal);
    *new_internal_ptr = new_internal;
}

bool BPTreeManager::search(const std::string& key, std::vector<int64_t>& out_values) {
    out_values.clear();
    
    if (root_offset_ == -1) {
        std::cerr << "BPTree: Search called on empty tree" << std::endl;
        return false;
    }
    
    BPTreeNode* leaf = find_leaf(key);
    if (!leaf) {
        std::cerr << "BPTree: Failed to find leaf for key: " << key << std::endl;
        return false;
    }
    
    int pos = find_key_position(leaf, key);
    
    if (pos < leaf->key_count && leaf->keys[pos] == key) {
        out_values = leaf->leaf_values[pos];
        return true;
    }
    
    return false;
}

BPTreeNode* BPTreeManager::find_leaf(const std::string& key) {
    if (root_offset_ == -1) return nullptr;
    
    int64_t current_offset = root_offset_;
    int depth = 0;
    const int MAX_DEPTH = 100; // Prevent infinite loops
    
    while (depth < MAX_DEPTH) {
        BPTreeNode* node = get_node(current_offset);
        
        if (!node) {
            std::cerr << "BPTree: Failed to load node at offset " << current_offset << std::endl;
            return nullptr;
        }
        
        if (node->type == BPTreeNodeType::LEAF) {
            return node;
        }
        
        // Internal node - find child
        int pos = find_key_position(node, key);
        int child_index = (pos < node->key_count && key >= node->keys[pos]) ? pos + 1 : pos;
        
        if (child_index >= static_cast<int>(node->children.size())) {
            std::cerr << "BPTree: Invalid child index " << child_index << " for node with " 
                      << node->children.size() << " children" << std::endl;
            return nullptr;
        }
        
        current_offset = node->children[child_index];
        depth++;
    }
    
    std::cerr << "BPTree: Maximum depth exceeded in find_leaf" << std::endl;
    return nullptr;
}

int BPTreeManager::find_key_position(BPTreeNode* node, const std::string& key) {
    return std::lower_bound(node->keys.begin(), node->keys.begin() + node->key_count, key) 
           - node->keys.begin();
}

void BPTreeManager::remove(const std::string& key, int64_t value) {
    // Simplified remove - just remove value from leaf
    BPTreeNode* leaf = find_leaf(key);
    if (!leaf) return;
    
    int pos = find_key_position(leaf, key);
    
    if (pos < leaf->key_count && leaf->keys[pos] == key) {
        auto& values = leaf->leaf_values[pos];
        values.erase(std::remove(values.begin(), values.end(), value), values.end());
        
        // If no values left, remove key
        if (values.empty()) {
            leaf->keys.erase(leaf->keys.begin() + pos);
            leaf->leaf_values.erase(leaf->leaf_values.begin() + pos);
            leaf->key_count--;
        }
        
        write_node(leaf);
    }
}

BPTreeNode* BPTreeManager::get_node(int64_t offset) {
    auto it = node_cache_.find(offset);
    if (it != node_cache_.end()) {
        return it->second.get();
    }
    
    BPTreeNode* node = read_node(offset);
    node_cache_[offset] = std::unique_ptr<BPTreeNode>(node);
    return node;
}

int64_t BPTreeManager::allocate_node(BPTreeNodeType type) {
    return node_count_++;
}

void BPTreeManager::write_node(BPTreeNode* node) {
    std::fstream file(index_file_, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        file.open(index_file_, std::ios::binary | std::ios::out);
    }
    
    // Calculate node position (after header)
    int64_t pos = 32 + node->node_offset * 8192; // 8KB per node
    file.seekp(pos, std::ios::beg);
    
    serialize_node(file, node);
    file.flush();  // Ensure data is written to disk
    file.close();
}

BPTreeNode* BPTreeManager::read_node(int64_t offset) {
    std::ifstream file(index_file_, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "BPTree: Failed to open index file for reading: " << index_file_ << std::endl;
        return nullptr;
    }
    
    int64_t pos = 32 + offset * 8192;
    file.seekg(pos, std::ios::beg);
    
    if (!file.good()) {
        std::cerr << "BPTree: Failed to seek to position " << pos << std::endl;
        file.close();
        return nullptr;
    }
    
    BPTreeNode* node = deserialize_node(file, offset);
    file.close();
    
    if (!node) {
        std::cerr << "BPTree: Failed to deserialize node at offset " << offset << std::endl;
    }
    
    return node;
}

void BPTreeManager::serialize_node(std::ostream& out, BPTreeNode* node) {
    // Node type
    uint8_t type = static_cast<uint8_t>(node->type);
    out.write(reinterpret_cast<char*>(&type), 1);
    
    // Key count
    int32_t kc = node->key_count;
    out.write(reinterpret_cast<char*>(&kc), 4);
    
    // Next leaf (for leaf nodes)
    out.write(reinterpret_cast<char*>(&node->next_leaf), 8);
    
    // Keys
    for (int i = 0; i < node->key_count; i++) {
        uint32_t len = node->keys[i].length();
        out.write(reinterpret_cast<char*>(&len), 4);
        out.write(node->keys[i].data(), len);
    }
    
    if (node->type == BPTreeNodeType::LEAF) {
        // Leaf values
        for (int i = 0; i < node->key_count; i++) {
            uint32_t val_count = node->leaf_values[i].size();
            out.write(reinterpret_cast<char*>(&val_count), 4);
            for (int64_t val : node->leaf_values[i]) {
                out.write(reinterpret_cast<char*>(&val), 8);
            }
        }
    } else {
        // Children
        for (int i = 0; i <= node->key_count; i++) {
            out.write(reinterpret_cast<char*>(&node->children[i]), 8);
        }
    }
}

BPTreeNode* BPTreeManager::deserialize_node(std::istream& in, int64_t offset) {
    uint8_t type;
    in.read(reinterpret_cast<char*>(&type), 1);
    
    if (!in.good()) {
        std::cerr << "BPTree: Failed to read node type" << std::endl;
        return nullptr;
    }
    
    BPTreeNode* node = new BPTreeNode(static_cast<BPTreeNodeType>(type));
    node->node_offset = offset;
    
    int32_t kc;
    in.read(reinterpret_cast<char*>(&kc), 4);
    
    if (!in.good() || kc < 0 || kc > MAX_KEYS) {
        std::cerr << "BPTree: Invalid key count " << kc << std::endl;
        delete node;
        return nullptr;
    }
    
    node->key_count = kc;
    
    in.read(reinterpret_cast<char*>(&node->next_leaf), 8);
    
    // Keys
    for (int i = 0; i < kc; i++) {
        uint32_t len;
        in.read(reinterpret_cast<char*>(&len), 4);
        std::string key(len, '\0');
        in.read(&key[0], len);
        node->keys.push_back(key);
    }
    
    if (node->type == BPTreeNodeType::LEAF) {
        // Leaf values
        for (int i = 0; i < kc; i++) {
            uint32_t val_count;
            in.read(reinterpret_cast<char*>(&val_count), 4);
            std::vector<int64_t> values;
            for (uint32_t j = 0; j < val_count; j++) {
                int64_t val;
                in.read(reinterpret_cast<char*>(&val), 8);
                values.push_back(val);
            }
            node->leaf_values.push_back(values);
        }
    } else {
        // Children
        for (int i = 0; i <= kc; i++) {
            int64_t child;
            in.read(reinterpret_cast<char*>(&child), 8);
            node->children.push_back(child);
        }
    }
    
    return node;
}

void BPTreeManager::write_header() {
    std::fstream file(index_file_, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        file.open(index_file_, std::ios::binary | std::ios::out);
    }
    
    file.seekp(0, std::ios::beg);
    
    // Magic
    file.write("BPTR", 4);
    
    // Version
    uint32_t version = 1;
    file.write(reinterpret_cast<char*>(&version), 4);
    
    // Root offset
    file.write(reinterpret_cast<char*>(&root_offset_), 8);
    
    // Node count
    file.write(reinterpret_cast<char*>(&node_count_), 4);
    
    file.flush();  // Ensure data is written to disk
    file.close();
}

bool BPTreeManager::read_header() {
    std::ifstream file(index_file_, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "BPTree: Cannot open index file: " << index_file_ << std::endl;
        return false;
    }
    
    char magic[4];
    file.read(magic, 4);
    if (std::string(magic, 4) != "BPTR") {
        std::cerr << "BPTree: Invalid magic header: " << std::string(magic, 4) << std::endl;
        file.close();
        return false;
    }
    
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), 4);
    
    file.read(reinterpret_cast<char*>(&root_offset_), 8);
    file.read(reinterpret_cast<char*>(&node_count_), 4);
    
    std::cout << "BPTree: Loaded header - root_offset=" << root_offset_ 
              << ", node_count=" << node_count_ << std::endl;
    
    file.close();
    return true;
}

bool BPTreeManager::load_tree() {
    return read_header();
}

void BPTreeManager::save_tree() {
    write_header();
    // Nodes are written individually during operations
}

// ============================================================================
// HashIndexEngine B+ Tree Integration
// ============================================================================

void HashIndexEngine::build_bptree_index(const TableSchema& schema, const std::string& table, const std::string& column) {
    std::string datafile = data_dir_ + "/" + table + ".dat";
    std::string idxfile = data_dir_ + "/" + table + "." + column + ".bpt";
    
    std::cout << "SM: Building B+ Tree index for " << table << "." << column << "..." << std::endl;
    
    std::ifstream file(datafile, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "SM: Failed to open " << datafile << std::endl;
        return;
    }
    
    // Find column
    int col_idx = -1;
    for (size_t i = 0; i < schema.column_names.size(); ++i) {
        if (schema.column_names[i] == column) {
            col_idx = i;
            break;
        }
    }
    
    if (col_idx == -1) {
        std::cerr << "SM: Column not found: " << column << std::endl;
        return;
    }
    
    // Collect all key-value pairs
    std::map<std::string, std::vector<int64_t>> data_map;
    
    while (true) {
        std::streampos pos = file.tellg();
        if (!file.good()) break;
        
        Row row = deserialize_row(file, schema);
        if (!file.good()) break;
        
        auto it = row.columns.find(column);
        if (it == row.columns.end()) continue;
        
        std::string keystr;
        int32_t iv;
        float fv;
        
        if (any_to_string(it->second, keystr)) {
            // ok
        } else if (any_to_int32(it->second, iv)) {
            keystr = std::to_string(iv);
        } else if (any_to_float(it->second, fv)) {
            keystr = std::to_string(fv);
        } else {
            continue;
        }
        
        data_map[keystr].push_back(static_cast<int64_t>(pos));
    }
    
    file.close();
    
    // Convert to sorted vector
    std::vector<std::pair<std::string, std::vector<int64_t>>> sorted_data;
    sorted_data.reserve(data_map.size());
    for (auto& kv : data_map) {
        sorted_data.push_back(std::move(kv));
    }
    
    // Build B+ Tree
    BPTreeManager tree(idxfile);
    tree.build_tree(sorted_data);
    
    std::cout << "SM: B+ Tree index created: " << idxfile << std::endl;
}

bool HashIndexEngine::lookup_bptree(const std::string& table, const std::string& column, const std::any& operand, std::vector<int64_t>& out_offsets) {
    std::string idxfile = data_dir_ + "/" + table + "." + column + ".bpt";
    
    std::cout << "BPTree: Looking up in index: " << idxfile << std::endl;
    
    BPTreeManager tree(idxfile);
    if (!tree.load_tree()) {
        std::cerr << "BPTree: Failed to load tree from " << idxfile << std::endl;
        return false;
    }
    
    std::cout << "BPTree: Tree loaded successfully" << std::endl;
    
    // Convert operand to string key
    std::string keystr;
    int32_t iv;
    float fv;
    
    if (any_to_string(operand, keystr)) {
        // ok
    } else if (any_to_int32(operand, iv)) {
        keystr = std::to_string(iv);
    } else if (any_to_float(operand, fv)) {
        keystr = std::to_string(fv);
    } else {
        std::cerr << "BPTree: Failed to convert operand to key" << std::endl;
        return false;
    }
    
    std::cout << "BPTree: Searching for key: " << keystr << std::endl;
    bool result = tree.search(keystr, out_offsets);
    std::cout << "BPTree: Search result: " << (result ? "found" : "not found") 
              << ", values: " << out_offsets.size() << std::endl;
    
    return result;
}

void HashIndexEngine::update_bptree_after_insert(const std::string& table, const std::string& column, int64_t row_offset, const Row& row) {
    std::string idxfile = data_dir_ + "/" + table + "." + column + ".bpt";
    
    auto it = row.columns.find(column);
    if (it == row.columns.end()) return;
    
    std::string keystr;
    int32_t iv;
    float fv;
    
    if (any_to_string(it->second, keystr)) {
        // ok
    } else if (any_to_int32(it->second, iv)) {
        keystr = std::to_string(iv);
    } else if (any_to_float(it->second, fv)) {
        keystr = std::to_string(fv);
    } else {
        return;
    }
    
    BPTreeManager tree(idxfile);
    if (tree.load_tree()) {
        tree.insert(keystr, row_offset);
    }
}

void HashIndexEngine::update_bptree_after_delete(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row) {
    std::string idxfile = data_dir_ + "/" + table + "." + column + ".bpt";
    
    auto it = old_row.columns.find(column);
    if (it == old_row.columns.end()) return;
    
    std::string keystr;
    int32_t iv;
    float fv;
    
    if (any_to_string(it->second, keystr)) {
        // ok
    } else if (any_to_int32(it->second, iv)) {
        keystr = std::to_string(iv);
    } else if (any_to_float(it->second, fv)) {
        keystr = std::to_string(fv);
    } else {
        return;
    }
    
    BPTreeManager tree(idxfile);
    if (tree.load_tree()) {
        tree.remove(keystr, row_offset);
    }
}

void HashIndexEngine::update_bptree_after_update(const std::string& table, const std::string& column, int64_t row_offset, const Row& old_row, const Row& new_row) {
    // Check if indexed column changed
    auto old_val = old_row.columns.at(column);
    auto new_val = new_row.columns.at(column);
    
    if (old_val.type() == new_val.type()) {
        if (old_val.type() == typeid(int) && std::any_cast<int>(old_val) == std::any_cast<int>(new_val)) return;
        if (old_val.type() == typeid(float) && std::any_cast<float>(old_val) == std::any_cast<float>(new_val)) return;
        if (old_val.type() == typeid(std::string) && std::any_cast<std::string>(old_val) == std::any_cast<std::string>(new_val)) return;
    }
    
    // Key changed - remove old and insert new
    update_bptree_after_delete(table, column, row_offset, old_row);
    update_bptree_after_insert(table, column, row_offset, new_row);
}

} // namespace mdbms::sm

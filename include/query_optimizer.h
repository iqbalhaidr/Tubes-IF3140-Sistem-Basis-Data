#pragma once

#include <memory>
#include <string>
#include <vector>

#include "types.h"

namespace mdbms::qo {

// ============================================================================
// QUERY OPTIMIZER / OPTIMIZATION ENGINE
// ============================================================================
// Query Optimizer bertugas untuk:
// 1. Parsing: Mengubah query string menjadi struktur data (query tree)
// 2. Validation: Memvalidasi sintaks dan semantik query
// 3. Optimization: Mengoptimasi query plan menggunakan equivalence rules
// 4. Cost Estimation: Menghitung biaya eksekusi untuk berbagai query plans
//
// Optimization dilakukan dengan menerapkan equivalence rules pada query tree
// untuk menghasilkan berbagai alternatif query plan, lalu memilih yang paling
// efisien berdasarkan cost estimation.
//
// Equivalence Rules yang didukung (dari spesifikasi):
// 1. Konjungtif selection decomposition: σ(θ1∧θ2)(E) = σ(θ1)(σ(θ2)(E))
// 2. Selection commutativity
// 3. Projection cascade elimination
// 4. Selection + Cartesian product → Join
// 5. Join commutativity
// 6. Join associativity (natural join dan theta join)
// 7. Selection distribution over join
// 8. Projection distribution over join
// ============================================================================

// Forward declaration
struct ParsedQuery;
struct QueryTree;

class OptimizationEngine {
public:
    // Constructor & Destructor
    OptimizationEngine();
    ~OptimizationEngine();

    // ========================================================================
    // PUBLIC METHODS - Method utama untuk parsing dan optimasi
    // ========================================================================

    /**
     * parse_query - Parsing query string menjadi ParsedQuery object
     *
     * Method ini melakukan:
     * 1. Lexical analysis: tokenisasi query string
     * 2. Syntax analysis: membangun parse tree
     * 3. Semantic analysis: validasi nama tabel, kolom, tipe data
     * 4. Konversi ke internal representation (QueryTree)
     *
     * Query yang didukung:
     * - SELECT [columns] FROM [tables] [WHERE conditions] [JOIN] [ORDER BY] [LIMIT]
     * - UPDATE [table] SET [assignments] WHERE [condition]
     * - INSERT INTO [table] VALUES (...)
     * - DELETE FROM [table] WHERE [condition]
     * - CREATE TABLE / DROP TABLE (bonus)
     * - BEGIN TRANSACTION / COMMIT / ROLLBACK
     *
     * @param query Query SQL dalam bentuk string
     * @return ParsedQuery object yang berisi query tree dan metadata
     * @throws std::runtime_error jika query tidak valid
     *
     * Contoh:
     *   OptimizationEngine optimizer;
     *   ParsedQuery pq = optimizer.parse_query("SELECT * FROM students WHERE gpa > 3.5");
     */
    ParsedQuery parse_query(const std::string& query);

    /**
     * optimize_query - Optimasi query plan menggunakan equivalence rules
     *
     * Method ini melakukan optimasi dengan:
     * 1. Menerapkan equivalence rules untuk generate alternatif query plans
     * 2. Menggunakan heuristic untuk filter plans yang obviously inefficient
     * 3. Menghitung cost untuk setiap alternatif menggunakan get_cost()
     * 4. Memilih query plan dengan cost terendah
     *
     * Heuristic yang diterapkan:
     * - Push selection down (lakukan filter sesegera mungkin)
     * - Push projection down (kurangi kolom sesegera mungkin)
     * - Avoid cartesian product jika mungkin
     * - Gunakan index jika tersedia
     *
     * Equivalence rules yang diimplementasikan:
     * - Rule 1-8 sesuai spesifikasi
     * - Tidak semua kombinasi perlu diimplementasikan (gunakan heuristic)
     *
     * BONUS: Implementasi genetic algorithm untuk optimization
     *
     * @param query ParsedQuery yang belum dioptimasi
     * @return ParsedQuery yang sudah dioptimasi dengan estimated_cost yang lebih rendah
     *
     * Contoh:
     *   ParsedQuery optimized = optimizer.optimize_query(parsed_query);
     */
    ParsedQuery optimize_query(const ParsedQuery& query);

    /**
     * get_cost - Menghitung estimasi biaya eksekusi query plan
     *
     * Method ini menghitung cost berdasarkan:
     * 1. Statistik database (dari Storage Manager via get_stats())
     * 2. Operasi yang dilakukan (scan, join, sort, dll)
     * 3. Selectivity factor untuk kondisi WHERE
     *
     * Formula cost untuk operasi umum:
     * - Linear scan: b_r (jumlah block)
     * - Index scan: h_i + b (height of index + matching blocks)
     * - Nested loop join: n_r * b_s (outer tuples * inner blocks)
     * - Hash join: 3(b_r + b_s) (3 passes untuk partition & join)
     * - Merge join: b_r + b_s + sort_cost
     *
     * @param query ParsedQuery yang akan dihitung costnya
     * @return Estimasi cost dalam bentuk integer (satuan: block access)
     *
     * Contoh:
     *   int cost = optimizer.get_cost(parsed_query);
     */
    int get_cost(const ParsedQuery& query);

    /**
     * set_storage_engine - Set reference ke Storage Manager
     *
     * Method ini diperlukan untuk mengambil statistik database melalui
     * storage_manager->get_stats() untuk cost estimation.
     *
     * @param storage Shared pointer ke StorageEngine
     */
    void set_storage_engine(std::shared_ptr<class StorageEngine> storage);

private:
    // ========================================================================
    // PRIVATE MEMBERS
    // ========================================================================

    std::shared_ptr<class StorageEngine> storage_engine_;  // Untuk get_stats()
    std::map<std::string, Statistic> cached_statistics_;   // Cache statistik

    // ========================================================================
    // PARSING HELPER METHODS
    // ========================================================================

    /**
     * tokenize - Memecah query string menjadi tokens
     *
     * @param query Query string
     * @return Vector of tokens
     */
    std::vector<std::string> tokenize(const std::string& query);

    /**
     * build_query_tree - Membangun query tree dari tokens
     *
     * @param tokens Vector of tokens
     * @return QueryTree root node
     */
    QueryTree* build_query_tree(const std::vector<std::string>& tokens);

    /**
     * validate_query - Validasi semantic query (tabel, kolom exist, dll)
     *
     * @param tree Query tree yang akan divalidasi
     * @return true jika valid, false jika tidak
     */
    bool validate_query(const QueryTree* tree);

    /**
     * parse_select - Parse SELECT query
     *
     * @param tokens Tokenized query
     * @param pq ParsedQuery to populate
     */
    void parse_select(const std::vector<std::string>& tokens, ParsedQuery& pq);

    /**
     * parse_update - Parse UPDATE query
     *
     * @param tokens Tokenized query
     * @param pq ParsedQuery to populate
     */
    void parse_update(const std::vector<std::string>& tokens, ParsedQuery& pq);

    /**
     * parse_insert - Parse INSERT query
     *
     * @param tokens Tokenized query
     * @param pq ParsedQuery to populate
     */
    void parse_insert(const std::vector<std::string>& tokens, ParsedQuery& pq);

    /**
     * parse_delete - Parse DELETE query
     *
     * @param tokens Tokenized query
     * @param pq ParsedQuery to populate
     */
    void parse_delete(const std::vector<std::string>& tokens, ParsedQuery& pq);

    /**
     * parse_where_clause - Parse WHERE clause
     *
     * @param tokens Tokenized query
     * @param i Current position in tokens (will be modified)
     * @param pq ParsedQuery to populate
     */
    void parse_where_clause(const std::vector<std::string>& tokens, size_t& i, ParsedQuery& pq);

    // ========================================================================
    // OPTIMIZATION HELPER METHODS - Implementasi Equivalence Rules
    // ========================================================================

    /**
     * apply_equivalence_rules - Menerapkan equivalence rules pada query tree
     *
     * @param tree Query tree asli
     * @return Vector of alternative query trees hasil penerapan rules
     */
    std::vector<QueryTree*> apply_equivalence_rules(QueryTree* tree);

    /**
     * rule_1_selection_decomposition - Rule 1: σ(θ1∧θ2)(E) = σ(θ1)(σ(θ2)(E))
     *
     * Memecah selection konjungtif menjadi urutan selection.
     * Ini memungkinkan optimasi lebih lanjut (e.g., push down salah satu).
     *
     * @param tree Query tree dengan selection konjungtif
     * @return Modified query tree dengan selection terpisah
     */
    QueryTree* rule_1_selection_decomposition(QueryTree* tree);

    /**
     * rule_3_projection_elimination - Rule 3: Π(L1)(Π(L2)(...)) = Π(L1)(E)
     *
     * Hanya projection terakhir yang diperlukan, sisanya dihilangkan.
     *
     * @param tree Query tree dengan cascade projection
     * @return Modified query tree dengan satu projection
     */
    QueryTree* rule_3_projection_elimination(QueryTree* tree);

    /**
     * rule_4_selection_to_join - Rule 4: σ(θ)(E1 × E2) = E1 ⋈(θ) E2
     *
     * Mengubah cartesian product + selection menjadi join.
     * Join lebih efisien daripada cartesian product + filter.
     *
     * @param tree Query tree dengan cartesian product + selection
     * @return Modified query tree dengan join
     */
    QueryTree* rule_4_selection_to_join(QueryTree* tree);

    /**
     * rule_5_join_commutativity - Rule 5: E1 ⋈ E2 = E2 ⋈ E1
     *
     * Menukar urutan join. Berguna untuk memilih tabel mana yang jadi outer loop.
     *
     * @param tree Query tree dengan join
     * @return Modified query tree dengan join order terbalik
     */
    QueryTree* rule_5_join_commutativity(QueryTree* tree);

    /**
     * rule_6_join_associativity - Rule 6: (E1 ⋈ E2) ⋈ E3 = E1 ⋈ (E2 ⋈ E3)
     *
     * Mengubah urutan join untuk multi-way joins.
     * Critical untuk query optimization dengan banyak tabel.
     *
     * @param tree Query tree dengan multiple joins
     * @return Modified query tree dengan join order berbeda
     */
    QueryTree* rule_6_join_associativity(QueryTree* tree);

    /**
     * rule_7_selection_distribution - Rule 7: σ(θ0)(E1 ⋈ E2) = σ(θ0)(E1) ⋈ E2
     *
     * Push selection ke bawah join (selection pushdown).
     * Mengurangi ukuran data sebelum join.
     *
     * @param tree Query tree dengan selection di atas join
     * @return Modified query tree dengan selection di bawah join
     */
    QueryTree* rule_7_selection_distribution(QueryTree* tree);

    /**
     * rule_8_projection_distribution - Rule 8: Π(L1∪L2)(E1 ⋈ E2) = Π(L1)(E1) ⋈ Π(L2)(E2)
     *
     * Push projection ke bawah join (projection pushdown).
     * Mengurangi jumlah kolom yang di-join.
     *
     * @param tree Query tree dengan projection di atas join
     * @return Modified query tree dengan projection di bawah join
     */
    QueryTree* rule_8_projection_distribution(QueryTree* tree);

    // ========================================================================
    // COST ESTIMATION HELPER METHODS
    // ========================================================================

    /**
     * estimate_selectivity - Estimasi selectivity factor untuk kondisi WHERE
     *
     * Selectivity = fraction of tuples that satisfy the condition
     * Digunakan untuk estimasi output size dari selection.
     *
     * Rules:
     * - Equality (A = v): 1 / V(A,r)
     * - Range (A > v): depends on value distribution
     * - Conjunction (θ1 ∧ θ2): sel(θ1) * sel(θ2)
     *
     * @param condition Kondisi WHERE
     * @param table_stats Statistik tabel
     * @return Selectivity factor (0.0 - 1.0)
     */
    double estimate_selectivity(const Condition& condition, const Statistic& table_stats);

    /**
     * calculate_scan_cost - Hitung cost untuk table scan
     *
     * Cost = b_r (linear scan) atau h_i + matching_blocks (index scan)
     *
     * @param table_name Nama tabel
     * @param use_index Apakah menggunakan index
     * @return Estimasi cost
     */
    int calculate_scan_cost(const std::string& table_name, bool use_index);

    /**
     * calculate_join_cost - Hitung cost untuk join operation
     *
     * Cost tergantung algoritma:
     * - Nested loop: n_r * b_s + b_r
     * - Block nested loop: b_r * b_s + b_r
     * - Index nested loop: b_r + n_r * c (c = cost per index lookup)
     * - Hash join: 3(b_r + b_s)
     * - Merge join: b_r + b_s + sort_cost
     *
     * @param left_stats Statistik tabel kiri
     * @param right_stats Statistik tabel kanan
     * @param join_algorithm Algoritma join yang digunakan
     * @return Estimasi cost
     */
    int calculate_join_cost(const Statistic& left_stats, const Statistic& right_stats,
                           const std::string& join_algorithm);

    /**
     * get_statistics - Ambil statistik tabel dari Storage Manager
     *
     * Method ini menggunakan caching untuk avoid overhead.
     *
     * @param table_name Nama tabel
     * @return Statistic object
     */
    Statistic get_statistics(const std::string& table_name);
};

// ============================================================================
// DATA STRUCTURES - Struct untuk parsing dan optimization
// ============================================================================

// QueryTree: Representasi tree untuk query execution plan
// Setiap node merepresentasikan operasi relational algebra
struct QueryTree {
    std::string type;               // Tipe operasi: SELECT, PROJECT, JOIN, SCAN, etc.
    std::string value;              // Parameter (nama tabel, kolom, kondisi, dll)
    std::vector<QueryTree*> children;  // Child nodes (sub-expressions)
    QueryTree* parent;              // Parent node

    // Additional metadata untuk optimization
    int estimated_rows;             // Estimasi jumlah baris output
    int estimated_cost;             // Estimasi cost operasi ini

    QueryTree() : parent(nullptr), estimated_rows(0), estimated_cost(0) {}

    ~QueryTree() {
        for (auto child : children) {
            delete child;
        }
    }

    // Helper method untuk add child
    void add_child(QueryTree* child) {
        children.push_back(child);
        child->parent = this;
    }

    // Helper method untuk clone tree (untuk generate alternatives)
    QueryTree* clone() const {
        QueryTree* new_tree = new QueryTree();
        new_tree->type = type;
        new_tree->value = value;
        new_tree->estimated_rows = estimated_rows;
        new_tree->estimated_cost = estimated_cost;

        for (auto child : children) {
            new_tree->add_child(child->clone());
        }

        return new_tree;
    }
};

// ParsedQuery: Representasi query yang sudah di-parse dan optimize
struct ParsedQuery {
    std::string original_query;         // Query string asli
    std::string query_type;             // SELECT, UPDATE, INSERT, DELETE, etc.
    QueryTree* query_tree;              // Query execution plan dalam bentuk tree
    int estimated_cost;                 // Estimasi cost dari optimizer

    // Query components (extracted from tree untuk kemudahan akses)
    std::vector<std::string> select_columns;    // Kolom yang di-SELECT
    std::vector<std::string> from_tables;       // Tabel-tabel di FROM
    std::vector<Condition> where_conditions;    // Kondisi WHERE
    std::vector<std::pair<std::string, std::string>> join_pairs;  // Join conditions
    std::string order_by_column;                // Kolom untuk ORDER BY
    bool order_ascending;                       // ASC atau DESC
    int limit_value;                            // Nilai LIMIT (bonus)

    // For UPDATE
    std::map<std::string, std::any> set_values; // SET column = value

    // For INSERT
    std::vector<std::any> insert_values;        // VALUES (...)

    ParsedQuery() : query_tree(nullptr), estimated_cost(0),
                   order_ascending(true), limit_value(-1) {}

    ~ParsedQuery() {
        if (query_tree) {
            delete query_tree;
        }
    }
};

} // namespace mdbms::qo

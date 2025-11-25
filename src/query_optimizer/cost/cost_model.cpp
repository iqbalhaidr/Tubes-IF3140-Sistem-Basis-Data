#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "query_optimizer.h"
#include "storage_manager.h"

namespace mdbms::qo {
namespace {

constexpr double kSeqBlockCost = 1.0;
constexpr double kCpuPerTuple = 0.01;
constexpr double kProjectionCpuFactor = 0.005;
constexpr double kHashBuildCpu = 0.02;
constexpr double kJoinCompareCpu = 0.0025;
constexpr double kDefaultSelectivity = 0.33;
constexpr double kDefaultJoinSelectivity = 0.1;
constexpr double kDefaultBlockFactor = 64.0;

struct ScanTarget {
    std::string base_name;
    std::string alias_name;
};

struct Predicate {
    bool is_equality = false;
    std::string alias_name;
    std::string column;
    std::string operation;
    std::string value;
};

struct JoinCondition {
    bool is_equality = false;
    std::string left_alias;
    std::string left_column;
    std::string right_alias;
    std::string right_column;
};

struct CostEstimate {
    double rows = 0.0;
    double cost = 0.0;
    double blocks = 0.0;
    std::unordered_map<std::string, double> alias_rows;
};

// hapus spasi di awal dan akhir
std::string trim_copy(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && isspace(static_cast<unsigned char>(input[start]))) {
        start++;
    }
    size_t end = input.size();
    while (end > start && isspace(static_cast<unsigned char>(input[end - 1]))) {
        end--;
    }
    return input.substr(start, end - start);
}

// jadiin lowercase
std::string normalize_identifier(const std::string& value) {
    std::string trimmed = trim_copy(value);
    std::string lowered;
    for (char c : trimmed) {
        lowered += std::tolower(static_cast<unsigned char>(c));
    }
    return lowered;
}

// split berdasarkan spasi ("table AS t" -> ["table", "AS", "t"])
std::vector<std::string> split_tokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : value) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

// input "employees AS e" jadi {base_name: "employees", alias_name: "e"}
ScanTarget parse_scan_target(const std::string& value) {
    const auto tokens = split_tokens(value);
    ScanTarget target{tokens[0], tokens[0]};
    if (tokens.size() >= 3 && normalize_identifier(tokens[1]) == "as") {
        target.alias_name = tokens[2];
    } else if (tokens.size() >= 2) {
        target.alias_name = tokens[1];
    }
    return target;
}

// cari statistik dari storage engine
std::optional<Statistic> lookup_stats(const std::string& table,
                                      const mdbms::sm::StorageEngine* storage) {
    const std::string normalized = normalize_identifier(table);

    if (storage) {
        const auto x = storage->build_dummy_get_stat(normalized);
        if (x.has_value()) {
            return *x;
        }
    }

    return std::nullopt;
}

double rows_to_blocks(const Statistic& stat, double rows) {
    double rows_per_block = (stat.f_r > 0) ? stat.f_r : kDefaultBlockFactor;

    if (rows <= 0) {
        return 0;
    }

    double blocks = std::ceil(rows / rows_per_block);
    return std::max(1.0, blocks);
}

void register_alias(const ScanTarget& target,
                    std::unordered_map<std::string, std::string>& alias_map) {
    std::string base = normalize_identifier(target.base_name);
    if (base.empty())
        return;

    std::string alias;
    if (target.alias_name.empty()) {
        alias = base;
    } else {
        alias = normalize_identifier(target.alias_name);
    }

    alias_map[alias] = base;
    alias_map[base] = base;
}

void get_alias(const QueryTree* node,
                     std::unordered_map<std::string, std::string>& alias_map) {
    if (!node) {
        return;
    }

    if (node->type == "SCAN") {
        ScanTarget target = parse_scan_target(node->value);
        register_alias(target, alias_map);
    }

    for (const QueryTree* child : node->children) {
        get_alias(child, alias_map);
    }
}

// parse ekspresi jadi Predicate
Predicate parse_predicate(const std::string& expression) {
    Predicate p{.is_equality = false};
    std::string s = trim_copy(expression);
    std::vector<std::string> ops = {"<=", ">=", "!=", "<>", "=", "<", ">"};
    for (const std::string& op : ops) {
        size_t position = s.find(op);
        if (position == std::string::npos) continue;

        p.operation = (op == "<>") ? "!=" : op;
        p.is_equality = (p.operation == "=");
        p.value = trim_copy(s.substr(position + op.size()));

        std::string left = trim_copy(s.substr(0, position));
        size_t dot = left.find('.');

        if (dot != std::string::npos) {
            p.alias_name = normalize_identifier(left.substr(0, dot));
            p.column = normalize_identifier(left.substr(dot + 1));
        } else {
            p.column = normalize_identifier(left);
        }
        return p;
    }
    return p;
}

// parse ekspresi jadi JoinCondition
JoinCondition parse_join_condition(const std::string& expression) {
    JoinCondition condition{.is_equality = true};
    const std::string s = trim_copy(expression);
    const size_t pos = s.find('=');
    const std::string left = trim_copy(s.substr(0, pos));
    const std::string right = trim_copy(s.substr(pos + 1));

    const auto parse_side = [](const std::string& side, std::string& alias, std::string& column) {
        const auto dot = side.find('.');
        if (dot != std::string::npos) {
            alias = normalize_identifier(side.substr(0, dot));
            column = normalize_identifier(side.substr(dot + 1));
        } else {
            alias.clear();
            column = normalize_identifier(side);
        }
    };
    parse_side(left, condition.left_alias, condition.left_column);
    parse_side(right, condition.right_alias, condition.right_column);
    return condition;
}

double lookup_distinct_values(const std::string& alias, const std::string& column,
                              const std::unordered_map<std::string, std::string>& alias_map,
                              const mdbms::sm::StorageEngine* storage) {
    std::string a = normalize_identifier(alias);
    std::string table_name;
    auto it = alias_map.find(a);
    if (it != alias_map.end()) {
        table_name = it->second;
    } else {
        table_name = a;
    }

    const auto stats_opt = lookup_stats(table_name, storage);
    if (!stats_opt.has_value()) {
        return 1.0;
    }
    const Statistic& stats = *stats_opt;
    std::string col = normalize_identifier(column);

    auto vit = stats.V_a_r.find(col);
    if (vit != stats.V_a_r.end() && vit->second > 0) {
        return vit->second;
    }
    return std::max(1, stats.n_r);
}

double predicate_selectivity(const Predicate& predicate,
                             const std::unordered_map<std::string, std::string>& alias_map,
                             const mdbms::sm::StorageEngine* storage) {
    double fallback = predicate.is_equality ? 0.1 : kDefaultSelectivity;
    if (predicate.alias_name.empty() || predicate.column.empty()) {
        return fallback;
    }

    std::string table_name;
    if (alias_map.count(predicate.alias_name) > 0) {
        table_name = alias_map.at(predicate.alias_name);
    } else {
        table_name = predicate.alias_name;
    }

    const auto stats_opt = lookup_stats(table_name, storage);
    if (!stats_opt.has_value()) {
        return fallback;
    }
    const Statistic& stats = *stats_opt;
    std::string col = predicate.column;
    auto it = stats.V_a_r.find(col);

    if (it != stats.V_a_r.end() && it->second > 0) {
        if (predicate.is_equality) {
            return 1.0 / static_cast<double>(it->second);
        } else {
            return fallback;
        }
    }

    if (!predicate.operation.empty() && predicate.operation != "=") {
        return 1.0 / 3.0;
    }
    return fallback;
}

double estimate_join_selectivity(const JoinCondition& cond,
                                 const std::unordered_map<std::string, std::string>& alias_map,
                                 const mdbms::sm::StorageEngine* storage) {
    if (!cond.is_equality) {
        return kDefaultJoinSelectivity;
    }

    double left_distinct =
        lookup_distinct_values(cond.left_alias, cond.left_column, alias_map, storage);
    double right_distinct =
        lookup_distinct_values(cond.right_alias, cond.right_column, alias_map, storage);

    double max_distinct = std::max(left_distinct, right_distinct);
    if (max_distinct <= 0.0) {
        return kDefaultJoinSelectivity;
    }
    return 1.0 / max_distinct;
}

CostEstimate estimate_node(const QueryTree* node,
                           const std::unordered_map<std::string, std::string>& alias_map,
                           const mdbms::sm::StorageEngine* storage);

CostEstimate estimate_scan(const QueryTree* node,
                           const std::unordered_map<std::string, std::string>& alias_map,
                           const mdbms::sm::StorageEngine* storage, const Predicate* predicate) {
    ScanTarget target = parse_scan_target(node->value);
    std::string base_name = normalize_identifier(target.base_name);
    std::string alias = normalize_identifier(target.alias_name);

    const auto stats_opt = lookup_stats(base_name, storage);
    if (!stats_opt.has_value()) {
        return {};
    }
    const Statistic& stats = *stats_opt;

    double base_rows = std::max(1.0, static_cast<double>(stats.n_r));
    double selectivity = predicate ? predicate_selectivity(*predicate, alias_map, storage) : 1.0;
    selectivity = std::clamp(selectivity, 1.0 / base_rows, 1.0);

    double filtered_rows = std::max(1.0, base_rows * selectivity);

    double blocks = (stats.b_r > 0) ? stats.b_r : rows_to_blocks(stats, base_rows);
    double seqCost = blocks * kSeqBlockCost + filtered_rows * kCpuPerTuple;
    double bestCost = seqCost;

    CostEstimate result;
    result.rows = filtered_rows;
    result.cost = bestCost;
    result.blocks = rows_to_blocks(stats, filtered_rows);
    result.alias_rows[alias] = filtered_rows;
    return result;
}

CostEstimate estimate_select(const QueryTree* node,
                             const std::unordered_map<std::string, std::string>& alias_map,
                             const mdbms::sm::StorageEngine* storage) {
    Predicate predicate = parse_predicate(node->value);
    const QueryTree* child = node->children.front();

    if (child && child->type == "SCAN") {
        return estimate_scan(child, alias_map, storage, &predicate);
    }
    CostEstimate result = estimate_node(child, alias_map, storage);

    double selection = predicate_selectivity(predicate, alias_map, storage);
    selection = std::clamp(selection, 1e-5, 1.0);

    result.rows = std::max(1.0, result.rows * selection);
    result.blocks = std::max(1.0, result.blocks * selection);

    if (!predicate.alias_name.empty()) {
        auto it = result.alias_rows.find(predicate.alias_name);
        if (it != result.alias_rows.end()) {
            it->second = std::max(1.0, it->second * selection);
        }
    } else {
        for (auto& entry : result.alias_rows) {
            entry.second = std::max(1.0, entry.second * selection);
        }
    }
    result.cost += result.rows * kCpuPerTuple;
    return result;
}

CostEstimate estimate_project(const QueryTree* node,
                              const std::unordered_map<std::string, std::string>& alias_map,
                              const mdbms::sm::StorageEngine* storage) {
    CostEstimate result = estimate_node(node->children.front(), alias_map, storage);
    result.cost += result.rows * kProjectionCpuFactor;
    return result;
}

CostEstimate estimate_join_node(const QueryTree* node,
                                const std::unordered_map<std::string, std::string>& alias_map,
                                const mdbms::sm::StorageEngine* storage) {
    CostEstimate left = estimate_node(node->children[0], alias_map, storage);
    CostEstimate right = estimate_node(node->children[1], alias_map, storage);

    JoinCondition condition = parse_join_condition(node->value);
    double selection = estimate_join_selectivity(condition, alias_map, storage);
    selection = std::clamp(selection, 1e-6, 1.0);

    double output_rows = left.rows * right.rows * selection;
    double nested = left.cost + right.cost + left.blocks * right.blocks * kSeqBlockCost +
                    (left.rows * right.rows) * kJoinCompareCpu;

    double hashjoin = left.cost + right.cost + (left.blocks + right.blocks) * kSeqBlockCost +
                   (left.rows + right.rows) * kHashBuildCpu;

    double merge = std::numeric_limits<double>::infinity();
    if (condition.is_equality) {
        merge = left.cost + right.cost + (left.blocks + right.blocks) * (kSeqBlockCost * 0.8);
    }

    double best = std::min({nested, hashjoin, merge});
    CostEstimate result;
    result.rows = std::max(1.0, output_rows);
    result.cost = best;
    result.blocks = std::max(1.0, (left.blocks + right.blocks) * selection);

    result.alias_rows = left.alias_rows;
    const double left_ratio = (left.rows > 0.0) ? (result.rows / left.rows) : 1.0;
    for (auto& entry : result.alias_rows) {
        entry.second = std::max(1.0, entry.second * left_ratio);
    }

    const double right_ratio = (right.rows > 0.0) ? (result.rows / right.rows) : 1.0;
    for (const auto& entry : right.alias_rows) {
        result.alias_rows[entry.first] = std::max(1.0, entry.second * right_ratio);
    }

    return result;
}

CostEstimate generic(const QueryTree* node,
                             const std::unordered_map<std::string, std::string>& alias_map,
                             const mdbms::sm::StorageEngine* storage) {
    CostEstimate result;

    for (const QueryTree* child : node->children) {
        CostEstimate ce = estimate_node(child, alias_map, storage);

        result.cost += ce.cost;
        result.rows = ce.rows;
        result.blocks = ce.blocks;

        for (auto& a : ce.alias_rows) result.alias_rows[a.first] = a.second;
    }

    if (result.rows > 0) {
        result.cost += result.rows * (kCpuPerTuple * 0.2);
    }
    return result;
}

CostEstimate estimate_node(const QueryTree* node,
                           const std::unordered_map<std::string, std::string>& alias_map,
                           const mdbms::sm::StorageEngine* storage) {
    if (node->type == "SCAN") {
        return estimate_scan(node, alias_map, storage, nullptr);
    }

    if (node->type == "SELECT") {
        return estimate_select(node, alias_map, storage);
    }

    if (node->type == "PROJECT") {
        return estimate_project(node, alias_map, storage);
    }

    if (node->type == "JOIN") {
        return estimate_join_node(node, alias_map, storage);
    }

    return generic(node, alias_map, storage);
}

} // namespace

int estimate_cost(const QueryTree& root, const mdbms::sm::StorageEngine* storage) {
    std::unordered_map<std::string, std::string> alias_map;
    get_alias(&root, alias_map);
    const CostEstimate estimate = estimate_node(&root, alias_map, storage);
    return static_cast<int>(std::round(estimate.cost));
}

} // namespace mdbms::qo
#include "utils/parser_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string_view>

namespace mdbms::qo {

std::vector<std::string> split_list(const std::string& clause, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(clause);
    std::string item;
    while (std::getline(ss, item, delim)) {
        auto token = trim(item);
        if (!token.empty()) {
            out.push_back(token);
        }
    }
    return out;
}

std::vector<std::string> split_on_keyword(const std::string& input,
                                          const std::string& keyword) {
    std::vector<std::string> parts;
    if (input.empty()) {
        return parts;
    }

    const std::string upper_keyword = to_upper(keyword);
    size_t start = 0;
    bool in_single = false;
    bool in_double = false;

    auto is_boundary = [](char c) {
        return c == '\0' || std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == ';';
    };

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (c == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (c == '\"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (in_single || in_double) {
            continue;
        }

        if (i + upper_keyword.size() > input.size()) {
            continue;
        }

        std::string candidate = to_upper(input.substr(i, upper_keyword.size()));
        if (candidate == upper_keyword) {
            char prev = (i == 0) ? '\0' : input[i - 1];
            char next = (i + upper_keyword.size() >= input.size()) ? '\0' : input[i + upper_keyword.size()];
            if (is_boundary(prev) && is_boundary(next)) {
                std::string chunk = trim(input.substr(start, i - start));
                if (!chunk.empty()) {
                    parts.push_back(chunk);
                }
                start = i + upper_keyword.size();
                i = start - 1;
            }
        }
    }

    std::string tail = trim(input.substr(start));
    if (!tail.empty()) {
        parts.push_back(tail);
    }

    return parts;
}

std::vector<std::string> split_top_level(const std::string& input) {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    for (char c : input) {
        if (c == '(') {
            depth++;
            current.push_back(c);
        } else if (c == ')') {
            depth = std::max(0, depth - 1);
            current.push_back(c);
        } else if (c == ',' && depth == 0) {
            std::string piece = trim(current);
            if (!piece.empty()) {
                parts.push_back(piece);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    std::string tail = trim(current);
    if (!tail.empty()) {
        parts.push_back(tail);
    }
    return parts;
}

std::string strip_quotes(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_single = false;
    bool in_double = false;

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (char c : input) {
        if (c == '\'' && !in_double) {
            current.push_back(c);
            if (in_single) {
                flush();
            }
            in_single = !in_single;
            continue;
        }
        if (c == '"' && !in_single) {
            current.push_back(c);
            if (in_double) {
                flush();
            }
            in_double = !in_double;
            continue;
        }

        if (in_single || in_double) {
            current.push_back(c);
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
            continue;
        }

        if (c == ',' || c == '(' || c == ')') {
            flush();
            tokens.emplace_back(1, c);
            continue;
        }

        current.push_back(c);
    }

    flush();
    return tokens;
}

std::string join_tokens(const std::vector<std::string>& tokens) {
    std::string result;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        if (token == ",") {
            if (!result.empty() && result.back() == ' ') {
                result.pop_back();
            }
            result += ", ";
            continue;
        }
        if (token == "(") {
            if (!result.empty() && result.back() != ' ') {
                result += ' ';
            }
            result += '(';
            continue;
        }
        if (token == ")") {
            if (!result.empty() && result.back() == ' ') {
                result.pop_back();
            }
            result += ')';
            if (i + 1 < tokens.size()) {
                result += ' ';
            }
            continue;
        }
        if (!result.empty() && result.back() != ' ' && result.back() != '(' && result.back() != ',') {
            result += ' ';
        }
        result += token;
    }
    return trim(result);
}

TableReference parse_table_reference(const std::string& raw) {
    std::string trimmed = trim(raw);
    TableReference ref;
    if (trimmed.empty()) {
        return ref;
    }

    std::vector<std::string> tokens;
    std::istringstream iss(trimmed);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        return ref;
    }

    ref.table_name = tokens.front();
    if (tokens.size() >= 3 && to_upper(tokens[1]) == "AS") {
        ref.alias = tokens[2];
    } else if (tokens.size() >= 2) {
        ref.alias = tokens[1];
    }
    return ref;
}

void record_table_reference(ParsedQuery& pq, TableReference ref) {
    if (ref.table_name.empty()) {
        return;
    }

    pq.table_references.push_back(ref);
    pq.from_tables.push_back(ref.table_name);
    pq.table_aliases[ref.table_name] = ref.table_name;
    if (!ref.alias.empty()) {
        pq.table_aliases[ref.alias] = ref.table_name;
    }
}

bool is_join_keyword(const std::string& upper_token) {
    static const std::array<std::string_view, 7> keywords = {
        "JOIN", "INNER", "LEFT", "RIGHT", "FULL", "OUTER", "NATURAL"
    };
    for (const auto& key : keywords) {
        if (upper_token == key) {
            return true;
        }
    }
    return false;
}

std::any parse_literal(const std::string& token) {
    std::string trimmed = trim(token);
    if (trimmed.empty()) {
        return trimmed;
    }

    if ((trimmed.front() == '\'' && trimmed.back() == '\'') ||
        (trimmed.front() == '"' && trimmed.back() == '"')) {
        return strip_quotes(trimmed);
    }

    bool is_numeric = true;
    bool has_decimal = false;
    size_t start = 0;
    if (trimmed[0] == '+' || trimmed[0] == '-') {
        start = 1;
    }
    for (size_t i = start; i < trimmed.size(); ++i) {
        if (trimmed[i] == '.') {
            has_decimal = true;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
            is_numeric = false;
            break;
        }
    }

    if (is_numeric) {
        try {
            if (has_decimal) {
                return std::stod(trimmed);
            }
            return std::stoi(trimmed);
        } catch (...) {
        }
    }

    return strip_quotes(trimmed);
}

Condition parse_condition(const std::string& raw_condition) {
    static constexpr std::array<const char*, 7> kOperators = {
        "<=", ">=", "!=", "<>", "=", "<", ">"
    };

    Condition condition;
    const std::string trimmed = trim(raw_condition);
    for (const char* op : kOperators) {
        const std::string_view token(op);
        const auto pos = trimmed.find(token);
        if (pos == std::string::npos) {
            continue;
        }

        condition.column = trim(trimmed.substr(0, pos));
        condition.operation = (token == "<>") ? "!=" : std::string(token);
        std::string rhs = trim(trimmed.substr(pos + token.size()));
        condition.operand = parse_literal(rhs);
        return condition;
    }

    condition.column = trimmed;
    condition.operation.clear();
    condition.operand.reset();
    return condition;
}

std::vector<Condition> parse_conditions(const std::string& clause) {
    std::vector<Condition> conditions;

    // Split on both AND and OR, preserving which operator was used
    std::string remaining = clause;
    std::string upper_clause = to_upper(clause);

    while (!remaining.empty()) {
        std::string current_condition;
        std::string next_operator = "AND";  // default

        // Find positions of next AND and OR
        size_t and_pos = upper_clause.find(" AND ");
        size_t or_pos = upper_clause.find(" OR ");

        // Determine which comes first
        size_t split_pos = std::string::npos;
        if (and_pos != std::string::npos && or_pos != std::string::npos) {
            if (and_pos < or_pos) {
                split_pos = and_pos;
                next_operator = "AND";
            } else {
                split_pos = or_pos;
                next_operator = "OR";
            }
        } else if (and_pos != std::string::npos) {
            split_pos = and_pos;
            next_operator = "AND";
        } else if (or_pos != std::string::npos) {
            split_pos = or_pos;
            next_operator = "OR";
        }

        // Extract current condition
        if (split_pos != std::string::npos) {
            current_condition = remaining.substr(0, split_pos);
            // Update remaining string (skip the operator)
            size_t operator_len = (next_operator == "AND") ? 5 : 4;  
            remaining = remaining.substr(split_pos + operator_len);
            upper_clause = to_upper(remaining);
        } else {
            // Last condition
            current_condition = remaining;
            remaining.clear();
            next_operator = "AND"; 
        }

        // Parse the condition and set its logical operator
        current_condition = trim(current_condition);
        if (!current_condition.empty()) {
            Condition cond = parse_condition(current_condition);
            cond.logical_operator = next_operator;
            conditions.push_back(cond);
        }
    }

    return conditions;
}

void parse_order_by(const std::string& clause, ParsedQuery& pq) {
    std::string trimmed = trim(clause);
    if (trimmed.empty()) {
        return;
    }

    std::istringstream iss(trimmed);
    std::string column;
    std::string direction;
    iss >> column;
    iss >> direction;

    pq.order_by_column = trim(column);
    if (!direction.empty()) {
        pq.order_ascending = (to_upper(direction) != "DESC");
    } else {
        pq.order_ascending = true;
    }
}

void parse_limit(const std::string& clause, ParsedQuery& pq) {
    std::string trimmed = trim(clause);
    if (trimmed.empty()) {
        return;
    }

    try {
        pq.limit_value = std::stoi(trimmed);
    } catch (...) {
        pq.limit_value = -1;
    }
}

}  // namespace mdbms::qo

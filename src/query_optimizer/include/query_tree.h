#pragma once
#include <memory>
#include <string>
#include <vector>

namespace mdbms::qo {

struct QueryTree {
    std::string op;   // SCAN, SELECT, PROJECT, JOIN, CARTESIAN
    std::string info; // detail operasi

    std::vector<std::unique_ptr<QueryTree>> children;

    QueryTree(const std::string& o = "", const std::string& i = "")
        : op(o), info(i) {}

    QueryTree(const QueryTree& other)
        : op(other.op), info(other.info) {
        children.reserve(other.children.size());
        for (const auto& child : other.children) {
            if (child) {
                children.push_back(std::make_unique<QueryTree>(*child));
            }
        }
    }

    QueryTree& operator=(const QueryTree& other) {
        if (this == &other) {
            return *this;
        }
        op = other.op;
        info = other.info;
        children.clear();
        children.reserve(other.children.size());
        for (const auto& child : other.children) {
            if (child) {
                children.push_back(std::make_unique<QueryTree>(*child));
            }
        }
        return *this;
    }

    QueryTree(QueryTree&&) noexcept = default;
    QueryTree& operator=(QueryTree&&) noexcept = default;

    QueryTree* add_child(std::unique_ptr<QueryTree> child) {
        children.push_back(std::move(child));
        return children.back().get();
    }
};

} // namespace mdbms::qo


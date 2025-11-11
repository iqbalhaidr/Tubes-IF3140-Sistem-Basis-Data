#pragma once

#include <memory>
#include <string>
#include "types.h"

namespace mdbms::qo {

using QueryTree = mdbms::QueryTree;
using QueryTreePtr = std::shared_ptr<QueryTree>;

inline QueryTreePtr make_node(std::string type, std::string value = {}) {
    auto node = std::make_shared<QueryTree>();
    node->type = std::move(type);
    node->value = std::move(value);
    return node;
}

inline void append_child(const QueryTreePtr& parent, const QueryTreePtr& child) {
    if (!parent || !child) {
        return;
    }
    child->parent = parent;
    parent->children.push_back(child);
}

} // namespace mdbms::qo

#pragma once

#include <memory>
#include <unordered_set>

#include "duckdb/common/typedefs.hpp"
#include "duckdb/common/constants.hpp"

namespace tud {

struct JoinTree {

    explicit JoinTree(duckdb::idx_t  relid);

    JoinTree(std::unique_ptr<JoinTree> left, std::unique_ptr<JoinTree> right);

    duckdb::idx_t relid;

    std::unique_ptr<JoinTree> left;
    std::unique_ptr<JoinTree> right;

    bool IsLeaf() const;

    std::unordered_set<duckdb::idx_t> CollectRelids() const;

};

} // namespace tud

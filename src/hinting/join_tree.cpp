
#include <memory>

#include "duckdb/common/constants.hpp"

#include "hinting/join_tree.hpp"

namespace tud {

JoinTree::JoinTree(duckdb::idx_t relid) : relid(relid), left(nullptr), right(nullptr) {}

JoinTree::JoinTree(std::unique_ptr<JoinTree> left, std::unique_ptr<JoinTree> right)
    : relid(duckdb::DConstants::INVALID_INDEX), left(std::move(left)), right(std::move(right)) {};

bool JoinTree::IsLeaf() const {
    return relid != duckdb::DConstants::INVALID_INDEX;
}

static void CollectRelidsInternal(const JoinTree &tree, std::unordered_set<duckdb::idx_t> &relids) {
    if (tree.IsLeaf()) {
        relids.insert(tree.relid);
        return;
    }
    CollectRelidsInternal(*tree.left, relids);
    CollectRelidsInternal(*tree.right, relids);
}


std::unordered_set<duckdb::idx_t> JoinTree::CollectRelids() const {
    std::unordered_set<duckdb::idx_t> relids;
    CollectRelidsInternal(*this, relids);
    return relids;
}

} // namespace tud

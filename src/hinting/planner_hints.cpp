#include "hinting/intermediate.hpp"
#include "hinting/planner_hints.hpp"

namespace tud {

void CollectOperatorRelidsInternal(const duckdb::LogicalOperator &op, std::unordered_set<duckdb::idx_t> &relids) {
    if (op.type == duckdb::LogicalOperatorType::LOGICAL_GET) {
        for (const auto &relid : op.GetTableIndex()) {
            relids.insert(relid);
        }
    }

    for (const auto &child : op.children) {
        CollectOperatorRelidsInternal(*child, relids);
    }
}

std::unordered_set<duckdb::idx_t> CollectOperatorRelids(const duckdb::LogicalOperator &op) {
    std::unordered_set<duckdb::idx_t> relids;
    CollectOperatorRelidsInternal(op, relids);
    return relids;
}


JoinOrderHinting::JoinOrderHinting(duckdb::PlanEnumerator &plan_enumerator,
                                   duckdb::QueryGraphManager &graph_manager)
    : plan_enumerator_(plan_enumerator), graph_manager_(graph_manager) {}


duckdb::unique_ptr<duckdb::DPJoinNode> JoinOrderHinting::MakeJoinNode(const JoinTree &jointree) {
    auto &set_manager = graph_manager_.set_manager;

    if (jointree.IsLeaf()) {
        auto &relset = set_manager.GetJoinRelation(jointree.relid);
        return duckdb::make_uniq<duckdb::DPJoinNode>(relset);
    }

    auto left_node = MakeJoinNode(*jointree.left);
    auto right_node = MakeJoinNode(*jointree.right);

    auto &query_graph = graph_manager_.GetQueryGraphEdges();
    auto &connections = query_graph.GetConnections(left_node->set, right_node->set);
    D_ASSERT(!connections.empty());

    auto &relset = set_manager.Union(left_node->set, right_node->set);

    return plan_enumerator_.CreateJoinTree(
        relset, connections, *left_node, *right_node);
}

PlannerHints::PlannerHints(const std::string query) : raw_query_{query}, contains_hint_{false} {
    relmap_ = std::unordered_map<std::string, duckdb::idx_t>();
    operator_hints_ = std::unordered_map<Intermediate, OperatorHint>();
    join_order_hint_ = nullptr;
}

void PlannerHints::RegisterBaseTable(const duckdb::BaseTableRef &ref, duckdb::idx_t relid) {
    std::string table_identifier = ref.table_name;
    relmap_[ref.table_name] = relid;
    if (!ref.alias.empty()) {
        relmap_[ref.alias] = relid;
    }
}

std::unordered_set<duckdb::idx_t> PlannerHints::ResolveRelids(const std::unordered_set<std::string>& relnames) const {
    std::unordered_set<duckdb::idx_t> relids(relnames.size());
    for (const auto& relname : relnames) {
        auto it = relmap_.find(relname);
        if (it != relmap_.end()) {
            relids.insert(it->second);
        } else {
            throw std::runtime_error("Relation name not found: " + relname);
        }
    }
    return relids;
}

void PlannerHints::AddOperatorHint(const std::string &relname, OperatorHint hint) {
    auto relid = relmap_.find(relname);
    if (relid == relmap_.end()) {
        throw std::runtime_error("Relation name not found: " + relname);
    }

    auto intermediate = Intermediate{relid->second};
    operator_hints_[intermediate] = hint;

    contains_hint_ = true;
}

void PlannerHints::AddOperatorHint(const std::unordered_set<std::string>& rels, OperatorHint hint) {
    auto relids = ResolveRelids(rels);
    auto relset = Intermediate(relids);
    operator_hints_[relset] = hint;
    contains_hint_ = true;
}

std::optional<OperatorHint> PlannerHints::GetOperatorHint(const duckdb::LogicalOperator &op) const {
    auto relids = CollectOperatorRelids(op);
    auto intermediate = Intermediate(relids);

    auto ophint = operator_hints_.find(intermediate);
    if (ophint == operator_hints_.end()) {
        return std::nullopt;
    }
    return ophint->second;
}

void PlannerHints::AddCardinalityHint(const std::unordered_set<std::string>& rels, double card) {
    auto relids = ResolveRelids(rels);
    auto relset = Intermediate(relids);
    cardinality_hints_[relset] = card;
    contains_hint_ = true;
}

std::optional<double> PlannerHints::GetCardinalityHint(const duckdb::JoinRelationSet &rels) const {
    std::unordered_set<duckdb::idx_t> relids(rels.count);
    for (duckdb::idx_t i = 0; i < rels.count; ++i) {
        relids.insert(rels.relations[i]);
    }
    auto intermediate = Intermediate(relids);

    auto card_hint = cardinality_hints_.find(intermediate);
    if (card_hint == cardinality_hints_.end()) {
        return std::nullopt;
    }
    return card_hint->second;
}

std::optional<double> PlannerHints::GetCardinalityHint(const duckdb::LogicalGet &op) const {
    auto relids = CollectOperatorRelids(static_cast<const duckdb::LogicalOperator&>(op));
    auto intermediate = Intermediate(relids);

    auto card_hint = cardinality_hints_.find(intermediate);
    if (card_hint == cardinality_hints_.end()) {
        return std::nullopt;
    }
    return card_hint->second;
}


void PlannerHints::AddJoinOrderHint(std::unique_ptr<JoinTree> join_tree) {
    join_order_hint_ = std::move(join_tree);
    contains_hint_ = true;
}

std::optional<JoinTree*> PlannerHints::GetJoinOrderHint() const {
    if (!join_order_hint_) {
        return std::nullopt;
    }
    return join_order_hint_.get();
}

//
// === HintingContext Implementation ===
//

std::unique_ptr<PlannerHints> HintingContext::planner_hints_ = nullptr;

PlannerHints* HintingContext::InitHints(const std::string &query) {
    planner_hints_ = std::make_unique<PlannerHints>(query);
    return planner_hints_.get();
}

PlannerHints* HintingContext::CurrentPlannerHints() {
    if (!planner_hints_) {
        throw std::runtime_error("No planner hints initialized");
    }
    return planner_hints_.get();
}

void HintingContext::ResetHints() {
    planner_hints_.reset();
}


} // namespace tud

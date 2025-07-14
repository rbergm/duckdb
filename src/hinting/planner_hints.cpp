#include "hinting/intermediate.hpp"
#include "hinting/planner_hints.hpp"

namespace tud {

//
// === Global functions ===
//

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


//
// === JoinOrderHinting Implementation ===
//

JoinOrderHinting::JoinOrderHinting(duckdb::PlanEnumerator &plan_enumerator)
    : plan_enumerator_(plan_enumerator) {}


duckdb::JoinRelationSet& JoinOrderHinting::MakeJoinNode(const JoinTree &jointree) {
    auto &graph_manager_ = plan_enumerator_.query_graph_manager;
    auto &set_manager = graph_manager_.set_manager;

    if (jointree.IsLeaf()) {
        auto &relset = set_manager.GetJoinRelation(jointree.relid);
        auto node = duckdb::make_uniq<duckdb::DPJoinNode>(relset);

        plan_enumerator_.plans[relset] = std::move(node);
        return relset;
    }

    auto &left_rels = MakeJoinNode(*jointree.left);
    auto &right_rels = MakeJoinNode(*jointree.right);

    auto &query_graph = graph_manager_.GetQueryGraphEdges();
    auto &connections = query_graph.GetConnections(left_rels, right_rels);
    D_ASSERT(!connections.empty());

    auto &join_rels = set_manager.Union(left_rels, right_rels);
    auto left_plan = plan_enumerator_.plans.find(left_rels);
    auto right_plan = plan_enumerator_.plans.find(right_rels);

    auto joinnode = plan_enumerator_.CreateJoinTree(join_rels, connections, *left_plan->second, *right_plan->second);
    plan_enumerator_.plans[join_rels] = std::move(joinnode);

    return join_rels;
}

//
// === PlannerHints Implementation ===
//

PlannerHints::PlannerHints(const std::string query) : raw_query_{query}, contains_hint_{false} {
    relmap_ = std::unordered_map<std::string, duckdb::idx_t>();
    operator_hints_ = std::unordered_map<Intermediate, OperatorHint>();
    join_order_hint_ = nullptr;
    global_operator_hints_ = {false, true, true, true}; // Default to enabled for all global operator hints, disable UNKNOWN
}

void PlannerHints::RegisterBaseTable(const duckdb::BaseTableRef &ref, duckdb::idx_t relid) {
    std::string table_identifier = ref.table_name;
    relmap_[ref.table_name] = relid;
    if (!ref.alias.empty()) {
        relmap_[ref.alias] = relid;
    }
}

std::optional<duckdb::idx_t> PlannerHints::ResolveRelid(const std::string& relname) const {
    auto relid = relmap_.find(relname);
    if (relid == relmap_.end()) {
        return std::nullopt;
    }
    return relid->second;
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

void PlannerHints::AddGlobalOperatorHint(OperatorHint op, bool enabled) {
    auto index = static_cast<size_t>(op);
    global_operator_hints_[index] = enabled;
    contains_hint_ = true;
}

bool PlannerHints::GetOperatorEnabled(OperatorHint op) const {
    auto index = static_cast<size_t>(op);
    return global_operator_hints_[index];
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

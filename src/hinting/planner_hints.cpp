#include "antlr4-runtime.h"
#include "HintBlockLexer.h"
#include "HintBlockParser.h"
#include "HintBlockBaseListener.h"

// Undefine ANTLR's INVALID_INDEX macro to avoid conflict with DuckDB's DConstants::INVALID_INDEX
#ifdef INVALID_INDEX
#undef INVALID_INDEX
#endif

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

PlannerHints::PlannerHints(const std::string query) : raw_query_{query}, contains_hint_{false} {
    relmap_ = std::unordered_map<std::string, duckdb::idx_t>();
    operator_hints_ = std::unordered_map<Intermediate, OperatorHint>();
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

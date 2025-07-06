
#include "antlr4-runtime.h"
#include "HintBlockLexer.h"
#include "HintBlockParser.h"
#include "HintBlockBaseListener.h"

#include <hinting/planner_hints.hpp>

namespace tud {

Intermediate PlannerHints::AsIntermediate(duckdb::idx_t relid) const {
    Intermediate intermediate;
    intermediate.insert(relid);
    return intermediate;
}

template<typename C>
Intermediate PlannerHints::AsIntermediate(const C& relids) const {
    Intermediate intermediate{relids.begin(), relids.end()};
    return intermediate;
}

void PlannerHints::AddHint(const std::string &relname, OperatorHint hint) {
    auto relid = relmap_.find(relname);
    if (relid == relmap_.end()) {
        throw std::runtime_error("Relation name not found: " + relname);
    }

    auto intermediate = AsIntermediate(relid->second);
    operator_hints_[intermediate] = hint;
}

} // namespace tud

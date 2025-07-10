#pragma once

#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "duckdb/common/typedefs.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/planner/logical_operator.hpp"

#include "hinting/intermediate.hpp"

namespace tud {

enum class OperatorHint {
    UNKNOWN,
    NLJ,
    HASH_JOIN,
    MERGE_JOIN,
};

std::unordered_set<duckdb::idx_t> CollectOperatorRelids(const duckdb::LogicalOperator &op);

class HintParser;

class PlannerHints {
    friend class HintParser;

public:

    explicit PlannerHints(const std::string query);

    // Rule-of-zero: we only rely on STL types so we let the auto-generated functions take over.

    // === Basic hint table management ===

    void RegisterBaseTable(const duckdb::BaseTableRef &ref, duckdb::idx_t relid);

    std::unordered_set<duckdb::idx_t> ResolveRelids(const std::unordered_set<std::string>& relnames) const;

    void ParseHints();

    // === Operator hints ===

    void AddOperatorHint(const std::string &relname, OperatorHint hint);

    void AddOperatorHint(const std::unordered_set<std::string>& rels, OperatorHint hint);

    std::optional<OperatorHint> GetOperatorHint(const duckdb::LogicalOperator &op) const;

    // === Cardinality hints ===

private:
    std::string raw_query_;

    bool contains_hint_;

    std::unordered_map<std::string, duckdb::idx_t> relmap_;

    std::unordered_map<Intermediate, OperatorHint> operator_hints_;

};

class HintingContext {

public:

    static PlannerHints* InitHints(const std::string &query);

    static PlannerHints* CurrentPlannerHints();

    static void ResetHints();

private:

    static std::unique_ptr<PlannerHints> planner_hints_;

};

};

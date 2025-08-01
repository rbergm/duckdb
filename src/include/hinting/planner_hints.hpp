#pragma once

#include <array>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "duckdb/common/typedefs.hpp"
#include "duckdb/optimizer/join_order/join_node.hpp"
#include "duckdb/optimizer/join_order/join_relation.hpp"
#include "duckdb/optimizer/join_order/plan_enumerator.hpp"
#include "duckdb/optimizer/join_order/query_graph_manager.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/planner/operator/logical_get.hpp"

#include "hinting/intermediate.hpp"
#include "hinting/join_tree.hpp"

namespace duckdb {
    class PlanEnumerator;
    class QueryGraphManager;
}

namespace tud {

enum class OperatorHint {
    UNKNOWN = 0,
    NLJ = 1,
    HASH_JOIN = 2,
    MERGE_JOIN = 3,
};

std::unordered_set<duckdb::idx_t> CollectOperatorRelids(const duckdb::LogicalOperator &op);

class JoinOrderHinting {

public:
    JoinOrderHinting(duckdb::PlanEnumerator &plan_enumerator);

    duckdb::JoinRelationSet& MakeJoinNode(const JoinTree &jointree);

private:
    duckdb::PlanEnumerator &plan_enumerator_;

};


class HintParser;

class PlannerHints {
    friend class HintParser;

public:

    explicit PlannerHints(const std::string query);

    // Rule-of-zero: we only rely on STL types so we let the auto-generated functions take over.

    //
    // === Basic hint table management ===
    //

    void RegisterBaseTable(const duckdb::BaseTableRef &ref, duckdb::idx_t relid);

    std::optional<duckdb::idx_t> ResolveRelid(const std::string& relname) const;

    std::unordered_set<duckdb::idx_t> ResolveRelids(const std::unordered_set<std::string>& relnames) const;

    void ParseHints();

    //
    // === Operator hints ===
    //

    void AddOperatorHint(const std::string &relname, OperatorHint hint);

    void AddOperatorHint(const std::unordered_set<std::string>& rels, OperatorHint hint);

    std::optional<OperatorHint> GetOperatorHint(const duckdb::LogicalOperator &op) const;

    //
    // === Cardinality hints ===
    //

    void AddCardinalityHint(const std::unordered_set<std::string>& rels, double card);

    std::optional<double> GetCardinalityHint(const duckdb::JoinRelationSet &rels) const;

    std::optional<double> GetCardinalityHint(const duckdb::LogicalGet &op) const;

    //
    // === Join order hints ===
    //

    void AddJoinOrderHint(std::unique_ptr<JoinTree> join_tree);

    std::optional<JoinTree*> GetJoinOrderHint() const;

    //
    // === Global hints ===
    //

    void AddGlobalOperatorHint(OperatorHint hint, bool enabled);

    bool GetOperatorEnabled(OperatorHint hint) const;

private:
    std::string raw_query_;

    bool contains_hint_;

    std::unordered_map<std::string, duckdb::idx_t> relmap_;

    std::unordered_map<Intermediate, OperatorHint> operator_hints_;

    std::unordered_map<Intermediate, double> cardinality_hints_;

    std::unique_ptr<JoinTree> join_order_hint_;

    std::array<bool, 4> global_operator_hints_; // UNKNOWN, NLJ, HASH_JOIN, MERGE_JOIN

};

class HintingContext {

public:

    static PlannerHints* InitHints(const std::string &query);

    static PlannerHints* CurrentPlannerHints();

    static void ResetHints();

private:

    static std::unique_ptr<PlannerHints> planner_hints_;

};

} // namespace tud

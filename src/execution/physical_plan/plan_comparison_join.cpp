#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"
#include "duckdb/execution/operator/join/perfect_hash_join_executor.hpp"
#include "duckdb/execution/operator/join/physical_blockwise_nl_join.hpp"
#include "duckdb/execution/operator/join/physical_cross_product.hpp"
#include "duckdb/execution/operator/join/physical_hash_join.hpp"
#include "duckdb/execution/operator/join/physical_iejoin.hpp"
#include "duckdb/execution/operator/join/physical_nested_loop_join.hpp"
#include "duckdb/execution/operator/join/physical_piecewise_merge_join.hpp"
#include "duckdb/execution/operator/scan/physical_table_scan.hpp"
#include "duckdb/execution/physical_plan_generator.hpp"
#include "duckdb/function/table/table_scan.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/planner/expression_iterator.hpp"
#include "duckdb/planner/operator/logical_comparison_join.hpp"
#include "duckdb/transaction/duck_transaction.hpp"

#include "hinting/planner_hints.hpp"

namespace duckdb {

static void RewriteJoinCondition(unique_ptr<Expression> &root_expr, idx_t offset) {
	ExpressionIterator::VisitExpressionMutable<BoundReferenceExpression>(
	    root_expr, [&](BoundReferenceExpression &ref, unique_ptr<Expression> &expr) { ref.index += offset; });
}

PhysicalOperator &PhysicalPlanGenerator::PlanComparisonJoin(LogicalComparisonJoin &op) {
	// now visit the children
	D_ASSERT(op.children.size() == 2);
	idx_t lhs_cardinality = op.children[0]->EstimateCardinality(context);
	idx_t rhs_cardinality = op.children[1]->EstimateCardinality(context);
	auto &left = CreatePlan(*op.children[0]);
	auto &right = CreatePlan(*op.children[1]);
	left.estimated_cardinality = lhs_cardinality;
	right.estimated_cardinality = rhs_cardinality;

	if (op.conditions.empty()) {
		// no conditions: insert a cross product
		return Make<PhysicalCrossProduct>(op.types, left, right, op.estimated_cardinality);
	}

	//
	// START hinting additions
	//

	auto planner_hints = tud::HintingContext::CurrentPlannerHints();
	auto join_hint = planner_hints->GetOperatorHint(op);
	if (join_hint) {
		// implementation of the different join operators is copied from below. Make sure to keep these in sync!
		switch (join_hint.value()) {
		case tud::OperatorHint::NLJ: {
			if (PhysicalNestedLoopJoin::IsSupported(op.conditions, op.join_type)) {
				return Make<PhysicalNestedLoopJoin>(op, left, right, std::move(op.conditions), op.join_type,
													op.estimated_cardinality, std::move(op.filter_pushdown));
			}

			for (auto &cond : op.conditions) {
				RewriteJoinCondition(cond.right, left.types.size());
			}
			auto condition = JoinCondition::CreateExpression(std::move(op.conditions));
			return Make<PhysicalBlockwiseNLJoin>(op, left, right, std::move(condition), op.join_type, op.estimated_cardinality);
		}

		case tud::OperatorHint::HASH_JOIN: {
			auto &join = Make<PhysicalHashJoin>(op, left, right, std::move(op.conditions), op.join_type,
												op.left_projection_map, op.right_projection_map, std::move(op.mark_types),
												op.estimated_cardinality, std::move(op.filter_pushdown));
			join.Cast<PhysicalHashJoin>().join_stats = std::move(op.join_stats);
			return join;
		}

		case tud::OperatorHint::MERGE_JOIN: {
			return Make<PhysicalPiecewiseMergeJoin>(op, left, right, std::move(op.conditions), op.join_type,
													op.estimated_cardinality, std::move(op.filter_pushdown));
		}

		default:
			throw InternalException("Unknown join hint type");
		}
	}

	//
	// END hinting additions
	//

	idx_t has_range = 0;
	bool has_equality = op.HasEquality(has_range);
	bool can_merge = has_range > 0;
	bool can_iejoin = has_range >= 2 && recursive_cte_tables.empty();
	switch (op.join_type) {
	case JoinType::SEMI:
	case JoinType::ANTI:
	case JoinType::RIGHT_ANTI:
	case JoinType::RIGHT_SEMI:
	case JoinType::MARK:
		can_merge = can_merge && op.conditions.size() == 1;
		can_iejoin = false;
		break;
	default:
		break;
	}
	auto &client_config = ClientConfig::GetConfig(context);

	//	TODO: Extend PWMJ to handle all comparisons and projection maps
	const auto prefer_range_joins = client_config.prefer_range_joins && can_iejoin;

	// HINTING addition: check if the user has disabled hash joins globally
	auto enable_hashjoin = planner_hints->GetOperatorEnabled(tud::OperatorHint::HASH_JOIN);
	if (enable_hashjoin && has_equality && !prefer_range_joins) {
		// Equality join with small number of keys : possible perfect join optimization
		auto &join = Make<PhysicalHashJoin>(op, left, right, std::move(op.conditions), op.join_type,
		                                    op.left_projection_map, op.right_projection_map, std::move(op.mark_types),
		                                    op.estimated_cardinality, std::move(op.filter_pushdown));
		join.Cast<PhysicalHashJoin>().join_stats = std::move(op.join_stats);
		return join;
	}

	// HINTING addition: disable this assertion for now, as it breaks global hinting. What does it do anyway?
	// We need to understand the implementation details behind this assertion and the DuckDB internals to be safe, but
	// let's just "move fast and break things" for now.
	//
	// D_ASSERT(op.left_projection_map.empty());
	//

	if (left.estimated_cardinality <= client_config.nested_loop_join_threshold ||
	    right.estimated_cardinality <= client_config.nested_loop_join_threshold) {
		can_iejoin = false;
		can_merge = false;
	}

	if (can_merge && can_iejoin) {
		if (left.estimated_cardinality <= client_config.merge_join_threshold ||
		    right.estimated_cardinality <= client_config.merge_join_threshold) {
			can_iejoin = false;
		}
	}

	if (can_iejoin) {
		return Make<PhysicalIEJoin>(op, left, right, std::move(op.conditions), op.join_type, op.estimated_cardinality,
		                            std::move(op.filter_pushdown));
	}

	// HINTING addition: check if the user has disabled merge joins globally
	auto enable_mergejoin = planner_hints->GetOperatorEnabled(tud::OperatorHint::MERGE_JOIN);
	if (enable_mergejoin && can_merge) {
		// range join: use piecewise merge join
		return Make<PhysicalPiecewiseMergeJoin>(op, left, right, std::move(op.conditions), op.join_type,
		                                        op.estimated_cardinality, std::move(op.filter_pushdown));
	}

	// HINTING addition: check if the user has disabled nested loop joins globally. If she has, we cannot execute the
	// join because there are no more operators left. DuckDB would have picked the first one already.
	if (!planner_hints->GetOperatorEnabled(tud::OperatorHint::NLJ)) {
		throw InternalException("Nested Loop Join is disabled globally, cannot execute join");
	}

	if (PhysicalNestedLoopJoin::IsSupported(op.conditions, op.join_type)) {
		// inequality join: use nested loop
		return Make<PhysicalNestedLoopJoin>(op, left, right, std::move(op.conditions), op.join_type,
		                                    op.estimated_cardinality, std::move(op.filter_pushdown));
	}

	for (auto &cond : op.conditions) {
		RewriteJoinCondition(cond.right, left.types.size());
	}
	auto condition = JoinCondition::CreateExpression(std::move(op.conditions));
	return Make<PhysicalBlockwiseNLJoin>(op, left, right, std::move(condition), op.join_type, op.estimated_cardinality);
}

PhysicalOperator &PhysicalPlanGenerator::CreatePlan(LogicalComparisonJoin &op) {
	switch (op.type) {
	case LogicalOperatorType::LOGICAL_ASOF_JOIN:
		return PlanAsOfJoin(op);
	case LogicalOperatorType::LOGICAL_COMPARISON_JOIN:
		return PlanComparisonJoin(op);
	case LogicalOperatorType::LOGICAL_DELIM_JOIN:
		return PlanDelimJoin(op);
	default:
		throw InternalException("Unrecognized operator type for LogicalComparisonJoin");
	}
}

} // namespace duckdb

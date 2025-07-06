
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <duckdb/common/typedefs.hpp>

namespace tud {

enum class OperatorHint {
    UNKNOWN,
    NLJ,
    HASH_JOIN,
    MERGE_JOIN,
};

typedef std::unordered_set<duckdb::idx_t> Intermediate;

class PlannerHints {
public:

    void AddHint(const std::string &relname, OperatorHint hint);

    std::optional<OperatorHint> GetOperatorHint(const std::string &relname) const;

    std::optional<OperatorHint> GetOperatorHint(duckdb::idx_t relid) const;

private:
    std::unordered_map<std::string, duckdb::idx_t> relmap_;

    std::unordered_map<Intermediate, OperatorHint> operator_hints_;

    Intermediate AsIntermediate(duckdb::idx_t relid) const;

    template<typename C>
    Intermediate AsIntermediate(const C& relids) const;

};

extern PlannerHints planner_hints;

extern void ParsePlannerHints(const std::string &query_string);

};

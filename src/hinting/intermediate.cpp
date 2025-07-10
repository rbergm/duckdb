
#include "hinting/intermediate.hpp"

namespace tud {

Intermediate::Intermediate(duckdb::idx_t relid) {
    relations = std::unordered_set<duckdb::idx_t>();
    relations.insert(relid);
}

// template<std::ranges::input_range Container>
//    requires std::same_as<std::ranges::range_value_t<Container>, duckdb::idx_t>
Intermediate::Intermediate(const std::unordered_set<duckdb::idx_t>& relids) {
    relations = std::unordered_set<duckdb::idx_t>{relids.begin(), relids.end()};
}

bool Intermediate::operator==(const Intermediate &other) const {
    return this->relations == other.relations;
}

} // namespace tud

#pragma once

#include <unordered_set>

#include "duckdb/common/typedefs.hpp"

namespace tud {

struct Intermediate {
    std::unordered_set<duckdb::idx_t> relations;

    Intermediate(duckdb::idx_t rel);

    Intermediate(const std::unordered_set<duckdb::idx_t>& relations);

    bool operator==(const Intermediate &other) const;
};

} // namespace tud

template<>
struct std::hash<tud::Intermediate> {
    std::size_t operator()(const tud::Intermediate& intermediate) const {
        std::size_t seed = 0;
        for (auto& id : intermediate.relations) {
            seed ^= std::hash<duckdb::idx_t>()(id) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

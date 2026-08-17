#pragma once

#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// First Fit: for each pending VM, in YAML order, place it on the first host
// in lexicographic host_id order that satisfies its CPU and RAM demand.
class FirstFit final : public PlacementAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "first_fit"; }
    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;
};

}  // namespace algosim::algorithms

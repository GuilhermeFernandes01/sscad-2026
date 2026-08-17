#pragma once

#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// First Fit Decreasing: sort pending VMs by cpu_demand_mips descending,
// then apply First Fit. Classic bin-packing heuristic; tends to use fewer
// hosts than plain First Fit by placing large items first.
class FFD final : public PlacementAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "ffd"; }
    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;
};

}  // namespace algosim::algorithms

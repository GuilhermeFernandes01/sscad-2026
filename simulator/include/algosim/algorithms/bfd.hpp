#pragma once

#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// Best Fit Decreasing: sort pending VMs by cpu_demand_mips descending,
// then apply Best Fit. The tightest bin-packing baseline: maximises
// consolidation by placing the largest VMs into the tightest-fitting hosts.
class BFD final : public PlacementAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "bfd"; }
    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;
};

}  // namespace algosim::algorithms

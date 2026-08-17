#pragma once

#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// Lowest Carbon DC First: for each pending VM, select the datacenter with
// the lowest current carbon intensity (`carbon_now`), then apply Best Fit
// within that datacenter. If the best DC is full, fall back to the next
// lowest-carbon DC.
//
// This is the simplest carbon-aware placement heuristic: it routes demand
// towards the greenest location at each decision point, without considering
// future intensity changes or migration costs.
class LowestCarbonDC final : public PlacementAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "lowest_carbon_dc"; }
    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;
};

}  // namespace algosim::algorithms

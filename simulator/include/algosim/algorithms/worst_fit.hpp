#pragma once

#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// Worst Fit: for each pending VM, place it on the host with the *largest*
// free_mips among those that fit. Primarily a baseline: expected to perform
// poorly on energy/carbon metrics because it actively spreads load and
// maximises the number of active hosts.
class WorstFit final : public PlacementAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "worst_fit"; }
    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;
};

}  // namespace algosim::algorithms

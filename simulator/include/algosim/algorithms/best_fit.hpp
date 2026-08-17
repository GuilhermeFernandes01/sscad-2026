#pragma once

#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// Best Fit: for each pending VM, place it on the host with the *smallest*
// free_mips that still satisfies the VM's demand. Ties broken by host_id.
//
// This is the consolidation-oriented baseline: it packs VMs tightly so that
// idle hosts can be powered off, yielding significantly better energy
// efficiency than First Fit on diurnal workloads.
class BestFit final : public PlacementAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "best_fit"; }
    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;
};

}  // namespace algosim::algorithms

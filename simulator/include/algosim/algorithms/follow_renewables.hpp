#pragma once

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// Follow The Renewables: carbon-aware migration heuristic.
//
// Placement: identical to LowestCarbonDC (route new VMs to the greenest DC).
//
// Migration: at each migration tick, identify DCs whose carbon intensity
// just crossed above a configurable threshold. For each VM on a "dirty" DC,
// find the "cleanest" DC that has capacity, and emit a MigrationDecision.
// VMs are migrated greedily, largest-demand first, to the DC with the lowest
// current carbon_now. No VM is migrated more than once per tick.
//
// This approximates the FollowME@S inter-DC strategy from c-nemesis, adapted
// to the algosim interface.
class FollowRenewables final : public PlacementAlgorithm, public MigrationAlgorithm {
public:
    explicit FollowRenewables(double carbon_threshold_gco2 = 400.0)
        : threshold_{carbon_threshold_gco2} {}

    [[nodiscard]] std::string_view name() const override { return "follow_renewables"; }

    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;

    [[nodiscard]] std::vector<domain::MigrationDecision>
        migrate(const domain::ClusterState& state) override;

private:
    double threshold_;
};

}  // namespace algosim::algorithms

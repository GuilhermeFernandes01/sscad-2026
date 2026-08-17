#pragma once

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// FollowMe@S Inter-DC (Ali et al. 2021, adapted).
//
// Placement: identical to LowestCarbonDC; route each pending VM to the
// greenest DC with capacity, Best Fit within. This matches the paper's
// "allocation step" which sorts DCs by green-energy availability.
//
// Migration: consolidation-driven (not carbon-threshold driven like
// FollowRenewables). At each migration tick:
//   1. Identify hosts with `utilization < underutilization_threshold`.
//   2. For each VM on those hosts, in demand-descending order:
//      - Sort DCs by carbon_now ascending.
//      - Migrate to the first DC with capacity that is NOT the current DC
//        (inter-DC semantics), Best Fit within the target DC.
//      - Skip if no better DC exists.
//   3. Respect `max_migrations_per_tick` to avoid network thrashing.
//
// Key difference from FollowRenewables: trigger is *underutilization*, not
// *threshold crossing*. This means migrations happen to consolidate VMs off
// under-loaded hosts even when all DCs are below the carbon threshold.
class FollowMeS final : public PlacementAlgorithm, public MigrationAlgorithm {
public:
    explicit FollowMeS(double underutilization_threshold = 0.20,
                       int    max_migrations_per_tick    = 50)
        : under_threshold_{underutilization_threshold},
          max_migs_{max_migrations_per_tick} {}

    [[nodiscard]] std::string_view name() const override { return "followme_s"; }

    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;

    [[nodiscard]] std::vector<domain::MigrationDecision>
        migrate(const domain::ClusterState& state) override;

private:
    double under_threshold_;
    int    max_migs_;
};

}  // namespace algosim::algorithms

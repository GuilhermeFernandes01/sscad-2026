#pragma once

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// c-NEMESIS (Vasconcelos et al. SMARTGREENS 2022, adapted as a simplified port).
//
// Placement: for each pending VM, minimize the "brown energy increase" proxy
//   brown_est(vm, dc) = vm.cpu_demand_mips * carbon_now[dc]
// Best Fit within the chosen DC.
//
// Migration (the paper's core contribution):
//   1. Sort DCs by carbon_now ascending (greenest first).
//   2. Split into "senders" (carbon > median) and "receivers" (carbon <= median).
//   3. For each (sender, receiver) pair in order, try to migrate VMs from the
//      sender to the receiver. VMs are processed largest-demand first.
//   4. A migration budget (max_concurrent) caps the number of emitted
//      decisions per tick; this approximates the bandwidth constraint from
//      the paper without reimplementing the network model (SimGrid handles
//      link contention natively).
//   5. A benefit constraint prevents thrashing: only migrate if
//      `carbon_now[receiver] * min_benefit_ratio < carbon_now[sender]`.
// Consolidation (second pass):
//   6. For DCs that did NOT participate in inter-DC migrations, consolidate
//      VMs off underutilized hosts onto hosts inside the same DC (Best Fit).
//
// Simplifications vs the paper:
//   - "Expected Remaining Green Energy" replaced by `1 / carbon_now`.
//   - Migration duration estimation (Alg 1) delegated to SimGrid.
//   - Link history tracked implicitly via SimGrid bandwidth simulation.
class CNemesis final : public PlacementAlgorithm, public MigrationAlgorithm {
public:
    explicit CNemesis(int    max_concurrent_migrations = 20,
                      double min_benefit_ratio         = 0.9,
                      double underutilization_threshold = 0.20)
        : max_concurrent_{max_concurrent_migrations},
          min_benefit_ratio_{min_benefit_ratio},
          under_threshold_{underutilization_threshold} {}

    [[nodiscard]] std::string_view name() const override { return "cnemesis"; }

    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;

    [[nodiscard]] std::vector<domain::MigrationDecision>
        migrate(const domain::ClusterState& state) override;

private:
    int    max_concurrent_;
    double min_benefit_ratio_;
    double under_threshold_;
};

}  // namespace algosim::algorithms

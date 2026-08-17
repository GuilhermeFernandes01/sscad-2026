#pragma once

#include "algosim/domain/cluster_state.hpp"
#include "algosim/domain/decisions.hpp"

#include <string_view>
#include <vector>

namespace algosim::algorithms {

// A migration algorithm optionally moves running VMs between hosts.
//
// Contract:
//   - May return an empty vector (no-op).
//   - Each decision must name a VM currently in `state.running_vms`, with a
//     `source_host_id` matching the VM's current placement and a
//     `target_host_id` that is a valid host in the cluster.
//   - Must not mutate `state`.
//   - Must be deterministic given `state` and internal configuration.
class MigrationAlgorithm {
public:
    MigrationAlgorithm() = default;
    MigrationAlgorithm(const MigrationAlgorithm&) = delete;
    MigrationAlgorithm(MigrationAlgorithm&&)      = delete;
    MigrationAlgorithm& operator=(const MigrationAlgorithm&) = delete;
    MigrationAlgorithm& operator=(MigrationAlgorithm&&)      = delete;
    virtual ~MigrationAlgorithm()                            = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::vector<domain::MigrationDecision>
        migrate(const domain::ClusterState& state) = 0;
};

}  // namespace algosim::algorithms

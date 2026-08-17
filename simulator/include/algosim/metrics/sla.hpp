#pragma once

#include "algosim/domain/cluster_state.hpp"

#include <cstddef>

namespace algosim::metrics {

// SLA proxy: number of VM-ticks degraded by CPU overcommit in this snapshot.
//
// A host is overcommitted when the sum of the CPU demand (MIPS) of the VMs
// placed on it exceeds its capacity; every VM on such a host is counted as
// one degraded VM-tick, because SimGrid's fair sharing gives each of them
// less than the MIPS it demands.
//
// This is a demand-not-served proxy, not a contractual SLA model.
[[nodiscard]] std::size_t sla_overcommit_vm_ticks(const domain::ClusterState& state);

}  // namespace algosim::metrics

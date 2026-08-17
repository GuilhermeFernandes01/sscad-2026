#include "algosim/algorithms/first_fit.hpp"

#include "algosim/domain/host.hpp"

namespace algosim::algorithms {

std::vector<domain::PlacementDecision>
FirstFit::place(const domain::ClusterState& state) {
    std::vector<domain::PlacementDecision> out;
    out.reserve(state.pending_vms.size());

    // Local mutable view of remaining capacity per host, so that within a
    // single `place()` call we correctly account for VMs already assigned
    // earlier in this loop. Keyed by host_id for O(1) lookup.
    const auto              hosts = state.all_hosts();
    std::vector<double>     remaining_mips;
    std::vector<int>        remaining_ram;
    remaining_mips.reserve(hosts.size());
    remaining_ram.reserve(hosts.size());
    for (const auto* h : hosts) {
        remaining_mips.push_back(h->free_mips());
        remaining_ram.push_back(h->free_ram_mb());
    }

    for (const auto& vm : state.pending_vms) {
        for (std::size_t i = 0; i < hosts.size(); ++i) {
            const auto* h = hosts[i];
            if (!h->active) {
                continue;
            }
            if (remaining_mips[i] >= vm.cpu_demand_mips
                && remaining_ram[i] >= vm.ram_mb) {
                out.push_back({vm.vm_id, h->host_id, "first_fit"});
                remaining_mips[i] -= vm.cpu_demand_mips;
                remaining_ram[i] -= vm.ram_mb;
                break;
            }
        }
    }
    return out;
}

}  // namespace algosim::algorithms

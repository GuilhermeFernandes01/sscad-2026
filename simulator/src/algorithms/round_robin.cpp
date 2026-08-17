#include "algosim/algorithms/round_robin.hpp"

#include "algosim/domain/host.hpp"

#include <cstddef>

namespace algosim::algorithms {

std::vector<domain::PlacementDecision>
RoundRobin::place(const domain::ClusterState& state) {
    std::vector<domain::PlacementDecision> out;
    out.reserve(state.pending_vms.size());

    const auto hosts = state.all_hosts();
    if (hosts.empty()) {
        return out;
    }

    std::vector<double> remaining_mips;
    std::vector<int>    remaining_ram;
    remaining_mips.reserve(hosts.size());
    remaining_ram.reserve(hosts.size());
    for (const auto* h : hosts) {
        remaining_mips.push_back(h->free_mips());
        remaining_ram.push_back(h->free_ram_mb());
    }

    for (const auto& vm : state.pending_vms) {
        // Probe up to `hosts.size()` positions starting at cursor_.
        std::size_t chosen = hosts.size();
        for (std::size_t probe = 0; probe < hosts.size(); ++probe) {
            const std::size_t idx = (cursor_ + probe) % hosts.size();
            const auto*       h   = hosts[idx];
            if (!h->active) {
                continue;
            }
            if (remaining_mips[idx] >= vm.cpu_demand_mips
                && remaining_ram[idx] >= vm.ram_mb) {
                chosen = idx;
                break;
            }
        }
        if (chosen < hosts.size()) {
            const auto* h = hosts[chosen];
            out.push_back({vm.vm_id, h->host_id, "round_robin"});
            remaining_mips[chosen] -= vm.cpu_demand_mips;
            remaining_ram[chosen] -= vm.ram_mb;
            cursor_ = (chosen + 1) % hosts.size();
        }
    }
    return out;
}

}  // namespace algosim::algorithms

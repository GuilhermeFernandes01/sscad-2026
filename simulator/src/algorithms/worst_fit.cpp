#include "algosim/algorithms/worst_fit.hpp"

#include "algosim/domain/host.hpp"

#include <cstddef>
#include <limits>

namespace algosim::algorithms {

std::vector<domain::PlacementDecision>
WorstFit::place(const domain::ClusterState& state) {
    std::vector<domain::PlacementDecision> out;
    out.reserve(state.pending_vms.size());

    const auto          hosts = state.all_hosts();
    std::vector<double> remaining_mips;
    std::vector<int>    remaining_ram;
    remaining_mips.reserve(hosts.size());
    remaining_ram.reserve(hosts.size());
    for (const auto* h : hosts) {
        remaining_mips.push_back(h->free_mips());
        remaining_ram.push_back(h->free_ram_mb());
    }

    for (const auto& vm : state.pending_vms) {
        std::size_t best_idx   = hosts.size();
        double      max_free   = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < hosts.size(); ++i) {
            const auto* h = hosts[i];
            if (!h->active) {
                continue;
            }
            if (remaining_mips[i] < vm.cpu_demand_mips || remaining_ram[i] < vm.ram_mb) {
                continue;
            }
            if (remaining_mips[i] > max_free) {
                max_free = remaining_mips[i];
                best_idx = i;
            }
        }
        if (best_idx < hosts.size()) {
            const auto* h = hosts[best_idx];
            out.push_back({vm.vm_id, h->host_id, "worst_fit"});
            remaining_mips[best_idx] -= vm.cpu_demand_mips;
            remaining_ram[best_idx] -= vm.ram_mb;
        }
    }
    return out;
}

}  // namespace algosim::algorithms

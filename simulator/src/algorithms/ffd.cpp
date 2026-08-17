#include "algosim/algorithms/ffd.hpp"

#include "algosim/algorithms/tiebreak.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace algosim::algorithms {

std::vector<domain::PlacementDecision>
FFD::place(const domain::ClusterState& state) {
    // Sort VMs by demand descending.
    auto sorted_vms = state.pending_vms;
    // Ordem total explícita (demanda desc, chegada asc, vm_id asc); ver a
    // especificação de desempate em algosim/algorithms/tiebreak.hpp.
    std::sort(sorted_vms.begin(), sorted_vms.end(), tiebreak::vm_demand_desc);

    const auto          hosts = state.all_hosts();
    std::vector<double> remaining_mips;
    std::vector<int>    remaining_ram;
    remaining_mips.reserve(hosts.size());
    remaining_ram.reserve(hosts.size());
    for (const auto* h : hosts) {
        remaining_mips.push_back(h->free_mips());
        remaining_ram.push_back(h->free_ram_mb());
    }

    std::vector<domain::PlacementDecision> out;
    out.reserve(sorted_vms.size());

    for (const auto& vm : sorted_vms) {
        for (std::size_t i = 0; i < hosts.size(); ++i) {
            if (!hosts[i]->active) {
                continue;
            }
            if (remaining_mips[i] >= vm.cpu_demand_mips
                && remaining_ram[i] >= vm.ram_mb) {
                out.push_back({vm.vm_id, hosts[i]->host_id, "ffd"});
                remaining_mips[i] -= vm.cpu_demand_mips;
                remaining_ram[i] -= vm.ram_mb;
                break;
            }
        }
    }
    return out;
}

}  // namespace algosim::algorithms

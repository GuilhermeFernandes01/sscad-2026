#include "algosim/metrics/sla.hpp"

#include <string>
#include <unordered_map>

namespace algosim::metrics {

std::size_t sla_overcommit_vm_ticks(const domain::ClusterState& state) {
    std::unordered_map<std::string, double> demand_by_vm;
    demand_by_vm.reserve(state.running_vms.size());
    for (const auto& vm : state.running_vms) {
        demand_by_vm.emplace(vm.vm_id, vm.cpu_demand_mips);
    }

    std::size_t violations = 0;
    for (const auto& dc : state.datacenters) {
        for (const auto& h : dc.hosts) {
            double      demand = 0.0;
            std::size_t placed = 0;
            for (const auto& vm_id : h.vms) {
                const auto it = demand_by_vm.find(vm_id);
                if (it == demand_by_vm.end()) {
                    continue;
                }
                demand += it->second;
                ++placed;
            }
            if (demand > h.cpu_capacity_mips) {
                violations += placed;
            }
        }
    }
    return violations;
}

}  // namespace algosim::metrics

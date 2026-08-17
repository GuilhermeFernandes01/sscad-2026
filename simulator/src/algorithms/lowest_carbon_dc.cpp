#include "algosim/algorithms/lowest_carbon_dc.hpp"

#include "algosim/algorithms/tiebreak.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace algosim::algorithms {

std::vector<domain::PlacementDecision>
LowestCarbonDC::place(const domain::ClusterState& state) {
    // Rank DCs by ascending carbon_now.
    std::vector<std::pair<double, std::size_t>> dc_order;
    dc_order.reserve(state.datacenters.size());
    for (std::size_t d = 0; d < state.datacenters.size(); ++d) {
        const auto& dc_id = state.datacenters[d].dc_id;
        double carbon = state.carbon_at(dc_id);
        dc_order.emplace_back(carbon, d);
    }
    // Ordem total explícita (carbono asc, dc_id asc); ver a especificação de
    // desempate em algosim/algorithms/tiebreak.hpp.
    tiebreak::sort_dcs_by_carbon(dc_order, state.datacenters);

    // Build a flat host list ordered by (dc_carbon asc, host residual asc):
    // Best Fit within each DC, greenest DC first.
    struct HostSlot {
        std::size_t dc_rank;
        std::size_t host_idx_flat;
    };
    std::vector<const domain::Host*> flat_hosts;
    std::vector<HostSlot>            slots;

    for (std::size_t rank = 0; rank < dc_order.size(); ++rank) {
        const auto& dc = state.datacenters[dc_order[rank].second];
        for (const auto& h : dc.hosts) {
            std::size_t idx = flat_hosts.size();
            flat_hosts.push_back(&h);
            slots.push_back({rank, idx});
        }
    }

    std::vector<double> remaining_mips;
    std::vector<int>    remaining_ram;
    remaining_mips.reserve(flat_hosts.size());
    remaining_ram.reserve(flat_hosts.size());
    for (const auto* h : flat_hosts) {
        remaining_mips.push_back(h->free_mips());
        remaining_ram.push_back(h->free_ram_mb());
    }

    std::vector<domain::PlacementDecision> out;
    out.reserve(state.pending_vms.size());

    for (const auto& vm : state.pending_vms) {
        // Within each DC (by carbon rank), find Best Fit.
        std::size_t chosen = flat_hosts.size();
        double      best_slack = std::numeric_limits<double>::infinity();
        std::size_t best_dc_rank = std::numeric_limits<std::size_t>::max();

        for (std::size_t i = 0; i < flat_hosts.size(); ++i) {
            if (!flat_hosts[i]->active) {
                continue;
            }
            if (remaining_mips[i] < vm.cpu_demand_mips || remaining_ram[i] < vm.ram_mb) {
                continue;
            }
            // Prefer lower-carbon DC; within same DC, prefer tighter fit.
            if (slots[i].dc_rank < best_dc_rank
                || (slots[i].dc_rank == best_dc_rank
                    && (remaining_mips[i] - vm.cpu_demand_mips) < best_slack)) {
                best_dc_rank = slots[i].dc_rank;
                best_slack   = remaining_mips[i] - vm.cpu_demand_mips;
                chosen       = i;
            }
        }
        if (chosen < flat_hosts.size()) {
            out.push_back({vm.vm_id, flat_hosts[chosen]->host_id, "lowest_carbon_dc"});
            remaining_mips[chosen] -= vm.cpu_demand_mips;
            remaining_ram[chosen] -= vm.ram_mb;
        }
    }
    return out;
}

}  // namespace algosim::algorithms

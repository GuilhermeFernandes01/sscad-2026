#include "algosim/algorithms/follow_renewables.hpp"

#include "algosim/algorithms/tiebreak.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace algosim::algorithms {

// Placement: route to greenest DC, Best Fit within.
// (Identical logic to LowestCarbonDC, duplicated to keep each algorithm
// self-contained and independently testable.)
std::vector<domain::PlacementDecision>
FollowRenewables::place(const domain::ClusterState& state) {
    std::vector<std::pair<double, std::size_t>> dc_order;
    dc_order.reserve(state.datacenters.size());
    for (std::size_t d = 0; d < state.datacenters.size(); ++d) {
        dc_order.emplace_back(state.carbon_at(state.datacenters[d].dc_id), d);
    }
    // Ordem total explícita (carbono asc, dc_id asc); ver a especificação de
    // desempate em algosim/algorithms/tiebreak.hpp.
    tiebreak::sort_dcs_by_carbon(dc_order, state.datacenters);

    std::vector<const domain::Host*> flat_hosts;
    struct Slot { std::size_t dc_rank; };
    std::vector<Slot> slots;
    for (std::size_t rank = 0; rank < dc_order.size(); ++rank) {
        for (const auto& h : state.datacenters[dc_order[rank].second].hosts) {
            flat_hosts.push_back(&h);
            slots.push_back({rank});
        }
    }
    std::vector<double> remaining_mips;
    std::vector<int>    remaining_ram;
    for (const auto* h : flat_hosts) {
        remaining_mips.push_back(h->free_mips());
        remaining_ram.push_back(h->free_ram_mb());
    }

    std::vector<domain::PlacementDecision> out;
    for (const auto& vm : state.pending_vms) {
        std::size_t chosen = flat_hosts.size();
        double best_slack = std::numeric_limits<double>::infinity();
        std::size_t best_rank = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < flat_hosts.size(); ++i) {
            if (!flat_hosts[i]->active) {
                continue;
            }
            if (remaining_mips[i] < vm.cpu_demand_mips || remaining_ram[i] < vm.ram_mb) {
                continue;
            }
            if (slots[i].dc_rank < best_rank
                || (slots[i].dc_rank == best_rank
                    && (remaining_mips[i] - vm.cpu_demand_mips) < best_slack)) {
                best_rank  = slots[i].dc_rank;
                best_slack = remaining_mips[i] - vm.cpu_demand_mips;
                chosen     = i;
            }
        }
        if (chosen < flat_hosts.size()) {
            out.push_back({vm.vm_id, flat_hosts[chosen]->host_id, "follow_renewables"});
            remaining_mips[chosen] -= vm.cpu_demand_mips;
            remaining_ram[chosen] -= vm.ram_mb;
        }
    }
    return out;
}

// Migration: move VMs away from DCs above carbon threshold towards cleaner DCs.
std::vector<domain::MigrationDecision>
FollowRenewables::migrate(const domain::ClusterState& state) {
    // Classify DCs into dirty (above threshold) and clean (below).
    std::vector<std::string> dirty_dcs;
    std::vector<std::pair<double, std::string>> clean_dcs;
    for (const auto& dc : state.datacenters) {
        double intensity = state.carbon_at(dc.dc_id);
        if (intensity > threshold_) {
            dirty_dcs.push_back(dc.dc_id);
        } else {
            clean_dcs.emplace_back(intensity, dc.dc_id);
        }
    }
    if (dirty_dcs.empty() || clean_dcs.empty()) {
        return {};
    }
    // `clean_dcs` guarda pares (intensidade, dc_id), de modo que o `operator<`
    // do std::pair já é a ordem total especificada em tiebreak.hpp: (carbono
    // asc, dc_id asc). Mantido explícito para não ser "uniformizado" para a
    // variante indexada, que desempataria pela ordem do YAML.
    std::sort(clean_dcs.begin(), clean_dcs.end());

    // Collect VMs on dirty DCs, sorted by demand descending.
    //
    // ATENÇÃO: a sequência de entrada aqui é `state.running_vms`, preenchida no
    // backend iterando um `std::unordered_map` (simgrid_backend.cpp). Sua ordem
    // não é definida pela política do algoritmo. A ordem total abaixo torna o
    // resultado independente dessa sequência.
    struct VmOnDirty {
        std::string vm_id;
        std::string source_host_id;
        std::string source_dc_id;
        double      demand;
        double      arrival;
        int         ram;
    };
    std::vector<VmOnDirty> candidates;
    for (const auto& vm : state.running_vms) {
        if (!vm.host_id) {
            continue;
        }
        for (const auto& dirty_dc : dirty_dcs) {
            const auto& dc = state.dc(dirty_dc);
            for (const auto& h : dc.hosts) {
                if (h.host_id == *vm.host_id) {
                    candidates.push_back({vm.vm_id, *vm.host_id, dirty_dc,
                                          vm.cpu_demand_mips, vm.arrival_time_s,
                                          vm.ram_mb});
                    goto next_vm;
                }
            }
        }
        next_vm:;
    }
    // Ordem total explícita (demanda desc, chegada asc, vm_id asc); ver a
    // especificação de desempate em algosim/algorithms/tiebreak.hpp.
    std::sort(candidates.begin(), candidates.end(),
              [](const VmOnDirty& a, const VmOnDirty& b) {
                  return tiebreak::demand_desc(a.demand, a.arrival, a.vm_id,
                                               b.demand, b.arrival, b.vm_id);
              });

    // Build remaining-capacity view of clean DC hosts.
    struct TargetHost {
        std::string host_id;
        std::string dc_id;
        double      free_mips;
        int         free_ram;
        std::size_t clean_rank;
    };
    std::vector<TargetHost> targets;
    for (std::size_t rank = 0; rank < clean_dcs.size(); ++rank) {
        const auto& dc = state.dc(clean_dcs[rank].second);
        for (const auto& h : dc.hosts) {
            if (!h.active) {
                continue;
            }
            targets.push_back({h.host_id, dc.dc_id, h.free_mips(),
                               h.free_ram_mb(), rank});
        }
    }

    std::vector<domain::MigrationDecision> out;
    for (const auto& cand : candidates) {
        std::size_t best = targets.size();
        double      best_slack = std::numeric_limits<double>::infinity();
        std::size_t best_rank  = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].free_mips < cand.demand || targets[i].free_ram < cand.ram) {
                continue;
            }
            if (targets[i].clean_rank < best_rank
                || (targets[i].clean_rank == best_rank
                    && (targets[i].free_mips - cand.demand) < best_slack)) {
                best_rank  = targets[i].clean_rank;
                best_slack = targets[i].free_mips - cand.demand;
                best       = i;
            }
        }
        if (best < targets.size()) {
            out.push_back({cand.vm_id, cand.source_host_id,
                           targets[best].host_id, "follow_renewables"});
            targets[best].free_mips -= cand.demand;
            targets[best].free_ram  -= cand.ram;
        }
    }
    return out;
}

}  // namespace algosim::algorithms

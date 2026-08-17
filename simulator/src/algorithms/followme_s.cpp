#include "algosim/algorithms/followme_s.hpp"

#include "algosim/algorithms/tiebreak.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace algosim::algorithms {

// Placement: greenest DC first, Best Fit within.
// Duplicated from LowestCarbonDC rather than delegated so each algorithm is
// self-contained and testable in isolation.
std::vector<domain::PlacementDecision>
FollowMeS::place(const domain::ClusterState& state) {
    std::vector<std::pair<double, std::size_t>> dc_order;
    dc_order.reserve(state.datacenters.size());
    for (std::size_t d = 0; d < state.datacenters.size(); ++d) {
        dc_order.emplace_back(state.carbon_at(state.datacenters[d].dc_id), d);
    }
    // Ordem total explícita (carbono asc, dc_id asc); ver a especificação de
    // desempate em algosim/algorithms/tiebreak.hpp.
    tiebreak::sort_dcs_by_carbon(dc_order, state.datacenters);

    std::vector<const domain::Host*> flat_hosts;
    std::vector<std::size_t>         host_dc_rank;
    for (std::size_t rank = 0; rank < dc_order.size(); ++rank) {
        for (const auto& h : state.datacenters[dc_order[rank].second].hosts) {
            flat_hosts.push_back(&h);
            host_dc_rank.push_back(rank);
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
        std::size_t chosen = flat_hosts.size();
        double      best_slack = std::numeric_limits<double>::infinity();
        std::size_t best_rank  = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < flat_hosts.size(); ++i) {
            if (!flat_hosts[i]->active) {
                continue;
            }
            if (remaining_mips[i] < vm.cpu_demand_mips || remaining_ram[i] < vm.ram_mb) {
                continue;
            }
            const double slack = remaining_mips[i] - vm.cpu_demand_mips;
            if (host_dc_rank[i] < best_rank
                || (host_dc_rank[i] == best_rank && slack < best_slack)) {
                best_rank  = host_dc_rank[i];
                best_slack = slack;
                chosen     = i;
            }
        }
        if (chosen < flat_hosts.size()) {
            out.push_back({vm.vm_id, flat_hosts[chosen]->host_id, "followme_s"});
            remaining_mips[chosen] -= vm.cpu_demand_mips;
            remaining_ram[chosen]  -= vm.ram_mb;
        }
    }
    return out;
}

// Migration: consolidate underutilized hosts, route to greenest DC.
std::vector<domain::MigrationDecision>
FollowMeS::migrate(const domain::ClusterState& state) {
    std::vector<domain::MigrationDecision> out;
    if (max_migs_ <= 0) {
        return out;
    }

    // Collect VMs sitting on underutilized hosts.
    struct Candidate {
        std::string vm_id;
        std::string source_host_id;
        std::string source_dc_id;
        double      demand;
        double      arrival;
        int         ram;
    };
    std::vector<Candidate> candidates;
    for (const auto& dc : state.datacenters) {
        for (const auto& h : dc.hosts) {
            if (h.vms.empty() || h.utilization() >= under_threshold_) {
                continue;
            }
            for (const auto& vm_id : h.vms) {
                for (const auto& vm : state.running_vms) {
                    if (vm.vm_id == vm_id) {
                        candidates.push_back({
                            vm.vm_id, h.host_id, dc.dc_id,
                            vm.cpu_demand_mips, vm.arrival_time_s, vm.ram_mb,
                        });
                        break;
                    }
                }
            }
        }
    }
    if (candidates.empty()) {
        return out;
    }
    // Ordem total explícita (demanda desc, chegada asc, vm_id asc); ver a
    // especificação de desempate em algosim/algorithms/tiebreak.hpp.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return tiebreak::demand_desc(a.demand, a.arrival, a.vm_id,
                                               b.demand, b.arrival, b.vm_id);
              });

    // Build target view: all hosts grouped by DC, ordered by carbon ascending.
    std::vector<std::pair<double, std::size_t>> dc_order;
    dc_order.reserve(state.datacenters.size());
    for (std::size_t d = 0; d < state.datacenters.size(); ++d) {
        dc_order.emplace_back(state.carbon_at(state.datacenters[d].dc_id), d);
    }
    // Ordem total explícita (carbono asc, dc_id asc); ver a especificação de
    // desempate em algosim/algorithms/tiebreak.hpp.
    tiebreak::sort_dcs_by_carbon(dc_order, state.datacenters);

    struct Target {
        std::string host_id;
        std::string dc_id;
        double      free_mips;
        int         free_ram;
        std::size_t dc_rank;
    };
    std::vector<Target> targets;
    for (std::size_t rank = 0; rank < dc_order.size(); ++rank) {
        const auto& dc = state.datacenters[dc_order[rank].second];
        for (const auto& h : dc.hosts) {
            targets.push_back({h.host_id, dc.dc_id, h.free_mips(), h.free_ram_mb(), rank});
        }
    }

    int emitted = 0;
    for (const auto& cand : candidates) {
        if (emitted >= max_migs_) {
            break;
        }
        // Find best target in the cleanest DC that has capacity AND is NOT
        // the same host (otherwise the migration is a no-op).
        std::size_t best = targets.size();
        double      best_slack = std::numeric_limits<double>::infinity();
        std::size_t best_rank  = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].host_id == cand.source_host_id) {
                continue;
            }
            if (targets[i].free_mips < cand.demand || targets[i].free_ram < cand.ram) {
                continue;
            }
            const double slack = targets[i].free_mips - cand.demand;
            if (targets[i].dc_rank < best_rank
                || (targets[i].dc_rank == best_rank && slack < best_slack)) {
                best_rank  = targets[i].dc_rank;
                best_slack = slack;
                best       = i;
            }
        }
        if (best < targets.size()) {
            out.push_back({cand.vm_id, cand.source_host_id,
                           targets[best].host_id, "followme_s"});
            targets[best].free_mips -= cand.demand;
            targets[best].free_ram  -= cand.ram;
            ++emitted;
        }
    }
    return out;
}

}  // namespace algosim::algorithms

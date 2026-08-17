#include "algosim/algorithms/cnemesis.hpp"

#include "algosim/algorithms/tiebreak.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace algosim::algorithms {

namespace {

// Best Fit inside a DC slice.
[[nodiscard]] std::size_t
best_fit_in_dc(const std::vector<const domain::Host*>& flat_hosts,
               const std::vector<double>& remaining_mips,
               const std::vector<int>&    remaining_ram,
               std::size_t                dc_begin,
               std::size_t                dc_end,
               double                     demand_mips,
               int                        demand_ram) {
    std::size_t best       = flat_hosts.size();
    double      best_slack = std::numeric_limits<double>::infinity();
    for (std::size_t i = dc_begin; i < dc_end; ++i) {
        if (!flat_hosts[i]->active) {
            continue;
        }
        if (remaining_mips[i] < demand_mips || remaining_ram[i] < demand_ram) {
            continue;
        }
        const double slack = remaining_mips[i] - demand_mips;
        if (slack < best_slack) {
            best_slack = slack;
            best       = i;
        }
    }
    return best;
}

}  // namespace

// ---------------------------------------------------------------------------
// Placement: minimize brown_energy proxy then Best Fit within the chosen DC.
// ---------------------------------------------------------------------------
std::vector<domain::PlacementDecision>
CNemesis::place(const domain::ClusterState& state) {
    std::vector<domain::PlacementDecision> out;
    if (state.datacenters.empty()) {
        return out;
    }
    out.reserve(state.pending_vms.size());

    // Flat host list + DC boundaries.
    std::vector<const domain::Host*> flat_hosts;
    std::vector<std::size_t>         dc_begin;
    std::vector<std::size_t>         dc_end;
    dc_begin.reserve(state.datacenters.size());
    dc_end.reserve(state.datacenters.size());
    for (const auto& dc : state.datacenters) {
        dc_begin.push_back(flat_hosts.size());
        for (const auto& h : dc.hosts) {
            flat_hosts.push_back(&h);
        }
        dc_end.push_back(flat_hosts.size());
    }

    std::vector<double> remaining_mips;
    std::vector<int>    remaining_ram;
    remaining_mips.reserve(flat_hosts.size());
    remaining_ram.reserve(flat_hosts.size());
    for (const auto* h : flat_hosts) {
        remaining_mips.push_back(h->free_mips());
        remaining_ram.push_back(h->free_ram_mb());
    }

    // Cache carbon_now per DC.
    std::vector<double> dc_carbon;
    dc_carbon.reserve(state.datacenters.size());
    for (const auto& dc : state.datacenters) {
        dc_carbon.push_back(state.carbon_avg(dc.dc_id));
    }

    for (const auto& vm : state.pending_vms) {
        // Try DCs in ascending brown-energy order: min(cpu_demand * carbon).
        // Since cpu_demand is constant per VM, ordering is just by carbon asc.
        std::vector<std::size_t> dc_order(state.datacenters.size());
        for (std::size_t i = 0; i < dc_order.size(); ++i) {
            dc_order[i] = i;
        }
        // Ordem total explícita (intensidade asc, dc_id asc); ver a
        // especificação de desempate em algosim/algorithms/tiebreak.hpp. Sob
        // perfis espacialmente uniformes (flat) TODOS os DCs empatam na chave
        // principal, e o dc_id é quem decide.
        std::sort(dc_order.begin(), dc_order.end(),
                  [&](std::size_t a, std::size_t b) {
                      return tiebreak::carbon_asc(dc_carbon[a],
                                                  state.datacenters[a].dc_id,
                                                  dc_carbon[b],
                                                  state.datacenters[b].dc_id);
                  });

        std::size_t chosen = flat_hosts.size();
        for (const auto d : dc_order) {
            chosen = best_fit_in_dc(flat_hosts, remaining_mips, remaining_ram,
                                     dc_begin[d], dc_end[d],
                                     vm.cpu_demand_mips, vm.ram_mb);
            if (chosen < flat_hosts.size()) {
                break;
            }
        }
        if (chosen < flat_hosts.size()) {
            out.push_back({vm.vm_id, flat_hosts[chosen]->host_id, "cnemesis"});
            remaining_mips[chosen] -= vm.cpu_demand_mips;
            remaining_ram[chosen]  -= vm.ram_mb;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Migration: sender→receiver inter-DC + intra-DC consolidation for idle DCs.
// ---------------------------------------------------------------------------
std::vector<domain::MigrationDecision>
CNemesis::migrate(const domain::ClusterState& state) {
    std::vector<domain::MigrationDecision> out;
    if (max_concurrent_ <= 0 || state.datacenters.size() < 2) {
        return out;
    }

    // --- Rank DCs by carbon_now; split into senders/receivers on the median.
    std::vector<std::pair<double, std::size_t>> dc_by_carbon;
    dc_by_carbon.reserve(state.datacenters.size());
    for (std::size_t d = 0; d < state.datacenters.size(); ++d) {
        dc_by_carbon.emplace_back(state.carbon_avg(state.datacenters[d].dc_id), d);
    }
    // Ordem total explícita (carbono asc, dc_id asc); ver a especificação de
    // desempate em algosim/algorithms/tiebreak.hpp. Aqui a ordem decide também
    // o corte na mediana entre DCs emissores e receptores.
    tiebreak::sort_dcs_by_carbon(dc_by_carbon, state.datacenters);
    const std::size_t median_idx = dc_by_carbon.size() / 2;
    const double      median_c   = dc_by_carbon[median_idx].first;

    std::vector<std::size_t> senders;
    std::vector<std::size_t> receivers;
    for (const auto& [carbon, d] : dc_by_carbon) {
        if (carbon > median_c) {
            senders.push_back(d);
        } else {
            receivers.push_back(d);
        }
    }

    // --- Build mutable target view of all receiver DCs' hosts.
    struct Target {
        std::string host_id;
        std::string dc_id;
        std::size_t dc_idx;
        double      free_mips;
        int         free_ram;
    };
    std::vector<Target> targets;
    for (const auto rd : receivers) {
        const auto& dc = state.datacenters[rd];
        for (const auto& h : dc.hosts) {
            targets.push_back({h.host_id, dc.dc_id, rd, h.free_mips(), h.free_ram_mb()});
        }
    }

    // --- Collect sender VMs (largest demand first).
    struct Candidate {
        std::string vm_id;
        std::string source_host_id;
        std::string source_dc_id;
        std::size_t source_dc_idx;
        double      demand;
        double      arrival;
        int         ram;
    };
    std::vector<Candidate> candidates;
    for (const auto sd : senders) {
        const auto& dc = state.datacenters[sd];
        for (const auto& h : dc.hosts) {
            for (const auto& vm_id : h.vms) {
                for (const auto& vm : state.running_vms) {
                    if (vm.vm_id == vm_id) {
                        candidates.push_back({
                            vm.vm_id, h.host_id, dc.dc_id, sd,
                            vm.cpu_demand_mips, vm.arrival_time_s, vm.ram_mb,
                        });
                        break;
                    }
                }
            }
        }
    }
    // Ordem total explícita (demanda desc, chegada asc, vm_id asc); ver a
    // especificação de desempate em algosim/algorithms/tiebreak.hpp.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return tiebreak::demand_desc(a.demand, a.arrival, a.vm_id,
                                               b.demand, b.arrival, b.vm_id);
              });

    // --- Cache DC carbon for benefit check.
    std::vector<double> dc_carbon;
    dc_carbon.reserve(state.datacenters.size());
    for (const auto& dc : state.datacenters) {
        dc_carbon.push_back(state.carbon_avg(dc.dc_id));
    }

    // --- Greedy assignment within the global migration budget.
    std::unordered_set<std::size_t> dcs_with_inter_mig;
    int emitted = 0;
    for (const auto& cand : candidates) {
        if (emitted >= max_concurrent_) {
            break;
        }
        std::size_t best       = targets.size();
        double      best_slack = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].free_mips < cand.demand || targets[i].free_ram < cand.ram) {
                continue;
            }
            // Benefit constraint.
            if (dc_carbon[targets[i].dc_idx] * min_benefit_ratio_
                >= dc_carbon[cand.source_dc_idx]) {
                continue;
            }
            const double slack = targets[i].free_mips - cand.demand;
            if (slack < best_slack) {
                best_slack = slack;
                best       = i;
            }
        }
        if (best < targets.size()) {
            out.push_back({cand.vm_id, cand.source_host_id,
                           targets[best].host_id, "cnemesis"});
            targets[best].free_mips -= cand.demand;
            targets[best].free_ram  -= cand.ram;
            dcs_with_inter_mig.insert(cand.source_dc_idx);
            dcs_with_inter_mig.insert(targets[best].dc_idx);
            ++emitted;
        }
    }

    // --- Consolidation pass: for DCs NOT in dcs_with_inter_mig, move VMs
    //     from underutilized hosts onto tighter-fit hosts in the same DC.
    for (std::size_t d = 0; d < state.datacenters.size(); ++d) {
        if (dcs_with_inter_mig.count(d) > 0) {
            continue;
        }
        if (emitted >= max_concurrent_) {
            break;
        }
        const auto& dc = state.datacenters[d];

        // Per-host mutable slack state for this DC.
        std::vector<const domain::Host*> dc_hosts;
        std::vector<double>              dc_remaining_mips;
        std::vector<int>                 dc_remaining_ram;
        for (const auto& h : dc.hosts) {
            dc_hosts.push_back(&h);
            dc_remaining_mips.push_back(h.free_mips());
            dc_remaining_ram.push_back(h.free_ram_mb());
        }

        // Collect VMs on underutilized hosts, demand-desc.
        struct LocalCand {
            std::string vm_id;
            std::string src_host;
            std::size_t src_idx;
            double      demand;
            double      arrival;
            int         ram;
        };
        std::vector<LocalCand> locals;
        for (std::size_t hi = 0; hi < dc_hosts.size(); ++hi) {
            const auto& h = *dc_hosts[hi];
            if (h.vms.empty() || h.utilization() >= under_threshold_) {
                continue;
            }
            for (const auto& vm_id : h.vms) {
                for (const auto& vm : state.running_vms) {
                    if (vm.vm_id == vm_id) {
                        locals.push_back({vm.vm_id, h.host_id, hi,
                                          vm.cpu_demand_mips, vm.arrival_time_s,
                                          vm.ram_mb});
                        break;
                    }
                }
            }
        }
        // Ordem total explícita (demanda desc, chegada asc, vm_id asc); ver a
        // especificação de desempate em algosim/algorithms/tiebreak.hpp.
        std::sort(locals.begin(), locals.end(),
                  [](const LocalCand& a, const LocalCand& b) {
                      return tiebreak::demand_desc(a.demand, a.arrival, a.vm_id,
                                                   b.demand, b.arrival, b.vm_id);
                  });

        for (const auto& lc : locals) {
            if (emitted >= max_concurrent_) {
                break;
            }
            std::size_t best       = dc_hosts.size();
            double      best_slack = std::numeric_limits<double>::infinity();
            for (std::size_t hi = 0; hi < dc_hosts.size(); ++hi) {
                if (hi == lc.src_idx) {
                    continue;
                }
                if (dc_remaining_mips[hi] < lc.demand || dc_remaining_ram[hi] < lc.ram) {
                    continue;
                }
                const double slack = dc_remaining_mips[hi] - lc.demand;
                if (slack < best_slack) {
                    best_slack = slack;
                    best       = hi;
                }
            }
            if (best < dc_hosts.size()) {
                out.push_back({lc.vm_id, lc.src_host,
                               dc_hosts[best]->host_id, "cnemesis-consolidation"});
                dc_remaining_mips[best] -= lc.demand;
                dc_remaining_ram[best]  -= lc.ram;
                ++emitted;
            }
        }
    }
    return out;
}

}  // namespace algosim::algorithms

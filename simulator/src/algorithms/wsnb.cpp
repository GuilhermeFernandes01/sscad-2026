#include "algosim/algorithms/wsnb.hpp"

#include "algosim/algorithms/tiebreak.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace algosim::algorithms {

namespace {

// FNV-1a de 64 bits, especificado no repositório.
//
// A versão anterior usava `std::hash<std::string>`, cuja função é definida
// pela implementação: libstdc++, libc++ e MSVC produzem valores diferentes
// para a mesma string, e nada impede que uma versão futura da libstdc++ mude a
// sua. O DC de origem de cada VM no WSNB, e portanto toda a alocação do
// algoritmo, dependia dessa escolha da biblioteca. Um terceiro que
// recompilasse o experimento com outra biblioteca padrão obteria alocações
// diferentes a partir dos MESMOS artefatos versionados. FNV-1a é aritmética
// inteira explícita: o valor é o mesmo em qualquer compilador e plataforma.
[[nodiscard]] std::uint64_t fnv1a_64(const std::string& s) noexcept {
    std::uint64_t h = 14695981039346656037ULL;
    for (const char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
    return h;
}

// Seletor determinístico do "DC de origem": mapeia o identificador da VM sobre
// os data centers ORDENADOS POR dc_id, não pela ordem de declaração no YAML.
//
// A versão anterior indexava `state.datacenters` diretamente, de modo que
// reordenar a lista de data centers no arquivo de cenário (uma edição sem
// nenhum conteúdo semântico) reatribuía o DC de origem de todas as VMs e
// mudava os resultados do WSNB. A ordem lexicográfica de dc_id é estável e
// independente do arquivo.
//
// ADAPTAÇÃO DE ALGORITMO: a semântica documentada ("hash do identificador da
// VM como proxy determinístico de afinidade de localidade") é preservada; o
// que muda é o mapeamento concreto, que era arbitrário nas duas versões. Por
// ser adaptação, está sujeita ao gate humano da constituição (secao 13, item 2)
// e registrada no relatorio de auditoria da campanha.
[[nodiscard]] std::size_t
home_dc_index(const std::string&                     vm_id,
              const std::vector<domain::Datacenter>& dcs) {
    if (dcs.empty()) {
        return 0;
    }
    std::vector<std::size_t> por_dc_id(dcs.size());
    for (std::size_t i = 0; i < por_dc_id.size(); ++i) {
        por_dc_id[i] = i;
    }
    std::sort(por_dc_id.begin(), por_dc_id.end(),
              [&](std::size_t a, std::size_t b) { return dcs[a].dc_id < dcs[b].dc_id; });

    const auto posicao = static_cast<std::size_t>(fnv1a_64(vm_id) % dcs.size());
    return por_dc_id[posicao];
}

// Best Fit search inside a specific datacenter: returns the flat-host index
// with the smallest residual slack that still satisfies the VM demand.
// `flat_hosts` and `remaining_*` are indexed over the cluster-wide host
// list; `dc_begin`/`dc_end` bound the current DC's slice in that list.
[[nodiscard]] std::size_t
best_fit_in_range(const std::vector<const domain::Host*>& flat_hosts,
                  const std::vector<double>& remaining_mips,
                  const std::vector<int>&    remaining_ram,
                  std::size_t                dc_begin,
                  std::size_t                dc_end,
                  double                     demand_mips,
                  int                        demand_ram) {
    std::size_t best = flat_hosts.size();
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

std::vector<domain::PlacementDecision>
WSNB::place(const domain::ClusterState& state) {
    std::vector<domain::PlacementDecision> out;
    out.reserve(state.pending_vms.size());

    // Build flat host list with DC boundaries.
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

    // Rank DCs by carbon_now ascending, for the fallback step.
    std::vector<std::pair<double, std::size_t>> dc_order;
    dc_order.reserve(state.datacenters.size());
    for (std::size_t d = 0; d < state.datacenters.size(); ++d) {
        dc_order.emplace_back(state.carbon_at(state.datacenters[d].dc_id), d);
    }
    // Ordem total explícita (carbono asc, dc_id asc); ver a especificação de
    // desempate em algosim/algorithms/tiebreak.hpp.
    tiebreak::sort_dcs_by_carbon(dc_order, state.datacenters);

    for (const auto& vm : state.pending_vms) {
        const std::size_t home = home_dc_index(vm.vm_id, state.datacenters);
        std::size_t chosen     = flat_hosts.size();

        // Step 1: try the home DC if it's clean enough.
        const double home_carbon = state.carbon_at(state.datacenters[home].dc_id);
        if (home_carbon <= threshold_) {
            chosen = best_fit_in_range(flat_hosts, remaining_mips, remaining_ram,
                                        dc_begin[home], dc_end[home],
                                        vm.cpu_demand_mips, vm.ram_mb);
        }

        // Step 2: fallback, scan DCs in ascending carbon order for any fit.
        if (chosen >= flat_hosts.size()) {
            for (const auto& [carbon, d] : dc_order) {
                (void)carbon;
                chosen = best_fit_in_range(flat_hosts, remaining_mips, remaining_ram,
                                            dc_begin[d], dc_end[d],
                                            vm.cpu_demand_mips, vm.ram_mb);
                if (chosen < flat_hosts.size()) {
                    break;
                }
            }
        }

        if (chosen < flat_hosts.size()) {
            out.push_back({vm.vm_id, flat_hosts[chosen]->host_id, "wsnb"});
            remaining_mips[chosen] -= vm.cpu_demand_mips;
            remaining_ram[chosen]  -= vm.ram_mb;
        }
    }
    return out;
}

}  // namespace algosim::algorithms

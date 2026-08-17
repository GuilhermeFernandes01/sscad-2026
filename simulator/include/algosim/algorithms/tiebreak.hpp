#pragma once

// =============================================================================
// Política de desempate da campanha experimental.
//
// MOTIVAÇÃO
//   Uma auditoria de reprodutibilidade constatou que sete ordenações dos
//   algoritmos usavam APENAS a chave principal da heurística. A ordem relativa
//   entre elementos de chave equivalente ficava, portanto, a cargo da
//   implementação de `std::sort` e da ordem da sequência de entrada, nenhuma
//   das duas especificada pela política do algoritmo.
//
//   Isso não é ruído nem não determinismo dentro de um mesmo binário: para um
//   dado build e ambiente, `std::sort` é reprodutível. O problema é que a
//   política experimental estava SUBESPECIFICADA, e a ordem escolhida pela
//   biblioteca influenciava decisões posteriores de empacotamento e migração.
//   Não é caso de canto: no trace azure_2020 (n=10.000), 6.406 VMs (64,1%)
//   têm demanda exatamente igual à de outra VM.
//
// ESPECIFICAÇÃO ADOTADA
//   Todo comparador usado para decisão é uma ORDEM TOTAL ESTRITA, encerrada
//   por um identificador único e estável. Duas consequências desejadas:
//     1. `std::sort` e `std::stable_sort` passam a produzir o mesmo resultado:
//        a estabilidade deixa de ser premissa não declarada;
//     2. a ordem da sequência de ENTRADA torna-se irrelevante, o que também
//        neutraliza sequências derivadas de contêineres não ordenados
//        (`ClusterState::running_vms` é preenchido iterando um
//        `std::unordered_map` no backend).
//
//   VMs, "maior demanda primeiro":
//       (1) cpu_demand_mips decrescente   - chave da heurística
//       (2) arrival_time_s crescente      - mais antiga primeiro
//       (3) vm_id crescente               - único por construção; fecha a ordem
//
//   Data centers, "mais limpo primeiro":
//       (1) intensidade de carbono crescente
//       (2) dc_id crescente               - único por construção; fecha a ordem
//
//   A chave (2) dos data centers NÃO é redundante e não pode ser substituída
//   pelo índice de declaração no YAML. Os cenários da campanha declaram os DCs
//   em ordem de fuso horário (canberra, seoul, paris, virginia, dubai,
//   singapore, pune, johannesburg, sp), que não é lexicográfica; sob o perfil
//   de carbono constante TODOS os DCs empatam na chave principal, e as duas
//   regras selecionariam data centers diferentes. O índice de declaração é um
//   acidente de edição do arquivo de configuração: reordenar o YAML mudaria os
//   resultados sem que nada da política mudasse. O dc_id é estável.
//
//   A chave (2) das VMs tem efeito limitado nos traces usados (o azure_2020
//   concentra as 10.000 chegadas em 16 instantes distintos); ela é mantida por
//   ser a regra semanticamente correta, com o vm_id garantindo a totalidade.
//
// FORA DE ESCOPO (ordem já determinística e documentada, sem alteração)
//   A varredura linear de hosts em best fit (`slack < best_slack`, primeiro
//   índice vence) percorre `ClusterState::all_hosts()`, cuja ordem é a do YAML,
//   normalizada em ordem lexicográfica de host_id pelo ScenarioBuilder. A regra
//   em vigor é, portanto, "menor folga; empates pelo menor host_id".
//
// PRÉ-CONDIÇÃO
//   Os comparadores exigem valores finitos. Demanda ou intensidade NaN violaria
//   a ordem estrita fraca exigida por std::sort (comportamento indefinido); o
//   ScenarioBuilder rejeita entradas não finitas na carga do cenário.
// =============================================================================

#include "algosim/domain/vm.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace algosim::algorithms::tiebreak {

// Ordem total estrita: maior demanda primeiro.
[[nodiscard]] inline bool demand_desc(double             demand_a,
                                      double             arrival_a,
                                      const std::string& vm_id_a,
                                      double             demand_b,
                                      double             arrival_b,
                                      const std::string& vm_id_b) noexcept {
    if (demand_a != demand_b) {
        return demand_a > demand_b;
    }
    if (arrival_a != arrival_b) {
        return arrival_a < arrival_b;
    }
    return vm_id_a < vm_id_b;
}

[[nodiscard]] inline bool vm_demand_desc(const domain::VM& a,
                                         const domain::VM& b) noexcept {
    return demand_desc(a.cpu_demand_mips, a.arrival_time_s, a.vm_id,
                       b.cpu_demand_mips, b.arrival_time_s, b.vm_id);
}

// Ordem total estrita: menor intensidade de carbono primeiro.
[[nodiscard]] inline bool carbon_asc(double             carbon_a,
                                     const std::string& dc_id_a,
                                     double             carbon_b,
                                     const std::string& dc_id_b) noexcept {
    if (carbon_a != carbon_b) {
        return carbon_a < carbon_b;
    }
    return dc_id_a < dc_id_b;
}

// Ordena pares (intensidade, índice do data center) pela ordem total acima.
//
// Cinco algoritmos montam esse vetor e o ordenavam com o `operator<` do
// `std::pair`, o que desempatava pelo ÍNDICE DE DECLARAÇÃO no YAML. Esta função
// é o único ponto onde a regra vive, para que não volte a divergir entre
// algoritmos. O parâmetro é template apenas para evitar que este cabeçalho
// dependa de `cluster_state.hpp`.
template <typename Datacenters>
void sort_dcs_by_carbon(std::vector<std::pair<double, std::size_t>>& dcs,
                        const Datacenters&                          datacenters) {
    std::sort(dcs.begin(), dcs.end(), [&](const auto& a, const auto& b) {
        return carbon_asc(a.first, datacenters[a.second].dc_id,
                          b.first, datacenters[b.second].dc_id);
    });
}

}  // namespace algosim::algorithms::tiebreak

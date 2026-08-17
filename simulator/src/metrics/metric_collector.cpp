#include "algosim/metrics/metric_collector.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace algosim::metrics {

namespace {

[[nodiscard]] double pue_for(const scenario::ScenarioSpec& spec, const std::string& dc_id) {
    for (const auto& dc : spec.datacenters) {
        if (dc.dc_id == dc_id) {
            return dc.pue;
        }
    }
    return 1.0;
}

// Soma ponderada pelo PUE em ordem lexicográfica de dc_id.
//
// A ordem importa: a adição em ponto flutuante não é associativa, e os mapas
// de origem são `std::unordered_map`, cuja ordem de iteração depende da
// implementação da biblioteca padrão e do histórico de inserções. Sem esta
// normalização, `total_kwh` e `total_gco2` (as métricas primárias do trabalho)
// só seriam reproduzíveis bit a bit no mesmo par (biblioteca, sequência de
// inserção). A diferença é da ordem do épsilon da máquina, mas invalidaria a
// comparação byte a byte usada nas auditorias de determinismo.
[[nodiscard]] double
soma_por_dc(const scenario::ScenarioSpec&                   spec,
            const std::unordered_map<std::string, double>&  por_dc) {
    std::vector<const std::pair<const std::string, double>*> ordenado;
    ordenado.reserve(por_dc.size());
    for (const auto& entrada : por_dc) {
        ordenado.push_back(&entrada);
    }
    std::sort(ordenado.begin(), ordenado.end(),
              [](const auto* a, const auto* b) { return a->first < b->first; });

    double total = 0.0;
    for (const auto* entrada : ordenado) {
        total += pue_for(spec, entrada->first) * entrada->second;
    }
    return total;
}

}  // namespace

void MetricCollector::record(const domain::ClusterState& state,
                             const BackendMetrics&       raw,
                             long long                   algo_wall_us) {
    // Apply PUE per-DC while aggregating totals, em ordem determinística.
    const double total_kwh_eff  = soma_por_dc(spec_, raw.kwh_by_dc);
    const double total_gco2_eff = soma_por_dc(spec_, raw.gco2_by_dc);

    // Utilization statistics: a host counts as "active" (powered on) when
    // it has non-zero CPU work or hosts at least one VM. This is a proxy for
    // SimGrid's is_on() state since `h.active` now means "available for
    // scheduling" (always true), not "powered on".
    std::vector<double> utils;
    std::size_t         active = 0;
    for (const auto& dc : state.datacenters) {
        for (const auto& h : dc.hosts) {
            const bool powered_on = h.cpu_used_mips > 0.0 || !h.vms.empty();
            if (powered_on) {
                ++active;
                utils.push_back(h.utilization());
            }
        }
    }
    double mean_u = 0.0;
    double p95_u  = 0.0;
    if (!utils.empty()) {
        for (const auto u : utils) {
            mean_u += u;
        }
        mean_u /= static_cast<double>(utils.size());
        std::sort(utils.begin(), utils.end());
        const auto idx = static_cast<std::size_t>(0.95 * static_cast<double>(utils.size() - 1));
        p95_u          = utils[idx];
    }

    ticks_.push_back({
        state.t_seconds,
        total_kwh_eff,
        total_gco2_eff,
        active,
        mean_u,
        p95_u,
        raw.migrations_total,
        raw.migrations_bytes_mb,
        raw.sla_violations_tick,
        raw.unplaced_vms_total,
        algo_wall_us,
    });
}

MetricSummary MetricCollector::summary() const noexcept {
    MetricSummary s;
    if (ticks_.empty()) {
        return s;
    }
    const auto& last = ticks_.back();
    s.total_kwh            = last.total_kwh;
    s.total_gco2           = last.total_gco2;
    s.total_migrations     = last.migrations;
    s.total_mig_bytes      = last.mig_bytes;
    s.final_sla_violations = last.sla_violations;
    s.total_unplaced_vms   = last.unplaced_vms;

    double sum_active = 0.0;
    for (const auto& t : ticks_) {
        sum_active += static_cast<double>(t.active_hosts);
        s.algo_wall_total_us += t.algo_wall_us;
    }
    s.mean_active_hosts = sum_active / static_cast<double>(ticks_.size());
    return s;
}

}  // namespace algosim::metrics

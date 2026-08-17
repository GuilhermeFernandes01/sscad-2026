#pragma once

#include "algosim/domain/cluster_state.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace algosim::metrics {

// Raw per-tick metrics reported by the backend (pre-PUE).
// kWh and gCO2 are per-datacenter cumulative integrals.
struct BackendMetrics {
    std::unordered_map<std::string, double> kwh_by_dc;
    std::unordered_map<std::string, double> gco2_by_dc;
    std::size_t                             migrations_total = 0;
    double                                  migrations_bytes_mb = 0.0;
    // Cumulative VM-ticks degraded by CPU overcommit (see metrics/sla.hpp).
    std::size_t                             sla_violations_tick = 0;
    // Cumulative pending VMs that received no placement and were dropped.
    std::size_t                             unplaced_vms_total = 0;
};

// One row of `metrics.csv`, produced once per runner tick.
struct MetricTick {
    double      t_seconds;
    double      total_kwh;
    double      total_gco2;
    std::size_t active_hosts;
    double      mean_util;
    double      p95_util;
    std::size_t migrations;
    double      mig_bytes;
    std::size_t sla_violations;
    std::size_t unplaced_vms;
    long long   algo_wall_us;
};

// Aggregate scalars written to summary.json.
struct MetricSummary {
    double      total_kwh           = 0.0;
    double      total_gco2          = 0.0;
    std::size_t total_migrations    = 0;
    double      total_mig_bytes     = 0.0;
    double      mean_active_hosts   = 0.0;
    std::size_t final_sla_violations = 0;
    std::size_t total_unplaced_vms  = 0;
    long long   algo_wall_total_us  = 0;
};

class MetricCollector {
public:
    explicit MetricCollector(const scenario::ScenarioSpec& spec) : spec_{spec} {}

    void record(const domain::ClusterState& state,
                const BackendMetrics&       raw,
                long long                   algo_wall_us);

    [[nodiscard]] const std::vector<MetricTick>& time_series() const noexcept { return ticks_; }
    [[nodiscard]] MetricSummary                  summary() const noexcept;

private:
    const scenario::ScenarioSpec& spec_;
    std::vector<MetricTick>       ticks_;
};

}  // namespace algosim::metrics

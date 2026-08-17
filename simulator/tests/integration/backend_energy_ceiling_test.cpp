// Integration test: total energy must respect the physical ceiling of the
// platform.
//
// The host_energy plugin caps instantaneous power at power_peak_w, so the
// energy of a physical host can never exceed power_peak_w * duration. The
// defect: the orchestrator called sg_vm_migrate() inline, which BLOCKS the
// calling actor until the live migration completes. Each migration therefore
// pushed the SimGrid clock forward by its full duration while the logical
// tick counter `t` did not move, and the energy plugins kept integrating
// idle/load power over that extra (unaccounted) simulated time. With slow
// WAN links the clock drifted by hours and total_kwh exceeded the ceiling
// by an order of magnitude.
//
// Scenario: 2 DCs x 1 host (100 W peak, PUE 1.0), 1 VM of 8 GiB RAM,
// 100 Mbps inter-DC WAN, and a ping-pong migration policy that bounces the
// VM between the two DCs at every migration interval (60 s). Each 8 GiB
// transfer takes ~700 s of simulated time, so with the defect the clock ends
// at ~47500 s instead of 3600 s and total_kwh ~= 1.3 kWh.
//
// Ceiling: 2 hosts * 100 W * 3600 s = 720 kJ = 0.2 kWh.
//
// The same scenario also pins the gCO2/kWh ratio: both DCs use a flat
// carbon intensity of 475 gCO2/kWh, so total_gco2 / total_kwh must be
// exactly 475 regardless of migrations (the carbon plugin
// re-derives energy from end-of-interval instantaneous power and misses
// update events during migrations, so its footprint drifts from the energy
// plugin's exact integral; the backend now integrates carbon from energy
// deltas itself).

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/registry.hpp"
#include "algosim/backend/simgrid_backend.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace algosim;

namespace {

domain::Host make_host(const std::string& id, const std::string& dc_id) {
    domain::Host h;
    h.host_id           = id;
    h.dc_id             = dc_id;
    h.cpu_cores         = 8;
    h.cpu_capacity_mips = 10000.0;
    h.ram_mb            = 65536;
    h.net_bw_mbps       = 1000.0;
    h.power_idle_w      = 50.0;
    h.power_peak_w      = 100.0;
    return h;
}

scenario::ScenarioSpec make_spec() {
    scenario::ScenarioSpec spec;
    spec.name                       = "energy_ceiling_it";
    spec.duration_seconds           = 3600.0;
    spec.dt_seconds                 = 60.0;
    spec.migration_interval_seconds = 60.0;
    spec.seed                       = 42;
    spec.network.inter_dc_bw_mbps   = 100.0;  // slow WAN: ~700 s per migration

    for (const std::string& dc_id : {std::string{"dca"}, std::string{"dcb"}}) {
        domain::Datacenter dc;
        dc.dc_id            = dc_id;
        dc.name             = dc_id;
        dc.latitude         = dc_id == "dca" ? 0.0 : 10.0;
        dc.longitude        = dc_id == "dca" ? 0.0 : 10.0;
        dc.pue              = 1.0;
        dc.carbon_series_id = dc_id;
        dc.hosts            = {make_host(dc_id + "-h000", dc_id)};
        spec.datacenters.push_back(dc);

        domain::CarbonIntensitySeries series;
        series.series_id    = dc_id;
        series.start        = spec.start_datetime;
        series.step_seconds = 3600;
        series.gco2_per_kwh = {475.0};
        spec.carbon_series.emplace(dc_id, series);
    }

    domain::VM vm;
    vm.vm_id           = "vm-0";
    vm.cpu_cores       = 1;
    vm.cpu_demand_mips = 1000.0;
    vm.ram_mb          = 8192;
    vm.image_size_mb   = 8192;
    vm.arrival_time_s  = 5.0;

    domain::WorkloadEvent ev;
    ev.t_seconds = vm.arrival_time_s;
    ev.kind      = domain::EventKind::Submit;
    ev.vm        = vm;
    spec.events.push_back(ev);
    return spec;
}

// Bounces every running VM to the other DC's host at each invocation.
// Deterministic worst case for migration-induced clock drift.
class PingPongMigration final : public algorithms::MigrationAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "ping_pong"; }

    [[nodiscard]] std::vector<domain::MigrationDecision>
    migrate(const domain::ClusterState& state) override {
        std::vector<domain::MigrationDecision> out;
        for (const auto& dc : state.datacenters) {
            for (const auto& h : dc.hosts) {
                for (const auto& vm_id : h.vms) {
                    const auto target = h.host_id == "dca-h000"
                                            ? std::string{"dcb-h000"}
                                            : std::string{"dca-h000"};
                    out.push_back({vm_id, h.host_id, target, "ping_pong"});
                }
            }
        }
        return out;
    }
};

}  // namespace

TEST_CASE("total energy never exceeds the physical ceiling of the platform",
          "[integration][backend][energy]") {
    algorithms::register_builtin_algorithms();
    const auto spec = make_spec();

    backend::SimGridBackend backend_inst{spec};
    auto placement = algorithms::AlgorithmRegistry::make_placement("first_fit");
    PingPongMigration migration;
    const auto result = backend_inst.run(*placement, &migration);

    // Migrations must actually have happened for this test to be meaningful.
    REQUIRE(result.summary.total_migrations >= 1);

    // Physical ceiling: every host at peak power for the whole duration.
    double ceiling_kwh = 0.0;
    for (const auto& dc : spec.datacenters) {
        for (const auto& h : dc.hosts) {
            ceiling_kwh += dc.pue * h.power_peak_w * spec.duration_seconds / 3.6e6;
        }
    }
    INFO("total_kwh=" << result.summary.total_kwh
                      << " ceiling_kwh=" << ceiling_kwh);
    CHECK(result.summary.total_kwh <= ceiling_kwh * (1.0 + 1e-9));
    CHECK(result.summary.total_kwh > 0.0);

    // Flat 475 gCO2/kWh everywhere: the ratio must be exact even with
    // migrations (carbon must be integrated from energy deltas).
    const double ratio = result.summary.total_gco2 / result.summary.total_kwh;
    INFO("gco2/kwh ratio=" << ratio);
    CHECK(ratio > 475.0 * (1.0 - 1e-9));
    CHECK(ratio < 475.0 * (1.0 + 1e-9));
}

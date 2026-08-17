// Integration test: host CPU accounting seen by algorithms must reflect the
// DEMAND of the VMs placed on the host, not SimGrid's measured host load
// (which is always ~0 when all work runs inside VMs).
//
// Scenario: 1 DC, 2 hosts of 10000 MIPS, 4 permanent VMs of 6000 MIPS
// arriving one per tick, first_fit placement.
//   With demand accounting: vm0 -> h0; vm1 does not fit h0 -> h1;
//   vm2/vm3 fit nowhere -> unplaced. No host is ever overcommitted and the
//   collector observes real utilization.
//   With measured-load accounting (the old defect) every VM lands on h0:
//   sla_violations > 0, unplaced == 0 and mean_util == 0 forever.

#include "algosim/algorithms/registry.hpp"
#include "algosim/backend/simgrid_backend.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace algosim;

namespace {

domain::Host make_host(const std::string& id) {
    domain::Host h;
    h.host_id           = id;
    h.dc_id             = "dc1";
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
    spec.name             = "demand_accounting_it";
    spec.duration_seconds = 300.0;
    spec.dt_seconds       = 60.0;
    spec.seed             = 42;

    domain::Datacenter dc;
    dc.dc_id            = "dc1";
    dc.name             = "DC1";
    dc.pue              = 1.0;
    dc.carbon_series_id = "dc1";
    dc.hosts            = {make_host("dc1-h000"), make_host("dc1-h001")};
    spec.datacenters.push_back(dc);

    domain::CarbonIntensitySeries series;
    series.series_id    = "dc1";
    series.start        = spec.start_datetime;
    series.step_seconds = 3600;
    series.gco2_per_kwh = {100.0};
    spec.carbon_series.emplace("dc1", series);

    for (int i = 0; i < 4; ++i) {
        domain::VM vm;
        vm.vm_id           = "vm-" + std::to_string(i);
        vm.cpu_cores       = 1;
        vm.cpu_demand_mips = 6000.0;
        vm.ram_mb          = 1024;
        vm.image_size_mb   = 1024;
        vm.arrival_time_s  = 5.0 + 60.0 * i;

        domain::WorkloadEvent ev;
        ev.t_seconds = vm.arrival_time_s;
        ev.kind      = domain::EventKind::Submit;
        ev.vm        = vm;
        spec.events.push_back(ev);
    }
    return spec;
}

}  // namespace

TEST_CASE("placement sees demand-based host CPU usage",
          "[integration][backend][demand]") {
    algorithms::register_builtin_algorithms();
    const auto spec = make_spec();

    backend::SimGridBackend backend_inst{spec};
    auto placement = algorithms::AlgorithmRegistry::make_placement("first_fit");
    const auto result = backend_inst.run(*placement, nullptr);

    // 2 hosts x 10000 MIPS hold one 6000-MIPS VM each; the other 2 VMs
    // must be reported as unplaced instead of overcommitting h0.
    CHECK(result.summary.final_sla_violations == 0);
    CHECK(result.summary.total_unplaced_vms == 2);

    // The collector must observe real (demand-based) utilization.
    const bool any_util = std::any_of(
        result.time_series.begin(), result.time_series.end(),
        [](const metrics::MetricTick& t) { return t.mean_util > 0.1; });
    CHECK(any_util);
}

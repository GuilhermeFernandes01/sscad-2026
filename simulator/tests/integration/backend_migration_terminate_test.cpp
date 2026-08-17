// Integration test: terminating a VM while its live migration is completing
// must not crash nor hang the simulation.
//
// The defect: the live_migration plugin's MigrationRx clears is_migrating
// (VirtualMachineImpl::end_migration) BEFORE sending the stage-4 ACK back to
// the issuer of sg_vm_migrate() over mbox_ctl. That ACK is a real
// communication with nonzero simulated latency, so there is a window
// [finalize, ACK-received] in which:
//   - the VM is RUNNING and NOT migrating, and
//   - the detached helper actor is still blocked inside sg_vm_migrate(),
//     holding a raw pointer to the VM, about to call vm->end_migration().
// If a Terminate event destroys the VM inside this window, the plugin's
// shutdown callback does NOT kill the issuer (is_migrating is false), the
// VirtualMachineImpl is freed, and the helper then executes
// vm->end_migration() on freed memory (VmLiveMigration.cpp:368 ->
// VirtualMachineImpl.cpp:413). Undefined behavior: SIGSEGV, or heap
// corruption that leaves engine.run() stuck.
//
// Scenario: 2 DCs x 1 host, huge inter-DC latency (dist 50 deg * 0.5 ms *
// factor 2400 = 60 s raw) so the finalize->ACK window is at least one full
// tick wide, dt = 10 s so every window contains several ticks, and a
// ping-pong migration policy that keeps every VM migrating continuously.
// Twelve VMs are terminated at staggered times: deterministically (fixed
// seed, SimGrid is deterministic) several land inside a finalize window.
// Without the fix this test dies with SIGSEGV; with the fix the backend
// defers the destruction of a finalize-window VM to its helper actor and
// the run completes.

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/registry.hpp"
#include "algosim/backend/simgrid_backend.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <simgrid/s4u/Engine.hpp>
#include <simgrid/s4u/VirtualMachine.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace algosim;

namespace {

constexpr int kNumVms = 12;

domain::Host make_host(const std::string& id, const std::string& dc_id) {
    domain::Host h;
    h.host_id           = id;
    h.dc_id             = dc_id;
    h.cpu_cores         = 32;
    h.cpu_capacity_mips = 100000.0;
    h.ram_mb            = 262144;
    h.net_bw_mbps       = 10000.0;
    h.power_idle_w      = 50.0;
    h.power_peak_w      = 100.0;
    return h;
}

scenario::ScenarioSpec make_spec() {
    scenario::ScenarioSpec spec;
    spec.name                       = "migration_terminate_it";
    spec.duration_seconds           = 3600.0;
    spec.dt_seconds                 = 10.0;
    spec.migration_interval_seconds = 10.0;
    spec.seed                       = 42;
    spec.network.inter_dc_bw_mbps   = 1000.0;
    // dist((0,0),(30,40)) = 50 deg -> 25 ms raw; factor 120 -> 3 s, which
    // the default TCP latency factor (~13x) turns into ~39 s per message.
    // The stage-4 ACK therefore takes several 10 s ticks of simulated time,
    // so every migration exposes a wide finalize window.
    spec.network.inter_dc_latency_factor = 120.0;

    int dc_idx = 0;
    for (const std::string& dc_id : {std::string{"dca"}, std::string{"dcb"}}) {
        domain::Datacenter dc;
        dc.dc_id            = dc_id;
        dc.name             = dc_id;
        dc.latitude         = dc_idx == 0 ? 0.0 : 30.0;
        dc.longitude        = dc_idx == 0 ? 0.0 : 40.0;
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
        ++dc_idx;
    }

    for (int i = 0; i < kNumVms; ++i) {
        domain::VM vm;
        vm.vm_id           = "vm-" + std::to_string(i);
        vm.cpu_cores       = 1;
        vm.cpu_demand_mips = 500.0;
        vm.ram_mb          = 256;
        vm.image_size_mb   = 256;
        vm.arrival_time_s  = 0.0;

        domain::WorkloadEvent submit;
        submit.t_seconds = vm.arrival_time_s;
        submit.kind      = domain::EventKind::Submit;
        submit.vm        = vm;
        spec.events.push_back(submit);

        // vm-0's first migration finalize window is [483.9, 523.0] (measured
        // with the on_migration_end probe below; deterministic: fixed seed,
        // SimGrid 4.1.1): terminating it at t=500 lands inside the window
        // and triggers the UAF without the fix. The remaining VMs are
        // terminated mid-transfer, covering the (already safe) path where
        // the plugin kills the TX/RX/issuer actors.
        domain::WorkloadEvent term;
        term.t_seconds = i == 0 ? 500.0 : 600.0 + 250.0 * i;
        term.kind      = domain::EventKind::Terminate;
        term.vm        = vm;
        spec.events.push_back(term);
    }
    return spec;
}

// Bounces every running VM to the other DC's host at each invocation. The
// backend's in-flight guard turns this into "re-issue as soon as the
// previous migration finished", keeping every VM cycling through
// transfer -> finalize-window -> idle for its whole life.
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

TEST_CASE("terminating VMs during live-migration completion neither crashes "
          "nor hangs the simulation",
          "[integration][backend][migration]") {
    algorithms::register_builtin_algorithms();
    const auto spec = make_spec();

    // Probe: on_migration_end fires twice per completed migration, at the
    // RX finalize (unsafe window opens) and at the issuer's end_migration
    // (window closes). Recording vm-0's fires proves the terminate at t=500
    // really lands inside the finalize window (non-vacuous test).
    static std::vector<double> vm0_mig_end_times;
    vm0_mig_end_times.clear();
    simgrid::s4u::VirtualMachine::on_migration_end_cb(
        [](const simgrid::s4u::VirtualMachine& vm) {
            if (vm.get_name() == "vm-0") {
                vm0_mig_end_times.push_back(simgrid::s4u::Engine::get_clock());
            }
        });

    backend::SimGridBackend backend_inst{spec};
    auto placement = algorithms::AlgorithmRegistry::make_placement("first_fit");
    PingPongMigration migration;
    const auto result = backend_inst.run(*placement, &migration);

    // Reaching this point at all is the core assertion: without the fix the
    // process dies with SIGSEGV (or corrupts the heap and never returns from
    // engine.run(); the ctest TIMEOUT converts that into a failure).

    // Migrations must actually have happened for the test to be meaningful.
    REQUIRE(result.summary.total_migrations >= kNumVms);

    // The orchestrator must have covered the whole nominal duration, tick by
    // tick: the last recorded tick closes exactly at duration_seconds.
    REQUIRE_FALSE(result.time_series.empty());
    CHECK(result.time_series.back().t_seconds == spec.duration_seconds);

    // Non-vacuousness: vm-0's first migration finalize window (first two
    // on_migration_end fires) must straddle its terminate at t=500, i.e.
    // the VM was destroyed after the RX cleared is_migrating but before the
    // issuer finished sg_vm_migrate().
    REQUIRE(vm0_mig_end_times.size() >= 2);
    CHECK(vm0_mig_end_times[0] < 500.0);
    CHECK(vm0_mig_end_times[1] > 500.0);
}

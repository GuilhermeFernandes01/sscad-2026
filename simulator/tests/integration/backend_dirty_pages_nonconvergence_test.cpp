// Integration test: non-convergent pre-copy (dirty rate > available
// bandwidth) must not stall the simulation clock (ressalva R-DIRTY,
// exp-20260523-cross-base-comparability).
//
// SimGrid's live_migration plugin iterates stage 2 while
// remaining > threshold = measured_bandwidth * max_downtime (30 ms). When
// the VM dirties memory faster than the link drains it, the loop never
// converges; the plugin's only internal bound is a hard migration budget of
// 1e7 simulated seconds (VmLiveMigration.cpp, mig_timeout), i.e. ~115 days
// of simulated clock per stuck migration. The backend must therefore bound
// non-convergent migrations itself: the end-of-run cleanup destroys every
// VM, which kills the plugin's TX/RX/issuer actors (is_migrating is still
// true, so onVirtualMachineShutdown applies), and engine.run() returns at
// the nominal duration.
//
// Scenario: 2 DCs x 1 host, 50 Mbit/s WAN (6.25 MB/s). Three VMs (512 MiB
// RAM, one full-speed core, dirty_rate_mbps = 400 = 50 MB/s) all migrate
// concurrently at t = 120 s, sharing the narrow link: per-flow bandwidth
// ~2 MB/s, dirty rate 25x above it: certain non-convergence. Assertions:
//   1. engine.run() terminates (ctest TIMEOUT turns a hang into a failure)
//      with the SimGrid clock at ~duration, not at the plugin's 1e7 s bound;
//   2. no migration ever completes (on_migration_end never fires); the
//      transfers were still cycling in stage 2 when the run ended;
//   3. each VM transferred well over its RAM size (stage-2 retransmissions
//      kept the link saturated until the end of the run).
// Without the dirty-page fix the migrations degenerate into a single RAM
// copy: they converge, complete, and transfer exactly RAM, so assertions 2
// and 3 fail, proving the scenario really exercises non-convergence.

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/registry.hpp"
#include "algosim/backend/simgrid_backend.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <simgrid/s4u/Comm.hpp>
#include <simgrid/s4u/Engine.hpp>
#include <simgrid/s4u/Mailbox.hpp>
#include <simgrid/s4u/VirtualMachine.hpp>

#include <map>
#include <string>
#include <vector>

using namespace algosim;

namespace {

constexpr int    kNumVms   = 3;
constexpr int    kRamMb    = 512;
constexpr double kRamBytes = static_cast<double>(kRamMb) * 1024.0 * 1024.0;

domain::Host make_host(const std::string& id, const std::string& dc_id) {
    domain::Host h;
    h.host_id           = id;
    h.dc_id             = dc_id;
    h.cpu_cores         = 8;
    h.cpu_capacity_mips = 10000.0;
    h.ram_mb            = 65536;
    h.net_bw_mbps       = 1000.0;  // platform generator emits this as MBps
    h.power_idle_w      = 50.0;
    h.power_peak_w      = 100.0;
    return h;
}

scenario::ScenarioSpec make_spec() {
    scenario::ScenarioSpec spec;
    spec.name                       = "dirty_pages_nonconvergence_it";
    spec.duration_seconds           = 1800.0;
    spec.dt_seconds                 = 10.0;
    spec.migration_interval_seconds = 10.0;
    spec.seed                       = 42;
    spec.network.inter_dc_bw_mbps   = 50.0;  // 6.25 MB/s WAN, shared 3 ways

    for (const std::string& dc_id : {std::string{"dca"}, std::string{"dcb"}}) {
        domain::Datacenter dc;
        dc.dc_id            = dc_id;
        dc.name             = dc_id;
        dc.latitude         = dc_id == "dca" ? 0.0 : 10.0;
        dc.longitude        = dc_id == "dca" ? 0.0 : 10.0;
        dc.pue              = 1.0;
        dc.carbon_series_id = dc_id;
        dc.hosts            = {make_host(dc_id + "-h000", dc_id),
                               make_host(dc_id + "-h001", dc_id),
                               make_host(dc_id + "-h002", dc_id)};
        spec.datacenters.push_back(dc);

        domain::CarbonIntensitySeries series;
        series.series_id    = dc_id;
        series.start        = spec.start_datetime;
        series.step_seconds = 3600;
        series.gco2_per_kwh = {475.0};
        spec.carbon_series.emplace(dc_id, series);
    }

    for (int i = 0; i < kNumVms; ++i) {
        domain::VM vm;
        vm.vm_id           = "vm-" + std::to_string(i);
        vm.cpu_cores       = 1;
        // Full-speed core for the whole run (see transfer test): dirty bytes
        // derive from computed flops, so the VM must stay busy.
        vm.cpu_demand_mips = 10000.0;
        vm.ram_mb          = kRamMb;
        vm.image_size_mb   = kRamMb;
        vm.dirty_rate_mbps = 400.0;  // 50 MB/s >> per-flow WAN share
        vm.arrival_time_s  = 0.0;

        domain::WorkloadEvent ev;
        ev.t_seconds = 0.0;
        ev.kind      = domain::EventKind::Submit;
        ev.vm        = vm;
        spec.events.push_back(ev);
    }
    return spec;
}

// "dca-hNNN" <-> "dcb-hNNN": the homologous host in the other DC.
std::string other_dc_host(const std::string& host_id) {
    std::string out = host_id;
    out[2]          = out[2] == 'a' ? 'b' : 'a';
    return out;
}

// Migrates every VM exactly once, all at the first tick >= 120 s, each to
// the homologous host in the other DC, so the three transfers run
// concurrently over the same inter-DC link.
class ConcurrentOneShotMigration final
    : public algorithms::MigrationAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "one_shot"; }

    [[nodiscard]] std::vector<domain::MigrationDecision>
    migrate(const domain::ClusterState& state) override {
        std::vector<domain::MigrationDecision> out;
        if (issued_ || state.t_seconds < 120.0) {
            return out;
        }
        for (const auto& vm : state.running_vms) {
            if (vm.host_id) {
                out.push_back(
                    {vm.vm_id, *vm.host_id, other_dc_host(*vm.host_id), "one_shot"});
            }
        }
        issued_ = !out.empty();
        return out;
    }

private:
    bool issued_ = false;
};

// Same probe as backend_dirty_pages_transfer_test: bytes posted per VM on
// the migration data mailbox "__mbox_mig_src_dst:<vm>(<src>-<dst>)".
std::map<std::string, double>& bytes_by_vm() {
    static std::map<std::string, double> bytes;
    return bytes;
}

void register_probe() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    simgrid::s4u::Comm::on_send_cb([](simgrid::s4u::Comm const& c) {
        const auto* mbox = c.get_mailbox();
        if (mbox == nullptr) {
            return;
        }
        static const std::string prefix = "__mbox_mig_src_dst:";
        const std::string&       name   = mbox->get_name();
        if (name.rfind(prefix, 0) != 0) {
            return;
        }
        const auto open = name.find('(', prefix.size());
        bytes_by_vm()[name.substr(prefix.size(), open - prefix.size())] +=
            c.get_remaining();
    });
}

}  // namespace

TEST_CASE("non-convergent pre-copy migrations terminate with the run and do "
          "not stall the simulation clock",
          "[integration][backend][migration][dirty-pages]") {
    algorithms::register_builtin_algorithms();
    register_probe();
    bytes_by_vm().clear();

    // Times at which on_migration_end fired. A migration that really
    // completes fires it twice during the run (RX finalize + issuer's
    // end_migration); a migration killed by the end-of-run cleanup fires it
    // once, at exactly t = duration, from the plugin's shutdown callback.
    static std::vector<double> migration_end_times;
    migration_end_times.clear();
    simgrid::s4u::VirtualMachine::on_migration_end_cb(
        [](const simgrid::s4u::VirtualMachine&) {
            migration_end_times.push_back(simgrid::s4u::Engine::get_clock());
        });

    const auto spec = make_spec();

    backend::SimGridBackend backend_inst{spec};
    auto placement = algorithms::AlgorithmRegistry::make_placement("first_fit");
    ConcurrentOneShotMigration migration;
    const auto result = backend_inst.run(*placement, &migration);

    // 1. Termination near the nominal duration: the orchestrator covered
    //    every tick and the end-of-run cleanup killed the stuck transfers
    //    without letting the plugin's 1e7 s budget inflate the clock.
    REQUIRE_FALSE(result.time_series.empty());
    CHECK(result.time_series.back().t_seconds == spec.duration_seconds);
    CHECK(simgrid::s4u::Engine::get_clock() <= spec.duration_seconds + 60.0);

    // All three migrations were issued.
    REQUIRE(result.summary.total_migrations == kNumVms);

    // 2. Non-convergence is real: no migration ever reached stage 3 /
    //    finalize during the run: every on_migration_end fire comes from
    //    the end-of-run teardown, never earlier.
    for (const double t_end : migration_end_times) {
        CHECK(t_end >= spec.duration_seconds);
    }

    // 3. Each VM kept retransmitting dirty pages: well over one full RAM
    //    copy per VM was posted before the run ended.
    for (int i = 0; i < kNumVms; ++i) {
        CHECK(bytes_by_vm()["vm-" + std::to_string(i)] > 1.5 * kRamBytes);
    }
}

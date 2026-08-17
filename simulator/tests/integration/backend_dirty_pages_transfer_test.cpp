// Integration test: a VM's dirty_rate_mbps must reach SimGrid's live
// migration model (ressalva R-DIRTY, exp-20260523-cross-base-comparability).
//
// The defect: the backend never calls sg_vm_set_migration_speed /
// sg_vm_set_dirty_page_intensity / sg_vm_set_working_set_memory, so the
// live_migration plugin sees dp_intensity = 0 and mig_speed = 0, computes
// dp_rate = mig_speed * dp_intensity / host_speed = 0, skips the whole
// pre-copy stage 2, and every migration degenerates into a single RAM copy.
// domain::VM::dirty_rate_mbps is parsed from YAML and then ignored, so
// migration cost (bytes, duration) is systematically underestimated.
//
// Scenario: 2 DCs x 1 host, 1 Gbps WAN. Two identical VMs (2 GiB RAM, one
// full-speed core) that differ only in dirty_rate_mbps: vm-clean = 0,
// vm-dirty = 400 Mbps (= 50 MB/s, 40% of the 125 MB/s WAN, so the pre-copy
// converges geometrically). Each VM is migrated exactly once, at disjoint
// times. A Comm::on_send probe attributes the posted bytes of every
// migration data chunk (mailbox "__mbox_mig_src_dst:<vm>(src-dst)") to its
// VM. With the fix, vm-dirty must retransmit stage-2 rounds of
// sum_k r^k ~ r/(1-r) = 0.67x its RAM on top of the stage-1 full copy,
// while vm-clean still transfers exactly its RAM once. Without the fix both
// transfer exactly RAM and the ratio assertion fails.
//
// The test also pins the backend's total_mig_bytes metric to the bytes
// actually posted by the migration comms (in MiB): before the fix it
// counted image_size_mb once per issued migration, a static value that
// ignores both the real RAM copy and the dirty-page retransmissions
// (image_size_mb is deliberately != ram_mb here to expose that).

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/registry.hpp"
#include "algosim/backend/simgrid_backend.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <simgrid/s4u/Comm.hpp>
#include <simgrid/s4u/Mailbox.hpp>

#include <map>
#include <string>
#include <vector>

using namespace algosim;

namespace {

constexpr int    kRamMb    = 2048;
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

domain::VM make_vm(const std::string& id, double dirty_rate_mbps) {
    domain::VM vm;
    vm.vm_id           = id;
    vm.cpu_cores       = 1;
    // Demand == host capacity: the workload exec (demand * 1e6 * duration * 2
    // flops at full core speed) outlives the run, so the VM computes at full
    // speed during its whole life; the dirty-page model derives dirty bytes
    // from computed flops, so the VM must stay busy while migrating.
    vm.cpu_demand_mips = 10000.0;
    vm.ram_mb          = kRamMb;
    vm.image_size_mb   = 1024;  // deliberately != ram_mb (see header comment)
    vm.dirty_rate_mbps = dirty_rate_mbps;
    vm.arrival_time_s  = 0.0;
    return vm;
}

scenario::ScenarioSpec make_spec() {
    scenario::ScenarioSpec spec;
    spec.name                       = "dirty_pages_transfer_it";
    spec.duration_seconds           = 600.0;
    spec.dt_seconds                 = 10.0;
    spec.migration_interval_seconds = 10.0;
    spec.seed                       = 42;
    spec.network.inter_dc_bw_mbps   = 1000.0;  // 125 MB/s WAN

    for (const std::string& dc_id : {std::string{"dca"}, std::string{"dcb"}}) {
        domain::Datacenter dc;
        dc.dc_id            = dc_id;
        dc.name             = dc_id;
        dc.latitude         = dc_id == "dca" ? 0.0 : 10.0;
        dc.longitude        = dc_id == "dca" ? 0.0 : 10.0;
        dc.pue              = 1.0;
        dc.carbon_series_id = dc_id;
        dc.hosts            = {make_host(dc_id + "-h000", dc_id),
                               make_host(dc_id + "-h001", dc_id)};
        spec.datacenters.push_back(dc);

        domain::CarbonIntensitySeries series;
        series.series_id    = dc_id;
        series.start        = spec.start_datetime;
        series.step_seconds = 3600;
        series.gco2_per_kwh = {475.0};
        spec.carbon_series.emplace(dc_id, series);
    }

    for (const auto& vm : {make_vm("vm-clean", 0.0), make_vm("vm-dirty", 400.0)}) {
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

// Migrates each VM exactly once, to the homologous host in the other DC:
// vm-clean at the first tick >= 100 s, vm-dirty at the first tick >= 300 s
// (disjoint transfer windows: each transfer lasts < 60 s on this WAN).
class OneShotMigration final : public algorithms::MigrationAlgorithm {
public:
    [[nodiscard]] std::string_view name() const override { return "one_shot"; }

    [[nodiscard]] std::vector<domain::MigrationDecision>
    migrate(const domain::ClusterState& state) override {
        std::vector<domain::MigrationDecision> out;
        issue_once(state, "vm-clean", 100.0, moved_clean_, out);
        issue_once(state, "vm-dirty", 300.0, moved_dirty_, out);
        return out;
    }

private:
    static void issue_once(const domain::ClusterState& state,
                           const std::string& vm_id, double not_before,
                           bool& moved,
                           std::vector<domain::MigrationDecision>& out) {
        if (moved || state.t_seconds < not_before) {
            return;
        }
        for (const auto& vm : state.running_vms) {
            if (vm.vm_id == vm_id && vm.host_id) {
                out.push_back(
                    {vm_id, *vm.host_id, other_dc_host(*vm.host_id), "one_shot"});
                moved = true;
            }
        }
    }

    bool moved_clean_ = false;
    bool moved_dirty_ = false;
};

// Bytes posted by migration data comms, per VM. Comm::on_send fires once per
// chunk when the send is posted, with get_remaining() == chunk size; the
// migration data mailbox is "__mbox_mig_src_dst:<vm>(<src>-<dst>)"
// (verified against SimGrid 4.1.1, src/plugins/vm/VmLiveMigration.hpp).
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

TEST_CASE("dirty_rate_mbps drives pre-copy retransmission: a dirty VM "
          "transfers more bytes than an identical clean VM",
          "[integration][backend][migration][dirty-pages]") {
    algorithms::register_builtin_algorithms();
    register_probe();
    bytes_by_vm().clear();

    const auto spec = make_spec();

    backend::SimGridBackend backend_inst{spec};
    auto placement = algorithms::AlgorithmRegistry::make_placement("first_fit");
    OneShotMigration migration;
    const auto result = backend_inst.run(*placement, &migration);

    REQUIRE(result.summary.total_migrations == 2);

    const double clean_bytes = bytes_by_vm()["vm-clean"];
    const double dirty_bytes = bytes_by_vm()["vm-dirty"];

    // Sanity: the clean VM is exactly one full RAM copy (stage 1 = ramsize,
    // stage 2 skipped, stage 3 = 0 bytes). Also proves the probe works.
    REQUIRE(clean_bytes == kRamBytes);

    // Core assertion: dirty pages force stage-2 retransmissions. With
    // dirty rate = 40% of the WAN bandwidth the geometric series adds
    // ~0.67x RAM; require a robust 20% margin.
    CHECK(dirty_bytes > 1.2 * clean_bytes);

    // The backend metric must report the bytes actually transferred (MiB),
    // not image_size_mb once per issued migration.
    const double expected_mb = (clean_bytes + dirty_bytes) / (1024.0 * 1024.0);
    CHECK(result.summary.total_mig_bytes
          == Catch::Approx(expected_mb).epsilon(0.01));
}

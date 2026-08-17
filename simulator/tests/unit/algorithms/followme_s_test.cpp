#include "algosim/algorithms/followme_s.hpp"
#include "algosim/domain/cluster_state.hpp"
#include "algosim/domain/datacenter.hpp"
#include "algosim/domain/host.hpp"
#include "algosim/domain/vm.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

namespace {

// Two-DC fixture where dc-a (dirty) has an underutilized host and dc-b
// (clean) has a host with enough free capacity to absorb the VM.
domain::ClusterState make_migration_state() {
    domain::Host h_dirty;
    h_dirty.host_id           = "dc-a-h0";
    h_dirty.dc_id             = "dc-a";
    h_dirty.cpu_cores         = 4;
    h_dirty.cpu_capacity_mips = 4000.0;
    h_dirty.ram_mb            = 8192;
    h_dirty.cpu_used_mips     = 500.0;   // 12.5% utilization, below 20% threshold
    h_dirty.ram_used_mb       = 2048;
    h_dirty.vms               = {"vm-1"};
    h_dirty.power_idle_w      = 50;
    h_dirty.power_peak_w      = 100;

    domain::Host h_clean;
    h_clean.host_id           = "dc-b-h0";
    h_clean.dc_id             = "dc-b";
    h_clean.cpu_cores         = 4;
    h_clean.cpu_capacity_mips = 4000.0;
    h_clean.ram_mb            = 8192;
    h_clean.power_idle_w      = 50;
    h_clean.power_peak_w      = 100;

    domain::Datacenter a;
    a.dc_id = "dc-a";
    a.hosts = {h_dirty};

    domain::Datacenter b;
    b.dc_id = "dc-b";
    b.hosts = {h_clean};

    domain::VM vm;
    vm.vm_id           = "vm-1";
    vm.cpu_demand_mips = 500.0;
    vm.ram_mb          = 2048;
    vm.host_id         = "dc-a-h0";

    domain::ClusterState state;
    state.datacenters = {a, b};
    state.running_vms = {vm};
    state.carbon_now["dc-a"] = 600.0;
    state.carbon_now["dc-b"] = 100.0;
    return state;
}

}  // namespace

TEST_CASE("FollowMeS migrates VM from underutilized host to greenest DC",
          "[unit][algorithm][followme_s]") {
    auto state = make_migration_state();

    algorithms::FollowMeS algo{0.20, 50};
    const auto decisions = algo.migrate(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].vm_id          == "vm-1");
    CHECK(decisions[0].source_host_id == "dc-a-h0");
    CHECK(decisions[0].target_host_id == "dc-b-h0");
}

TEST_CASE("FollowMeS skips well-utilized hosts", "[unit][algorithm][followme_s]") {
    auto state = make_migration_state();
    // Raise utilization above 20% threshold.
    state.datacenters[0].hosts[0].cpu_used_mips = 2000.0;  // 50%

    algorithms::FollowMeS algo{0.20, 50};
    const auto decisions = algo.migrate(state);

    CHECK(decisions.empty());
}

TEST_CASE("FollowMeS respects max_migrations_per_tick",
          "[unit][algorithm][followme_s]") {
    auto state = make_migration_state();
    // Add a second VM on the underutilized host.
    state.datacenters[0].hosts[0].vms.push_back("vm-2");
    domain::VM vm2;
    vm2.vm_id           = "vm-2";
    vm2.cpu_demand_mips = 500.0;
    vm2.ram_mb          = 1024;
    vm2.host_id         = "dc-a-h0";
    state.running_vms.push_back(vm2);

    algorithms::FollowMeS algo{0.20, 1};  // budget = 1 migration
    const auto decisions = algo.migrate(state);

    CHECK(decisions.size() == 1);
}

TEST_CASE("FollowMeS placement routes to greenest DC",
          "[unit][algorithm][followme_s]") {
    auto state = make_migration_state();
    // Clear running VMs for placement test.
    state.running_vms.clear();
    state.datacenters[0].hosts[0].cpu_used_mips = 0.0;
    state.datacenters[0].hosts[0].ram_used_mb   = 0;
    state.datacenters[0].hosts[0].vms.clear();

    domain::VM vm;
    vm.vm_id           = "new-vm";
    vm.cpu_demand_mips = 1000.0;
    vm.ram_mb          = 2048;
    state.pending_vms.push_back(vm);

    algorithms::FollowMeS algo;
    const auto decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "dc-b-h0");  // clean DC wins
}

#include "algosim/algorithms/follow_renewables.hpp"
#include "algosim/domain/cluster_state.hpp"
#include "algosim/domain/datacenter.hpp"
#include "algosim/domain/host.hpp"
#include "algosim/domain/vm.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

namespace {

domain::ClusterState make_migration_state() {
    domain::Host h1;
    h1.host_id           = "dirty-h0";
    h1.dc_id             = "dc-dirty";
    h1.cpu_cores         = 4;
    h1.cpu_capacity_mips = 4000.0;
    h1.ram_mb            = 8192;
    h1.cpu_used_mips     = 1000.0;
    h1.ram_used_mb       = 2048;
    h1.vms               = {"vm-1"};
    h1.power_idle_w      = 50;
    h1.power_peak_w      = 100;

    domain::Host h2;
    h2.host_id           = "clean-h0";
    h2.dc_id             = "dc-clean";
    h2.cpu_cores         = 4;
    h2.cpu_capacity_mips = 4000.0;
    h2.ram_mb            = 8192;
    h2.power_idle_w      = 50;
    h2.power_peak_w      = 100;

    domain::Datacenter dirty;
    dirty.dc_id = "dc-dirty";
    dirty.hosts = {h1};
    dirty.carbon_series_id = "dirty";

    domain::Datacenter clean;
    clean.dc_id = "dc-clean";
    clean.hosts = {h2};
    clean.carbon_series_id = "clean";

    domain::VM vm;
    vm.vm_id           = "vm-1";
    vm.cpu_demand_mips = 1000.0;
    vm.ram_mb          = 2048;
    vm.host_id         = "dirty-h0";
    vm.image_size_mb   = 2048;

    domain::ClusterState state;
    state.datacenters   = {dirty, clean};
    state.running_vms   = {vm};
    state.carbon_now["dc-dirty"] = 600.0;
    state.carbon_now["dc-clean"] = 150.0;
    return state;
}

}  // namespace

TEST_CASE("FollowRenewables migrates VM from dirty DC to clean DC",
          "[unit][algorithm][follow_renewables]") {
    auto state = make_migration_state();

    algorithms::FollowRenewables algo(400.0);
    const auto decisions = algo.migrate(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].vm_id == "vm-1");
    CHECK(decisions[0].source_host_id == "dirty-h0");
    CHECK(decisions[0].target_host_id == "clean-h0");
}

TEST_CASE("FollowRenewables does not migrate when all DCs are clean",
          "[unit][algorithm][follow_renewables]") {
    auto state = make_migration_state();
    state.carbon_now["dc-dirty"] = 200.0;

    algorithms::FollowRenewables algo(400.0);
    const auto decisions = algo.migrate(state);

    CHECK(decisions.empty());
}

TEST_CASE("FollowRenewables does not migrate when no clean DC has capacity",
          "[unit][algorithm][follow_renewables]") {
    auto state = make_migration_state();
    state.datacenters[1].hosts[0].cpu_used_mips = 3500.0;
    state.datacenters[1].hosts[0].ram_used_mb   = 7000;

    algorithms::FollowRenewables algo(400.0);
    const auto decisions = algo.migrate(state);

    CHECK(decisions.empty());
}

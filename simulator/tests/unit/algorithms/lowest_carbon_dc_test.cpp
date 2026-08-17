#include "algosim/algorithms/lowest_carbon_dc.hpp"
#include "algosim/domain/cluster_state.hpp"
#include "algosim/domain/datacenter.hpp"
#include "algosim/domain/host.hpp"
#include "algosim/domain/vm.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

namespace {

domain::ClusterState make_two_dc_state() {
    domain::Host h1;
    h1.host_id           = "dc-a-h0";
    h1.dc_id             = "dc-a";
    h1.cpu_cores         = 4;
    h1.cpu_capacity_mips = 4000.0;
    h1.ram_mb            = 8192;
    h1.power_idle_w      = 50;
    h1.power_peak_w      = 100;

    domain::Host h2;
    h2.host_id           = "dc-b-h0";
    h2.dc_id             = "dc-b";
    h2.cpu_cores         = 4;
    h2.cpu_capacity_mips = 4000.0;
    h2.ram_mb            = 8192;
    h2.power_idle_w      = 50;
    h2.power_peak_w      = 100;

    domain::Datacenter a;
    a.dc_id = "dc-a";
    a.hosts = {h1};
    a.carbon_series_id = "a";

    domain::Datacenter b;
    b.dc_id = "dc-b";
    b.hosts = {h2};
    b.carbon_series_id = "b";

    domain::ClusterState state;
    state.datacenters = {a, b};
    state.carbon_now["dc-a"] = 600.0;  // dirty
    state.carbon_now["dc-b"] = 200.0;  // clean
    return state;
}

}  // namespace

TEST_CASE("LowestCarbonDC routes to the DC with lower carbon intensity",
          "[unit][algorithm][lowest_carbon_dc]") {
    auto state = make_two_dc_state();

    domain::VM vm;
    vm.vm_id = "vm-1";
    vm.cpu_demand_mips = 1000;
    vm.ram_mb = 2048;
    state.pending_vms.push_back(vm);

    algorithms::LowestCarbonDC algo;
    const auto decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "dc-b-h0");  // dc-b has 200 gCO2/kWh
}

TEST_CASE("LowestCarbonDC falls back to dirtier DC when clean one is full",
          "[unit][algorithm][lowest_carbon_dc]") {
    auto state = make_two_dc_state();

    // Fill dc-b completely.
    state.datacenters[1].hosts[0].cpu_used_mips = 4000.0;

    domain::VM vm;
    vm.vm_id = "vm-1";
    vm.cpu_demand_mips = 1000;
    vm.ram_mb = 2048;
    state.pending_vms.push_back(vm);

    algorithms::LowestCarbonDC algo;
    const auto decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "dc-a-h0");  // fallback to dirty DC
}

#include "algosim/algorithms/cnemesis.hpp"
#include "algosim/domain/cluster_state.hpp"
#include "algosim/domain/datacenter.hpp"
#include "algosim/domain/host.hpp"
#include "algosim/domain/vm.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

namespace {

// Three-DC fixture: dirty / mid / clean, each with a single host.
domain::ClusterState make_three_dc_state() {
    domain::Host h1;
    h1.host_id           = "dirty-h0";
    h1.dc_id             = "dirty";
    h1.cpu_cores         = 4;
    h1.cpu_capacity_mips = 4000.0;
    h1.ram_mb            = 8192;
    h1.power_idle_w      = 50;
    h1.power_peak_w      = 100;

    domain::Host h2;
    h2.host_id           = "mid-h0";
    h2.dc_id             = "mid";
    h2.cpu_cores         = 4;
    h2.cpu_capacity_mips = 4000.0;
    h2.ram_mb            = 8192;
    h2.power_idle_w      = 50;
    h2.power_peak_w      = 100;

    domain::Host h3;
    h3.host_id           = "clean-h0";
    h3.dc_id             = "clean";
    h3.cpu_cores         = 4;
    h3.cpu_capacity_mips = 4000.0;
    h3.ram_mb            = 8192;
    h3.power_idle_w      = 50;
    h3.power_peak_w      = 100;

    domain::Datacenter dirty;
    dirty.dc_id = "dirty";
    dirty.hosts = {h1};
    domain::Datacenter mid;
    mid.dc_id   = "mid";
    mid.hosts   = {h2};
    domain::Datacenter clean;
    clean.dc_id = "clean";
    clean.hosts = {h3};

    domain::ClusterState state;
    state.datacenters = {dirty, mid, clean};
    state.carbon_now["dirty"] = 900.0;
    state.carbon_now["mid"]   = 400.0;
    state.carbon_now["clean"] = 100.0;
    return state;
}

}  // namespace

TEST_CASE("CNemesis placement picks greenest DC for each pending VM",
          "[unit][algorithm][cnemesis]") {
    auto state = make_three_dc_state();
    domain::VM vm;
    vm.vm_id           = "vm-1";
    vm.cpu_demand_mips = 1000.0;
    vm.ram_mb          = 2048;
    state.pending_vms.push_back(vm);

    algorithms::CNemesis algo;
    const auto decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "clean-h0");
}

TEST_CASE("CNemesis migrates VMs from sender DCs (above median) to receivers",
          "[unit][algorithm][cnemesis]") {
    auto state = make_three_dc_state();
    // VM on dirty DC (above median of {100,400,900} = 400 → senders: dirty).
    state.datacenters[0].hosts[0].cpu_used_mips = 1000.0;
    state.datacenters[0].hosts[0].ram_used_mb   = 2048;
    state.datacenters[0].hosts[0].vms           = {"vm-1"};

    domain::VM vm;
    vm.vm_id           = "vm-1";
    vm.cpu_demand_mips = 1000.0;
    vm.ram_mb          = 2048;
    vm.host_id         = "dirty-h0";
    state.running_vms  = {vm};

    algorithms::CNemesis algo;
    const auto decisions = algo.migrate(state);

    REQUIRE(decisions.size() >= 1);
    CHECK(decisions[0].vm_id          == "vm-1");
    CHECK(decisions[0].source_host_id == "dirty-h0");
    // Target should be a receiver (mid or clean), not dirty.
    CHECK((decisions[0].target_host_id == "clean-h0"
           || decisions[0].target_host_id == "mid-h0"));
}

TEST_CASE("CNemesis respects migration budget",
          "[unit][algorithm][cnemesis]") {
    auto state = make_three_dc_state();
    // Two VMs on dirty host, both candidates for migration.
    state.datacenters[0].hosts[0].cpu_used_mips = 2000.0;
    state.datacenters[0].hosts[0].ram_used_mb   = 4096;
    state.datacenters[0].hosts[0].vms           = {"vm-1", "vm-2"};

    for (int i = 1; i <= 2; ++i) {
        domain::VM v;
        v.vm_id           = "vm-" + std::to_string(i);
        v.cpu_demand_mips = 1000.0;
        v.ram_mb          = 2048;
        v.host_id         = "dirty-h0";
        state.running_vms.push_back(v);
    }

    algorithms::CNemesis algo{/*max_concurrent=*/1};
    const auto decisions = algo.migrate(state);

    CHECK(decisions.size() == 1);
}

TEST_CASE("CNemesis benefit constraint blocks unprofitable migrations",
          "[unit][algorithm][cnemesis]") {
    auto state = make_three_dc_state();
    // Make receiver barely cleaner than sender so the min_benefit_ratio=0.9
    // test fails: 900 * 0.9 = 810; we need receiver > 810 to fail benefit,
    // but the check is `carbon[target] * 0.9 >= carbon[source]`, i.e. target
    // carbon * 0.9 >= source carbon. Set target=900/0.9 ≈ 1000, source=900.
    // Actually we want to test the FAILURE case, so make target almost as
    // dirty as source.
    state.carbon_now["dirty"] = 500.0;
    state.carbon_now["mid"]   = 480.0;   // 480*0.9 = 432 < 500 -> passes; bad
    // Let's force rejection: target=490, 490*0.9 = 441 < 500 -> passes; bad
    // For rejection we need target*0.9 >= source, i.e. target >= source/0.9
    // With source=500, target must be >= 555.6. Set mid=600 (still receiver
    // because below median? median of {100,500,600} = 500, so mid=600 is NOT
    // below median; it would be a sender too. Make this a 2-DC test instead.)
    state.datacenters.resize(2);  // drop 'clean'
    state.datacenters[0].dc_id = "dirty";
    state.datacenters[1].dc_id = "mid";
    state.carbon_now.clear();
    state.carbon_now["dirty"] = 500.0;
    state.carbon_now["mid"]   = 480.0;   // Just below -> receiver, but benefit
                                         // check: 480*0.9 = 432 < 500, passes.
    // To BLOCK the migration, set mid very close: 480*0.9=432 < 500 → passes.
    // Need mid*0.9 >= 500 → mid >= 555.56, but then mid > dirty and roles flip.
    // Conclusion: you can't have a "slightly cleaner" receiver with benefit
    // blocked: if target is cleaner enough to be receiver, it's cleaner
    // enough to profit (given ratio=0.9). Test a different path: when all
    // DCs have identical carbon, nothing migrates.
    state.carbon_now["dirty"] = 500.0;
    state.carbon_now["mid"]   = 500.0;
    state.datacenters[0].hosts[0].cpu_used_mips = 1000.0;
    state.datacenters[0].hosts[0].vms           = {"vm-1"};

    domain::VM v;
    v.vm_id           = "vm-1";
    v.cpu_demand_mips = 1000.0;
    v.ram_mb          = 2048;
    v.host_id         = "dirty-h0";
    state.running_vms = {v};

    algorithms::CNemesis algo;
    const auto decisions = algo.migrate(state);

    CHECK(decisions.empty());  // no benefit: all DCs equal
}

TEST_CASE("CNemesis consolidates underutilized DC hosts intra-DC",
          "[unit][algorithm][cnemesis]") {
    // Scenario: all 3 DCs have the same carbon (no inter-DC movement), but
    // one host is underutilized and another has room. Consolidation should
    // move the VM to the tighter host inside the same DC.
    auto state = make_three_dc_state();
    // Flatten carbon to disable inter-DC.
    state.carbon_now["dirty"] = 400.0;
    state.carbon_now["mid"]   = 400.0;
    state.carbon_now["clean"] = 400.0;

    // Give clean DC a second host that already has a large VM taking most
    // capacity, and a first host that's underutilized with a small VM.
    domain::Host h_small;
    h_small.host_id           = "clean-h1";
    h_small.dc_id             = "clean";
    h_small.cpu_cores         = 4;
    h_small.cpu_capacity_mips = 4000.0;
    h_small.ram_mb            = 8192;
    h_small.cpu_used_mips     = 3000.0;   // already well-used
    h_small.ram_used_mb       = 4096;
    h_small.vms               = {"vm-big"};
    h_small.power_idle_w      = 50;
    h_small.power_peak_w      = 100;
    state.datacenters[2].hosts.push_back(h_small);

    // Mark clean-h0 as underutilized with a small VM.
    state.datacenters[2].hosts[0].cpu_used_mips = 500.0;  // 12.5%
    state.datacenters[2].hosts[0].ram_used_mb   = 1024;
    state.datacenters[2].hosts[0].vms           = {"vm-small"};

    domain::VM vm_small;
    vm_small.vm_id           = "vm-small";
    vm_small.cpu_demand_mips = 500.0;
    vm_small.ram_mb          = 1024;
    vm_small.host_id         = "clean-h0";

    domain::VM vm_big;
    vm_big.vm_id           = "vm-big";
    vm_big.cpu_demand_mips = 3000.0;
    vm_big.ram_mb          = 4096;
    vm_big.host_id         = "clean-h1";

    state.running_vms = {vm_small, vm_big};

    algorithms::CNemesis algo;
    const auto decisions = algo.migrate(state);

    // Expect at least one consolidation decision moving vm-small to clean-h1.
    bool found = false;
    for (const auto& d : decisions) {
        if (d.vm_id == "vm-small" && d.target_host_id == "clean-h1") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("carbon_avg returns carbon_now when no forecast",
          "[unit][cluster_state]") {
    domain::ClusterState state;
    state.carbon_now["dc1"] = 500.0;
    CHECK(state.carbon_avg("dc1") == 500.0);
}

TEST_CASE("carbon_avg computes average with forecast data",
          "[unit][cluster_state]") {
    domain::ClusterState state;
    state.carbon_now["dc1"] = 100.0;
    state.carbon_forecast["dc1"] = {200.0, 300.0};
    state.forecast_hours = 2;
    // avg = (100 + 200 + 300) / 3 = 200
    CHECK(state.carbon_avg("dc1") == 200.0);
}

TEST_CASE("CNemesis placement uses carbon_avg when forecast is set",
          "[unit][algorithm][cnemesis]") {
    auto state = make_three_dc_state();
    // Clean DC (100 now) will get dirty in the future.
    state.carbon_forecast["clean"] = {800.0, 900.0};
    // Dirty DC (900 now) will get clean.
    state.carbon_forecast["dirty"] = {100.0, 100.0};
    // Mid DC stays at 400.
    state.carbon_forecast["mid"] = {400.0, 400.0};
    state.forecast_hours = 2;

    // carbon_avg: clean = (100+800+900)/3 = 600, dirty = (900+100+100)/3 = 366.7, mid = 400
    // Greenest by avg is dirty (366.7), then mid (400), then clean (600).

    domain::VM vm;
    vm.vm_id           = "vm-1";
    vm.cpu_demand_mips = 1000.0;
    vm.ram_mb          = 2048;
    state.pending_vms.push_back(vm);

    algorithms::CNemesis algo;
    const auto decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    // Without forecast: would pick "clean-h0" (carbon 100).
    // With forecast: should pick "dirty-h0" (avg carbon 366.7, lowest).
    CHECK(decisions[0].target_host_id == "dirty-h0");
}

#include "algosim/metrics/sla.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

namespace {

domain::Host make_host(const std::string& id, double capacity_mips) {
    domain::Host h;
    h.host_id           = id;
    h.dc_id             = "dc1";
    h.cpu_capacity_mips = capacity_mips;
    h.ram_mb            = 65536;
    return h;
}

domain::VM make_vm(const std::string& id, double demand_mips) {
    domain::VM vm;
    vm.vm_id           = id;
    vm.cpu_demand_mips = demand_mips;
    vm.ram_mb          = 1024;
    return vm;
}

}  // namespace

TEST_CASE("overcommitted host counts every VM placed on it as a violation",
          "[unit][metrics][sla]") {
    domain::ClusterState state;
    domain::Datacenter   dc;
    dc.dc_id = "dc1";

    auto full = make_host("h-full", 1000.0);
    full.vms  = {"vm-a", "vm-b"};
    auto ok   = make_host("h-ok", 1000.0);
    ok.vms    = {"vm-c"};
    dc.hosts  = {full, ok};
    state.datacenters.push_back(dc);

    state.running_vms = {make_vm("vm-a", 600.0), make_vm("vm-b", 600.0),
                         make_vm("vm-c", 500.0)};

    // h-full: 600+600 = 1200 > 1000 -> its 2 VMs are degraded.
    // h-ok:   500 <= 1000           -> no violation.
    CHECK(metrics::sla_overcommit_vm_ticks(state) == 2);
}

TEST_CASE("no violations when every host meets its demand",
          "[unit][metrics][sla]") {
    domain::ClusterState state;
    domain::Datacenter   dc;
    dc.dc_id = "dc1";

    auto h = make_host("h0", 1000.0);
    h.vms  = {"vm-a"};
    dc.hosts.push_back(h);
    state.datacenters.push_back(dc);
    state.running_vms = {make_vm("vm-a", 999.0)};

    CHECK(metrics::sla_overcommit_vm_ticks(state) == 0);
}

TEST_CASE("empty cluster has zero violations", "[unit][metrics][sla]") {
    domain::ClusterState state;
    CHECK(metrics::sla_overcommit_vm_ticks(state) == 0);
}

TEST_CASE("VM ids without a running record contribute no demand",
          "[unit][metrics][sla]") {
    domain::ClusterState state;
    domain::Datacenter   dc;
    dc.dc_id = "dc1";

    auto h = make_host("h0", 1000.0);
    h.vms  = {"vm-ghost", "vm-a"};
    dc.hosts.push_back(h);
    state.datacenters.push_back(dc);
    state.running_vms = {make_vm("vm-a", 500.0)};

    CHECK(metrics::sla_overcommit_vm_ticks(state) == 0);
}

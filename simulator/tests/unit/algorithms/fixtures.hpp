#pragma once

#include "algosim/domain/cluster_state.hpp"
#include "algosim/domain/datacenter.hpp"
#include "algosim/domain/host.hpp"
#include "algosim/domain/vm.hpp"

#include <string>

namespace algosim::tests {

// A small hand-crafted cluster with one datacenter and three hosts of
// *deliberately* different capacities, so that First/Best/Worst Fit and
// Round Robin all produce visibly different placements.
//
//   host-a: free 1000 mips, free 4096 ram
//   host-b: free 5000 mips, free 8192 ram
//   host-c: free 3000 mips, free 6144 ram
//
// Use `make_vm(id, mips, ram)` to quickly add pending VMs.
inline domain::ClusterState make_three_host_state() {
    domain::Host a;
    a.host_id           = "host-a";
    a.dc_id             = "dc-0";
    a.cpu_cores         = 2;
    a.cpu_capacity_mips = 1000.0;
    a.ram_mb            = 4096;
    a.power_idle_w      = 50;
    a.power_peak_w      = 100;

    domain::Host b;
    b.host_id           = "host-b";
    b.dc_id             = "dc-0";
    b.cpu_cores         = 8;
    b.cpu_capacity_mips = 5000.0;
    b.ram_mb            = 8192;
    b.power_idle_w      = 100;
    b.power_peak_w      = 250;

    domain::Host c;
    c.host_id           = "host-c";
    c.dc_id             = "dc-0";
    c.cpu_cores         = 4;
    c.cpu_capacity_mips = 3000.0;
    c.ram_mb            = 6144;
    c.power_idle_w      = 80;
    c.power_peak_w      = 180;

    domain::Datacenter dc;
    dc.dc_id            = "dc-0";
    dc.name             = "Test DC";
    dc.pue              = 1.2;
    dc.carbon_series_id = "flat";
    dc.hosts            = {a, b, c};

    domain::ClusterState state;
    state.t_seconds = 0.0;
    state.datacenters.push_back(std::move(dc));
    state.carbon_now["dc-0"] = 500.0;
    return state;
}

inline domain::VM make_vm(const std::string& id, double mips, int ram) {
    domain::VM v;
    v.vm_id           = id;
    v.cpu_cores       = 1;
    v.cpu_demand_mips = mips;
    v.ram_mb          = ram;
    v.image_size_mb   = ram;
    v.dirty_rate_mbps = 10;
    return v;
}

}  // namespace algosim::tests

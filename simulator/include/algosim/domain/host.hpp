#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace algosim::domain {

// A host in a datacenter. Stateful in the sense that `cpu_used_mips`,
// `ram_used_mb`, `vms` and `active` represent the current snapshot; the
// static fields (capacity, power) are immutable over a run.
struct Host {
    std::string host_id;
    std::string dc_id;
    int         cpu_cores       = 0;
    double      cpu_capacity_mips = 0.0;
    int         ram_mb          = 0;
    int         disk_gb         = 0;
    double      net_bw_mbps     = 0.0;
    double      power_idle_w    = 0.0;
    double      power_peak_w    = 0.0;

    // Dynamic snapshot fields.
    double                   cpu_used_mips = 0.0;
    int                      ram_used_mb   = 0;
    std::vector<std::string> vms;
    bool                     active = true;

    [[nodiscard]] double utilization() const noexcept {
        if (cpu_capacity_mips <= 0.0) {
            return 0.0;
        }
        return cpu_used_mips / cpu_capacity_mips;
    }

    [[nodiscard]] double free_mips() const noexcept {
        return std::max(0.0, cpu_capacity_mips - cpu_used_mips);
    }

    [[nodiscard]] int free_ram_mb() const noexcept {
        return std::max(0, ram_mb - ram_used_mb);
    }

    // Does this host have enough free capacity to host a VM with the given
    // MIPS and RAM demands?
    [[nodiscard]] bool can_host(double vm_demand_mips, int vm_ram_mb) const noexcept {
        return active && free_mips() >= vm_demand_mips && free_ram_mb() >= vm_ram_mb;
    }
};

}  // namespace algosim::domain

#pragma once

#include <cmath>
#include <limits>
#include <optional>
#include <string>

namespace algosim::domain {

// A virtual machine request / instance.
struct VM {
    std::string vm_id;
    int         cpu_cores       = 1;
    double      cpu_demand_mips = 0.0;
    int         ram_mb          = 0;
    int         disk_gb         = 0;
    int         image_size_mb   = 0;     // for live migration cost
    double      dirty_rate_mbps = 0.0;   // pre-copy convergence driver
    double      arrival_time_s  = 0.0;
    // lifetime in simulation seconds; +inf encodes "permanent" (runs to end).
    double      lifetime_s      = std::numeric_limits<double>::infinity();

    // Current placement; nullopt when the VM is still in the pending queue.
    std::optional<std::string> host_id;

    [[nodiscard]] bool is_permanent() const noexcept {
        return !std::isfinite(lifetime_s);
    }
};

}  // namespace algosim::domain

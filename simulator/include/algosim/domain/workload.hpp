#pragma once

#include "algosim/domain/vm.hpp"

#include <optional>

namespace algosim::domain {

enum class EventKind {
    Submit,
    Terminate,
    DemandChange,
};

// A single workload event at a deterministic simulation time.
//
// For Submit events, `vm` is the full VM description (with `host_id` unset).
// For Terminate and DemandChange events, only `vm.vm_id` is meaningful.
struct WorkloadEvent {
    double                t_seconds = 0.0;
    EventKind             kind      = EventKind::Submit;
    VM                    vm;
    std::optional<double> new_demand_mips;
};

}  // namespace algosim::domain

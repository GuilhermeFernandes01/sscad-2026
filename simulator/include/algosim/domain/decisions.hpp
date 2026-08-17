#pragma once

#include <string>

namespace algosim::domain {

// Output of a PlacementAlgorithm: assign an unplaced VM to a specific host.
struct PlacementDecision {
    std::string vm_id;
    std::string target_host_id;
    std::string reason;
};

// Output of a MigrationAlgorithm: move a running VM from source to target.
struct MigrationDecision {
    std::string vm_id;
    std::string source_host_id;
    std::string target_host_id;
    std::string reason;
};

}  // namespace algosim::domain

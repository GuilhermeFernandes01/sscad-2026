#pragma once

#include "algosim/domain/cluster_state.hpp"
#include "algosim/domain/decisions.hpp"

#include <string_view>
#include <vector>

namespace algosim::algorithms {

// A placement algorithm assigns hosts to VMs currently in `pending_vms`.
//
// Contract:
//   - The algorithm MUST return one decision per VM in `state.pending_vms`
//     that it chooses to place. VMs omitted from the output are left pending
//     and will be re-offered on the next tick.
//   - The algorithm MUST NOT mutate `state`.
//   - `place()` must be a deterministic function of `state` (and of any
//     internal state exposed via the constructor for stateful algorithms).
//   - `name()` returns a short ASCII identifier used for run IDs and the
//     registry (e.g. "first_fit", "best_fit").
class PlacementAlgorithm {
public:
    PlacementAlgorithm() = default;
    PlacementAlgorithm(const PlacementAlgorithm&) = delete;
    PlacementAlgorithm(PlacementAlgorithm&&)      = delete;
    PlacementAlgorithm& operator=(const PlacementAlgorithm&) = delete;
    PlacementAlgorithm& operator=(PlacementAlgorithm&&)      = delete;
    virtual ~PlacementAlgorithm()                            = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) = 0;
};

}  // namespace algosim::algorithms

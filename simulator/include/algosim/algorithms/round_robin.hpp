#pragma once

#include "algosim/algorithms/placement_algorithm.hpp"

#include <cstddef>

namespace algosim::algorithms {

// Round Robin: place each pending VM on the next host in lexicographic order,
// advancing a persistent cursor. If the cursor's host lacks capacity, the
// algorithm probes the next host and so on, ensuring it never drops a VM
// when at least one host fits.
//
// The cursor is exposed via the constructor (default 0) so tests can reset
// it. There is no global state.
class RoundRobin final : public PlacementAlgorithm {
public:
    explicit RoundRobin(std::size_t initial_cursor = 0) : cursor_{initial_cursor} {}

    [[nodiscard]] std::string_view name() const override { return "round_robin"; }
    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;

    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

private:
    std::size_t cursor_ = 0;
};

}  // namespace algosim::algorithms

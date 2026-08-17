#include "fixtures.hpp"

#include "algosim/algorithms/best_fit.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

TEST_CASE("BestFit selects the tightest-fitting host", "[unit][algorithm][best_fit]") {
    auto state = tests::make_three_host_state();
    // 500 mips / 2048 ram; fits all three hosts. BestFit should choose
    // host-a (free 1000) because it has the smallest slack after placement.
    state.pending_vms.push_back(tests::make_vm("vm-1", 500, 2048));

    algorithms::BestFit algo;
    const auto          decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "host-a");
}

TEST_CASE("BestFit prefers smaller slack over host order",
          "[unit][algorithm][best_fit]") {
    auto state = tests::make_three_host_state();
    // 2000 mips: does NOT fit host-a (1000). Between host-b (5000) and
    // host-c (3000), BestFit picks host-c because slack 1000 < 3000.
    state.pending_vms.push_back(tests::make_vm("vm-1", 2000, 2048));

    algorithms::BestFit algo;
    const auto          decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "host-c");
}

TEST_CASE("BestFit matches FirstFit on a single-host fixture",
          "[unit][algorithm][best_fit]") {
    auto state = tests::make_three_host_state();
    // Only one VM, and only host-b can fit it.
    state.pending_vms.push_back(tests::make_vm("big", 4000, 2048));

    algorithms::BestFit algo;
    const auto          decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "host-b");
}

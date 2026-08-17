#include "fixtures.hpp"

#include "algosim/algorithms/round_robin.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

TEST_CASE("RoundRobin cycles through hosts", "[unit][algorithm][round_robin]") {
    auto state = tests::make_three_host_state();
    state.pending_vms.push_back(tests::make_vm("vm-1", 200, 512));
    state.pending_vms.push_back(tests::make_vm("vm-2", 200, 512));
    state.pending_vms.push_back(tests::make_vm("vm-3", 200, 512));

    algorithms::RoundRobin algo;
    const auto             decisions = algo.place(state);

    REQUIRE(decisions.size() == 3);
    CHECK(decisions[0].target_host_id == "host-a");
    CHECK(decisions[1].target_host_id == "host-b");
    CHECK(decisions[2].target_host_id == "host-c");
    CHECK(algo.cursor() == 0);  // wraps back to start
}

TEST_CASE("RoundRobin skips hosts without enough capacity",
          "[unit][algorithm][round_robin]") {
    auto state = tests::make_three_host_state();
    // vm needs 1500 mips; host-a cannot host it, so RR should skip to host-b.
    state.pending_vms.push_back(tests::make_vm("big", 1500, 512));

    algorithms::RoundRobin algo;
    const auto             decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "host-b");
    CHECK(algo.cursor() == 2);  // advances past host-b to host-c
}

TEST_CASE("RoundRobin is deterministic across repeated invocations",
          "[unit][algorithm][round_robin]") {
    auto state1 = tests::make_three_host_state();
    state1.pending_vms.push_back(tests::make_vm("vm-1", 200, 512));
    auto state2 = tests::make_three_host_state();
    state2.pending_vms.push_back(tests::make_vm("vm-1", 200, 512));

    algorithms::RoundRobin algo1;
    algorithms::RoundRobin algo2;
    CHECK(algo1.place(state1)[0].target_host_id
          == algo2.place(state2)[0].target_host_id);
}

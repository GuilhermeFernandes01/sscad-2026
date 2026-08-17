#include "fixtures.hpp"

#include "algosim/algorithms/worst_fit.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

TEST_CASE("WorstFit selects the host with the largest free capacity",
          "[unit][algorithm][worst_fit]") {
    auto state = tests::make_three_host_state();
    state.pending_vms.push_back(tests::make_vm("vm-1", 500, 2048));

    algorithms::WorstFit algo;
    const auto           decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "host-b");  // largest free mips
}

TEST_CASE("WorstFit spreads consecutive VMs across hosts",
          "[unit][algorithm][worst_fit]") {
    auto state = tests::make_three_host_state();
    state.pending_vms.push_back(tests::make_vm("vm-1", 500, 1024));
    state.pending_vms.push_back(tests::make_vm("vm-2", 500, 1024));

    algorithms::WorstFit algo;
    const auto           decisions = algo.place(state);

    REQUIRE(decisions.size() == 2);
    CHECK(decisions[0].target_host_id == "host-b");  // initially largest (5000)
    // After vm-1: host-b has 4500, host-c still 3000; host-b is still largest.
    CHECK(decisions[1].target_host_id == "host-b");
}

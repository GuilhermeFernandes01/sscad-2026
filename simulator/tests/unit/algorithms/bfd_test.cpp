#include "fixtures.hpp"

#include "algosim/algorithms/bfd.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

TEST_CASE("BFD combines decreasing sort with tightest-fit", "[unit][algorithm][bfd]") {
    auto state = tests::make_three_host_state();
    state.pending_vms.push_back(tests::make_vm("v1", 600, 512));
    state.pending_vms.push_back(tests::make_vm("v2", 2800, 512));

    algorithms::BFD algo;
    const auto      decisions = algo.place(state);

    REQUIRE(decisions.size() == 2);
    // v2 (2800) placed first; best fit among fitting hosts: host-c (3000, slack 200)
    CHECK(decisions[0].vm_id == "v2");
    CHECK(decisions[0].target_host_id == "host-c");
    // v1 (600): host-a (1000, slack 400) is tighter than host-b (5000, slack 4400)
    CHECK(decisions[1].vm_id == "v1");
    CHECK(decisions[1].target_host_id == "host-a");
}

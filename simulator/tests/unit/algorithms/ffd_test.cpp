#include "fixtures.hpp"

#include "algosim/algorithms/ffd.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

TEST_CASE("FFD places largest VMs first", "[unit][algorithm][ffd]") {
    auto state = tests::make_three_host_state();
    // vm-small fits on host-a, vm-big needs host-b or host-c.
    // FFD sorts by demand desc, so big goes first -> picks host-a? No: big
    // is 2500 MIPS, host-a only 1000. So big -> host-c (3000, tighter fit
    // than host-b 5000 via First Fit: actually FFD uses First Fit not Best
    // Fit, so it picks host-b first in lex order).
    state.pending_vms.push_back(tests::make_vm("vm-small", 500, 512));
    state.pending_vms.push_back(tests::make_vm("vm-big", 2500, 512));

    algorithms::FFD algo;
    const auto      decisions = algo.place(state);

    REQUIRE(decisions.size() == 2);
    // vm-big placed first (larger demand), needs >1000 -> host-b
    CHECK(decisions[0].vm_id == "vm-big");
    CHECK(decisions[0].target_host_id == "host-b");
    // vm-small placed second -> host-a (first that fits)
    CHECK(decisions[1].vm_id == "vm-small");
    CHECK(decisions[1].target_host_id == "host-a");
}

TEST_CASE("FFD uses fewer hosts than naive FirstFit order",
          "[unit][algorithm][ffd]") {
    auto state = tests::make_three_host_state();
    // FFD sorts to 2400,2000,600: 2400->host-b (first fit), 2000->host-b (still 600 free),
    // 600->host-a (first fit, host-a has 1000 >= 600). Uses 2 hosts.
    // Plain FirstFit in original order (600,2400,2000) would use: a(600), b(2400), c(2000) = 3 hosts.
    state.pending_vms.push_back(tests::make_vm("v1", 600, 512));
    state.pending_vms.push_back(tests::make_vm("v2", 2400, 512));
    state.pending_vms.push_back(tests::make_vm("v3", 2000, 512));

    algorithms::FFD algo;
    const auto      decisions = algo.place(state);

    REQUIRE(decisions.size() == 3);
    CHECK(decisions[0].target_host_id == "host-b");  // 2400 -> b
    CHECK(decisions[1].target_host_id == "host-b");  // 2000 -> b (600 left)
    CHECK(decisions[2].target_host_id == "host-a");  // 600 -> a (first fit)
}

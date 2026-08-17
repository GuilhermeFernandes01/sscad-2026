#include "fixtures.hpp"

#include "algosim/algorithms/first_fit.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace algosim;

TEST_CASE("FirstFit places a single VM on the first fitting host", "[unit][algorithm][first_fit]") {
    auto state = tests::make_three_host_state();
    state.pending_vms.push_back(tests::make_vm("vm-1", 500, 2048));

    algorithms::FirstFit algo;
    const auto           decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    // host-a has 1000 free mips / 4096 free ram; fits comfortably.
    CHECK(decisions[0].vm_id          == "vm-1");
    CHECK(decisions[0].target_host_id == "host-a");
    CHECK(decisions[0].reason         == "first_fit");
}

TEST_CASE("FirstFit skips hosts without enough CPU", "[unit][algorithm][first_fit]") {
    auto state = tests::make_three_host_state();
    // This VM is too large for host-a (1000 mips) but fits on host-b and host-c.
    state.pending_vms.push_back(tests::make_vm("big", 1500, 2048));

    algorithms::FirstFit algo;
    const auto           decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "host-b");   // first fitting host in lex order
}

TEST_CASE("FirstFit accounts for VMs placed earlier in the same batch",
          "[unit][algorithm][first_fit]") {
    auto state = tests::make_three_host_state();
    // Two VMs of 800 mips each. host-a has 1000 free, so only the first fits
    // there; the second must spill to host-b.
    state.pending_vms.push_back(tests::make_vm("vm-1", 800, 1024));
    state.pending_vms.push_back(tests::make_vm("vm-2", 800, 1024));

    algorithms::FirstFit algo;
    const auto           decisions = algo.place(state);

    REQUIRE(decisions.size() == 2);
    CHECK(decisions[0].target_host_id == "host-a");
    CHECK(decisions[1].target_host_id == "host-b");
}

TEST_CASE("FirstFit drops VMs that fit nowhere", "[unit][algorithm][first_fit]") {
    auto state = tests::make_three_host_state();
    state.pending_vms.push_back(tests::make_vm("huge", 10'000, 1024));

    algorithms::FirstFit algo;
    const auto           decisions = algo.place(state);

    CHECK(decisions.empty());   // no host fits; the VM remains pending
}

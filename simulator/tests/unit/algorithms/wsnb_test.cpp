#include "algosim/algorithms/wsnb.hpp"
#include "algosim/domain/cluster_state.hpp"
#include "algosim/domain/datacenter.hpp"
#include "algosim/domain/host.hpp"
#include "algosim/domain/vm.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using namespace algosim;

namespace {

// Two-DC fixture: dc-a is "dirty" (600 gCO2), dc-b is "clean" (200).
// Each DC has one host with 4000 MIPS / 8192 MB RAM.
domain::ClusterState make_two_dc_state() {
    domain::Host h1;
    h1.host_id           = "dc-a-h0";
    h1.dc_id             = "dc-a";
    h1.cpu_cores         = 4;
    h1.cpu_capacity_mips = 4000.0;
    h1.ram_mb            = 8192;
    h1.power_idle_w      = 50;
    h1.power_peak_w      = 100;

    domain::Host h2;
    h2.host_id           = "dc-b-h0";
    h2.dc_id             = "dc-b";
    h2.cpu_cores         = 4;
    h2.cpu_capacity_mips = 4000.0;
    h2.ram_mb            = 8192;
    h2.power_idle_w      = 50;
    h2.power_peak_w      = 100;

    domain::Datacenter a;
    a.dc_id = "dc-a";
    a.hosts = {h1};

    domain::Datacenter b;
    b.dc_id = "dc-b";
    b.hosts = {h2};

    domain::ClusterState state;
    state.datacenters = {a, b};
    state.carbon_now["dc-a"] = 600.0;
    state.carbon_now["dc-b"] = 200.0;
    return state;
}

// Escolhe um identificador de VM cujo DC de origem seja o índice pedido.
//
// A regra do DC de origem é reimplementada aqui de propósito: é uma SEGUNDA
// implementação independente, e qualquer mudança silenciosa da regra em
// wsnb.cpp faz estes testes falharem em vez de passarem por acidente.
//
// A regra é FNV-1a de 64 bits módulo o número de DCs, indexando a lista de DCs
// ORDENADA POR dc_id. Neste fixture a ordem de declaração {dc-a, dc-b} já é
// lexicográfica, então o índice ordenado coincide com o declarado. A versão
// anterior usava `std::hash<std::string>`, que é definido pela implementação;
// ver a justificativa em src/algorithms/wsnb.cpp.
std::uint64_t fnv1a_64(const std::string& s) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 0x100000001b3ULL;
    }
    return h;
}

std::string vm_id_with_home(std::size_t target_idx) {
    for (int i = 0;; ++i) {
        const auto candidate = "vm-" + std::to_string(i);
        if ((fnv1a_64(candidate) % 2) == target_idx) {
            return candidate;
        }
    }
}

}  // namespace

TEST_CASE("WSNB uses home DC when it is below threshold", "[unit][algorithm][wsnb]") {
    auto state = make_two_dc_state();
    // Make dc-a (dirty) the home DC but force it below threshold by lowering
    // its carbon intensity so locality wins.
    state.carbon_now["dc-a"] = 300.0;

    domain::VM vm;
    vm.vm_id           = vm_id_with_home(0);  // hashes to dc-a
    vm.cpu_demand_mips = 1000;
    vm.ram_mb          = 2048;
    state.pending_vms.push_back(vm);

    algorithms::WSNB algo{400.0};  // threshold 400 gCO2/kWh
    const auto decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "dc-a-h0");
}

TEST_CASE("WSNB falls back to greenest DC when home DC is above threshold",
          "[unit][algorithm][wsnb]") {
    auto state = make_two_dc_state();

    domain::VM vm;
    vm.vm_id           = vm_id_with_home(0);  // home = dc-a (600 gCO2, above 400)
    vm.cpu_demand_mips = 1000;
    vm.ram_mb          = 2048;
    state.pending_vms.push_back(vm);

    algorithms::WSNB algo{400.0};
    const auto decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "dc-b-h0");  // fell back to the clean DC
}

TEST_CASE("WSNB falls back to dirty DC when greenest DC has no capacity",
          "[unit][algorithm][wsnb]") {
    auto state = make_two_dc_state();
    // Home is dirty AND clean DC is full; should still place somewhere.
    state.datacenters[1].hosts[0].cpu_used_mips = 4000.0;

    domain::VM vm;
    vm.vm_id           = vm_id_with_home(0);
    vm.cpu_demand_mips = 1000;
    vm.ram_mb          = 2048;
    state.pending_vms.push_back(vm);

    algorithms::WSNB algo{400.0};
    const auto decisions = algo.place(state);

    REQUIRE(decisions.size() == 1);
    CHECK(decisions[0].target_host_id == "dc-a-h0");
}

TEST_CASE("WSNB is placement-only (no migration interface)",
          "[unit][algorithm][wsnb]") {
    // This test just ensures WSNB can be instantiated and doesn't crash with
    // an empty pending list: it's placement-only by design.
    domain::ClusterState state;
    algorithms::WSNB algo;
    const auto decisions = algo.place(state);
    CHECK(decisions.empty());
}

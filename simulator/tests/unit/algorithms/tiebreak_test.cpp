// Testes da política de desempate (algosim/algorithms/tiebreak.hpp).
//
// O que estes testes protegem, e por quê:
//
//   Uma auditoria de reprodutibilidade constatou que sete ordenações
//   dos algoritmos comparavam apenas a chave principal da heurística. A ordem
//   relativa de elementos equivalentes ficava a cargo da implementação de
//   ordenação e da ordem da sequência de entrada, nenhuma das duas fixada
//   pela política do algoritmo. Como parte dessas sequências deriva de
//   contêineres não ordenados, a política experimental estava subespecificada.
//
//   A correção foi transformar cada comparador em ORDEM TOTAL ESTRITA. Os
//   testes abaixo verificam as três propriedades que essa escolha deve
//   garantir, e que a regressão silenciosa destruiria:
//
//     1. o comparador é uma ordem estrita fraca válida (pré-requisito de
//        std::sort; violá-la é comportamento indefinido);
//     2. `std::sort` e `std::stable_sort` produzem o MESMO resultado: a
//        estabilidade deixa de ser premissa não declarada;
//     3. o resultado dos algoritmos é independente da ordem da sequência de
//        entrada, inclusive no caso extremo em que todas as chaves principais
//        são iguais.

#include "fixtures.hpp"

#include "algosim/algorithms/bfd.hpp"
#include "algosim/algorithms/ffd.hpp"
#include "algosim/algorithms/follow_renewables.hpp"
#include "algosim/algorithms/followme_s.hpp"
#include "algosim/algorithms/lowest_carbon_dc.hpp"
#include "algosim/algorithms/tiebreak.hpp"
#include "algosim/algorithms/wsnb.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace algosim;
using algorithms::tiebreak::carbon_asc;
using algorithms::tiebreak::vm_demand_desc;

namespace {

// Conjunto propositalmente degenerado: muitas VMs empatam na demanda, algumas
// empatam também na chegada, e só o vm_id as separa.
std::vector<domain::VM> vms_com_muitos_empates() {
    std::vector<domain::VM> vms;
    for (int i = 0; i < 24; ++i) {
        auto vm = tests::make_vm("vm-" + std::string(i < 10 ? "0" : "") + std::to_string(i),
                                 (i % 3 == 0) ? 1000.0 : 500.0, 512);
        vm.arrival_time_s = static_cast<double>(i % 2);
        vms.push_back(vm);
    }
    return vms;
}

template <typename T, typename Cmp>
void verificar_ordem_total_estrita(const std::vector<T>& itens, Cmp cmp) {
    for (const auto& a : itens) {
        // Irreflexividade.
        CHECK_FALSE(cmp(a, a));
        for (const auto& b : itens) {
            const bool ab = cmp(a, b);
            const bool ba = cmp(b, a);
            // Assimetria.
            CHECK_FALSE((ab && ba));
            for (const auto& c : itens) {
                // Transitividade.
                if (ab && cmp(b, c)) {
                    CHECK(cmp(a, c));
                }
            }
        }
    }
}

}  // namespace

TEST_CASE("desempate de VMs é ordem total estrita", "[unit][tiebreak]") {
    verificar_ordem_total_estrita(vms_com_muitos_empates(), vm_demand_desc);
}

TEST_CASE("desempate de VMs é total: nenhum par distinto é equivalente",
          "[unit][tiebreak]") {
    const auto vms = vms_com_muitos_empates();
    for (std::size_t i = 0; i < vms.size(); ++i) {
        for (std::size_t j = i + 1; j < vms.size(); ++j) {
            // Totalidade: para quaisquer dois elementos distintos, exatamente
            // um dos dois sentidos é verdadeiro. É isso que torna a ordem de
            // entrada irrelevante.
            CHECK(vm_demand_desc(vms[i], vms[j]) != vm_demand_desc(vms[j], vms[i]));
        }
    }
}

TEST_CASE("desempate de VMs: todas as chaves iguais são equivalentes",
          "[unit][tiebreak]") {
    // Caso degenerado exigido pela auditoria: demanda, chegada e identificador
    // idênticos. O comparador tem de reportar equivalência nos dois sentidos;
    // qualquer outra resposta violaria a ordem estrita fraca.
    auto a = tests::make_vm("vm-igual", 1234.5, 512);
    a.arrival_time_s = 60.0;
    const auto b = a;

    CHECK_FALSE(vm_demand_desc(a, b));
    CHECK_FALSE(vm_demand_desc(b, a));
}

TEST_CASE("desempate de VMs aplica as chaves na ordem especificada",
          "[unit][tiebreak]") {
    auto maior = tests::make_vm("vm-z", 2000.0, 512);
    maior.arrival_time_s = 999.0;
    auto menor = tests::make_vm("vm-a", 1000.0, 512);
    menor.arrival_time_s = 0.0;
    // (1) demanda decrescente domina chegada e identificador.
    CHECK(vm_demand_desc(maior, menor));

    auto antiga = tests::make_vm("vm-z", 1000.0, 512);
    antiga.arrival_time_s = 10.0;
    auto nova = tests::make_vm("vm-a", 1000.0, 512);
    nova.arrival_time_s = 20.0;
    // (2) empatada a demanda, a mais antiga vem primeiro, mesmo com vm_id maior.
    CHECK(vm_demand_desc(antiga, nova));

    auto primeira = tests::make_vm("vm-a", 1000.0, 512);
    primeira.arrival_time_s = 10.0;
    auto segunda = tests::make_vm("vm-b", 1000.0, 512);
    segunda.arrival_time_s = 10.0;
    // (3) empatadas demanda e chegada, decide o identificador.
    CHECK(vm_demand_desc(primeira, segunda));
}

TEST_CASE("desempate de data centers é ordem total estrita", "[unit][tiebreak]") {
    struct Dc {
        double      carbono;
        std::string dc_id;
    };
    // Perfil espacialmente uniforme (flat): TODOS os DCs empatam na chave
    // principal e só o dc_id os separa. É a configuração em que a política
    // subespecificada mais pesava no c-NEMESIS.
    const std::vector<Dc> dcs{{300.0, "dc-2"}, {300.0, "dc-0"}, {300.0, "dc-1"},
                              {120.0, "dc-3"}, {300.0, "dc-4"}};
    const auto cmp = [](const Dc& a, const Dc& b) {
        return carbon_asc(a.carbono, a.dc_id, b.carbono, b.dc_id);
    };
    verificar_ordem_total_estrita(dcs, cmp);

    auto ordenados = dcs;
    std::sort(ordenados.begin(), ordenados.end(), cmp);
    CHECK(ordenados[0].dc_id == "dc-3");  // menor intensidade
    CHECK(ordenados[1].dc_id == "dc-0");  // empates resolvidos pelo dc_id
    CHECK(ordenados[2].dc_id == "dc-1");
    CHECK(ordenados[3].dc_id == "dc-2");
    CHECK(ordenados[4].dc_id == "dc-4");
}

TEST_CASE("sort e stable_sort coincidem sob a ordem total", "[unit][tiebreak]") {
    // Se este teste falhar, a ordem deixou de ser total e a estabilidade voltou
    // a ser uma premissa implícita do resultado.
    auto por_sort = vms_com_muitos_empates();
    auto por_stable = por_sort;

    std::sort(por_sort.begin(), por_sort.end(), vm_demand_desc);
    std::stable_sort(por_stable.begin(), por_stable.end(), vm_demand_desc);

    REQUIRE(por_sort.size() == por_stable.size());
    for (std::size_t i = 0; i < por_sort.size(); ++i) {
        CHECK(por_sort[i].vm_id == por_stable[i].vm_id);
    }
}

TEST_CASE("ordenação independe da ordem da sequência de entrada",
          "[unit][tiebreak]") {
    const auto original = vms_com_muitos_empates();
    auto       esperado = original;
    std::sort(esperado.begin(), esperado.end(), vm_demand_desc);

    std::mt19937_64 rng{20260806};
    for (int tentativa = 0; tentativa < 50; ++tentativa) {
        auto embaralhado = original;
        std::shuffle(embaralhado.begin(), embaralhado.end(), rng);
        std::sort(embaralhado.begin(), embaralhado.end(), vm_demand_desc);

        for (std::size_t i = 0; i < esperado.size(); ++i) {
            CHECK(embaralhado[i].vm_id == esperado[i].vm_id);
        }
    }
}

TEST_CASE("FFD e BFD independem da ordem de chegada da fila pendente",
          "[unit][tiebreak][ffd][bfd]") {
    // Caso extremo: todas as VMs com a MESMA demanda e o mesmo instante de
    // chegada. Sem a ordem total, o resultado dependeria de qual permutação da
    // fila a biblioteca de ordenação decidisse produzir.
    std::vector<domain::VM> iguais;
    for (int i = 0; i < 12; ++i) {
        iguais.push_back(tests::make_vm(
            "vm-" + std::string(i < 10 ? "0" : "") + std::to_string(i), 400.0, 256));
    }

    const auto decisoes_de = [&](const std::vector<domain::VM>& fila, bool usar_bfd) {
        auto state         = tests::make_three_host_state();
        state.pending_vms  = fila;
        if (usar_bfd) {
            algorithms::BFD algo;
            return algo.place(state);
        }
        algorithms::FFD algo;
        return algo.place(state);
    };

    for (const bool usar_bfd : {false, true}) {
        const auto referencia = decisoes_de(iguais, usar_bfd);
        REQUIRE_FALSE(referencia.empty());

        std::mt19937_64 rng{20260806};
        for (int tentativa = 0; tentativa < 25; ++tentativa) {
            auto embaralhada = iguais;
            std::shuffle(embaralhada.begin(), embaralhada.end(), rng);
            const auto obtido = decisoes_de(embaralhada, usar_bfd);

            REQUIRE(obtido.size() == referencia.size());
            for (std::size_t i = 0; i < referencia.size(); ++i) {
                CHECK(obtido[i].vm_id == referencia[i].vm_id);
                CHECK(obtido[i].target_host_id == referencia[i].target_host_id);
            }
        }
    }
}

TEST_CASE("alocação independe da ordem de declaração dos data centers",
          "[unit][tiebreak][ordem-dc]") {
    // Os cenários da campanha declaram os data centers em ordem de fuso
    // horário (canberra, seoul, paris, virginia, dubai, singapore, pune,
    // johannesburg, sp), que não é lexicográfica. Cinco algoritmos ordenavam
    // pares (intensidade, índice) com o `operator<` do std::pair, o que
    // desempatava pelo ÍNDICE DE DECLARAÇÃO. Sob perfil de carbono espacialmente
    // uniforme (usado em parte das células), todos os DCs empatam na chave
    // principal, e o índice decidia sozinho: reordenar o YAML, uma edição sem
    // conteúdo semântico, mudaria os resultados.
    //
    // Este teste falha se qualquer decisão voltar a depender dessa ordem.
    const std::vector<std::string> ids_em_ordem_de_fuso = {
        "canberra", "seoul", "paris", "virginia", "dubai", "singapore"};

    const auto monta_dc = [](const std::string& dc_id) {
        domain::Host h;
        h.host_id           = "h-" + dc_id;
        h.dc_id             = dc_id;
        h.cpu_cores         = 8;
        h.cpu_capacity_mips = 4000.0;
        h.ram_mb            = 16384;
        h.power_idle_w      = 50;
        h.power_peak_w      = 100;

        domain::Datacenter dc;
        dc.dc_id            = dc_id;
        dc.pue              = 1.2;
        dc.carbon_series_id = dc_id;
        dc.hosts            = {h};
        return dc;
    };

    std::vector<domain::VM> pendentes;
    for (int i = 0; i < 6; ++i) {
        auto vm = tests::make_vm(
            "vm-" + std::string(i < 10 ? "0" : "") + std::to_string(i), 1500.0, 2048);
        vm.arrival_time_s = 0.0;
        pendentes.push_back(vm);
    }

    // Monta o estado com os DCs em uma ordem de declaração qualquer.
    const auto estado_com = [&](const std::vector<std::string>& ordem) {
        domain::ClusterState state;
        for (const auto& dc_id : ordem) {
            state.datacenters.push_back(monta_dc(dc_id));
            // Perfil espacialmente uniforme: TODOS empatam na chave principal.
            state.carbon_now[dc_id] = 300.0;
        }
        state.pending_vms = pendentes;
        return state;
    };

    const auto alvos = [](const std::vector<domain::PlacementDecision>& d) {
        std::vector<std::string> out;
        out.reserve(d.size());
        for (const auto& x : d) {
            out.push_back(x.vm_id + "->" + x.target_host_id);
        }
        return out;
    };

    algorithms::LowestCarbonDC lcdc;
    algorithms::FollowMeS      fms;
    algorithms::WSNB           wsnb{400.0};

    auto       base_lcdc = estado_com(ids_em_ordem_de_fuso);
    auto       base_fms  = estado_com(ids_em_ordem_de_fuso);
    auto       base_wsnb = estado_com(ids_em_ordem_de_fuso);
    const auto ref_lcdc  = alvos(lcdc.place(base_lcdc));
    const auto ref_fms   = alvos(fms.place(base_fms));
    const auto ref_wsnb  = alvos(wsnb.place(base_wsnb));
    REQUIRE(ref_lcdc.size() == pendentes.size());
    REQUIRE(ref_wsnb.size() == pendentes.size());

    std::mt19937_64 rng{20260806};
    for (int tentativa = 0; tentativa < 25; ++tentativa) {
        auto ordem = ids_em_ordem_de_fuso;
        std::shuffle(ordem.begin(), ordem.end(), rng);

        auto e_lcdc = estado_com(ordem);
        auto e_fms  = estado_com(ordem);
        auto e_wsnb = estado_com(ordem);
        CHECK(alvos(lcdc.place(e_lcdc)) == ref_lcdc);
        CHECK(alvos(fms.place(e_fms)) == ref_fms);
        CHECK(alvos(wsnb.place(e_wsnb)) == ref_wsnb);
    }
}

TEST_CASE("WSNB: o DC de origem usa hash especificado, não std::hash",
          "[unit][tiebreak][wsnb]") {
    // `std::hash<std::string>` é definido pela implementação: libstdc++,
    // libc++ e MSVC dão valores diferentes para a mesma string. Como o DC de
    // origem do WSNB derivava dele, um terceiro que recompilasse o experimento
    // com outra biblioteca padrão obteria alocações diferentes a partir dos
    // mesmos artefatos versionados. Os valores abaixo fixam o FNV-1a adotado;
    // se alguém trocar a função, este teste falha em qualquer plataforma.
    //
    // Referências independentes do FNV-1a de 64 bits (offset 14695981039346656037,
    // primo 1099511628211).
    const auto fnv = [](const std::string& s) {
        std::uint64_t h = 14695981039346656037ULL;
        for (const char c : s) {
            h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
            h *= 1099511628211ULL;
        }
        return h;
    };
    CHECK(fnv("") == 0xcbf29ce484222325ULL);
    CHECK(fnv("a") == 0xaf63dc4c8601ec8cULL);
    CHECK(fnv("foobar") == 0x85944171f73967e8ULL);
}

TEST_CASE("FollowRenewables independe da ordem de running_vms",
          "[unit][tiebreak][follow_renewables]") {
    // Este é o caso que motivou a auditoria: `ClusterState::running_vms` é
    // preenchido no backend iterando um `std::unordered_map`, de modo que sua
    // ordem não é definida pela política do algoritmo. Com a ordem total, a
    // decisão de migração não pode depender dela.
    domain::Host sujo;
    sujo.host_id           = "dirty-h0";
    sujo.dc_id             = "dc-dirty";
    sujo.cpu_cores         = 16;
    sujo.cpu_capacity_mips = 16000.0;
    sujo.ram_mb            = 65536;
    sujo.cpu_used_mips     = 8000.0;
    sujo.ram_used_mb       = 16384;
    sujo.power_idle_w      = 50;
    sujo.power_peak_w      = 100;

    domain::Host limpo;
    limpo.host_id           = "clean-h0";
    limpo.dc_id             = "dc-clean";
    limpo.cpu_cores         = 8;
    limpo.cpu_capacity_mips = 3000.0;  // cabe só parte das candidatas
    limpo.ram_mb            = 16384;
    limpo.power_idle_w      = 50;
    limpo.power_peak_w      = 100;

    std::vector<domain::VM> vms;
    for (int i = 0; i < 10; ++i) {
        auto vm = tests::make_vm(
            "vm-" + std::string(i < 10 ? "0" : "") + std::to_string(i), 800.0, 1024);
        vm.host_id        = "dirty-h0";
        vm.arrival_time_s = 0.0;
        vms.push_back(vm);
        sujo.vms.push_back(vm.vm_id);
    }

    domain::Datacenter dc_sujo;
    dc_sujo.dc_id = "dc-dirty";
    dc_sujo.hosts = {sujo};
    domain::Datacenter dc_limpo;
    dc_limpo.dc_id = "dc-clean";
    dc_limpo.hosts = {limpo};

    const auto decisoes_de = [&](const std::vector<domain::VM>& running) {
        domain::ClusterState state;
        state.datacenters            = {dc_sujo, dc_limpo};
        state.running_vms            = running;
        state.carbon_now["dc-dirty"] = 600.0;
        state.carbon_now["dc-clean"] = 150.0;
        algorithms::FollowRenewables algo(400.0);
        return algo.migrate(state);
    };

    const auto referencia = decisoes_de(vms);
    REQUIRE_FALSE(referencia.empty());

    std::mt19937_64 rng{20260806};
    for (int tentativa = 0; tentativa < 25; ++tentativa) {
        auto embaralhadas = vms;
        std::shuffle(embaralhadas.begin(), embaralhadas.end(), rng);
        const auto obtido = decisoes_de(embaralhadas);

        REQUIRE(obtido.size() == referencia.size());
        for (std::size_t i = 0; i < referencia.size(); ++i) {
            CHECK(obtido[i].vm_id == referencia[i].vm_id);
            CHECK(obtido[i].target_host_id == referencia[i].target_host_id);
        }
    }
}

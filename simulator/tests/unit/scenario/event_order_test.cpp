// Testes da ordem total da fila de eventos do workload.
//
// Esta é a ordenação mais consequente do simulador: a sequência dos eventos
// Submit define a ordem de `ClusterState::pending_vms`, que é a ordem de
// alocação de nove dos onze algoritmos: todos os que não reordenam a fila.
// Ordenar apenas por `t_seconds` deixava a ordem relativa dos eventos
// simultâneos a cargo da implementação de `std::sort`, e nos traces usados a
// esmagadora maioria dos eventos EMPATA nessa chave (o azure_2020 concentra
// 10.000 chegadas em 16 instantes distintos).
//
// A especificação adotada é (t_seconds asc, kind asc, vm_id asc).

#include "algosim/scenario/scenario_builder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace algosim;

namespace {

std::filesystem::path escrever(const std::filesystem::path& nome,
                               const std::string&           conteudo) {
    auto caminho = std::filesystem::temp_directory_path() / nome;
    std::ofstream out{caminho};
    out << conteudo;
    return caminho;
}

// Trace com todas as chegadas no MESMO instante e em ordem de identificador
// deliberadamente invertida no arquivo, o pior caso para a chave principal.
std::filesystem::path trace_simultaneo() {
    std::ostringstream conteudo;
    for (int i = 19; i >= 0; --i) {
        conteudo << "vmdispatcher sendVM 0.0 vm_"
                 << (i < 10 ? "0" : "") << i << " 1 5000.0\n";
    }
    conteudo << "vmdispatcher stop 300.0\n";
    return escrever("algosim_event_order_trace.txt", conteudo.str());
}

std::filesystem::path yaml_para(const std::string& trace_path) {
    std::ostringstream conteudo;
    conteudo << R"(
scenario:
  name: event_order_test
  duration_seconds: 1000
  dt_seconds: 60
  seed: 42
  start_datetime: "2020-01-01T00:00:00"

datacenters:
  - id: dc1
    name: "DC1"
    pue: 1.2
    carbon_series_id: dc1
    host_template:
      cpu_cores: 8
      cpu_capacity_mips: 10000
      ram_mb: 16384
      net_bw_mbps: 1000
      power_idle_w: 50
      power_peak_w: 100
    host_count: 5

carbon:
  - { series_id: dc1, kind: flat, value_gco2_per_kwh: 100.0 }

workload:
  kind: trace_file
  path: ")" << trace_path << R"("
  format: cnemesis
  n_vms: 0
  ram_mb: 4096
  image_size_mb: 4096
  dirty_rate_mbps: 20
  lifetime_mean_seconds: 500
  max_demand_mips: 100000
  rescale_arrival_seconds: 0
)";
    return escrever("algosim_event_order_scenario.yaml", conteudo.str());
}

}  // namespace

TEST_CASE("eventos simultâneos são ordenados por identificador",
          "[unit][scenario][ordem-eventos]") {
    const auto spec = scenario::ScenarioBuilder::build(yaml_para(trace_simultaneo().string()));

    std::vector<std::string> submits_em_t0;
    for (const auto& ev : spec.events) {
        if (ev.kind == domain::EventKind::Submit && ev.t_seconds == 0.0) {
            submits_em_t0.push_back(ev.vm.vm_id);
        }
    }
    REQUIRE(submits_em_t0.size() == 20);

    // O arquivo lista os identificadores em ordem decrescente; a fila tem de
    // sair em ordem crescente, independentemente disso.
    auto esperado = submits_em_t0;
    std::sort(esperado.begin(), esperado.end());
    CHECK(submits_em_t0 == esperado);
}

TEST_CASE("a fila de eventos está em ordem total não decrescente",
          "[unit][scenario][ordem-eventos]") {
    const auto spec = scenario::ScenarioBuilder::build(yaml_para(trace_simultaneo().string()));
    REQUIRE(spec.events.size() > 1);

    for (std::size_t i = 1; i < spec.events.size(); ++i) {
        const auto& anterior = spec.events[i - 1];
        const auto& atual    = spec.events[i];

        // Nenhum par consecutivo pode estar fora de ordem sob a chave
        // (t_seconds, kind, vm_id), e nenhum par distinto pode ser
        // equivalente; é o que torna a ordem independente da implementação
        // de ordenação.
        if (anterior.t_seconds == atual.t_seconds && anterior.kind == atual.kind) {
            CHECK(anterior.vm.vm_id < atual.vm.vm_id);
        } else if (anterior.t_seconds == atual.t_seconds) {
            CHECK(static_cast<int>(anterior.kind) < static_cast<int>(atual.kind));
        } else {
            CHECK(anterior.t_seconds < atual.t_seconds);
        }
    }
}

TEST_CASE("Submit precede Terminate no mesmo instante",
          "[unit][scenario][ordem-eventos]") {
    // Ordem causalmente correta: uma VM só pode terminar depois de submetida.
    const auto spec = scenario::ScenarioBuilder::build(yaml_para(trace_simultaneo().string()));

    bool viu_terminate_em_t0 = false;
    for (const auto& ev : spec.events) {
        if (ev.t_seconds != 0.0) {
            break;
        }
        if (ev.kind == domain::EventKind::Terminate) {
            viu_terminate_em_t0 = true;
        } else if (ev.kind == domain::EventKind::Submit) {
            CHECK_FALSE(viu_terminate_em_t0);
        }
    }
}

TEST_CASE("linha de trace malformada interrompe a carga",
          "[unit][scenario][finitude]") {
    // Antes, `if (iss.fail()) continue;` descartava a linha em silêncio: o
    // cenário rodaria com uma VM a menos e nenhum artefato registraria isso.
    // Para um experimento dirigido por trace, alterar a carga de trabalho sem
    // rastro é inaceitável: a carga tem de falhar.
    const auto trace = escrever("algosim_event_order_nan.txt",
                                "vmdispatcher sendVM 0.0 vm_00 1 nan\n"
                                "vmdispatcher sendVM 0.0 vm_01 1 5000.0\n"
                                "vmdispatcher stop 300.0\n");

    CHECK_THROWS(scenario::ScenarioBuilder::build(yaml_para(trace.string())));
}

TEST_CASE("intensidade de carbono não finita é rejeitada na carga",
          "[unit][scenario][finitude]") {
    // Pré-condição declarada em algosim/algorithms/tiebreak.hpp: um NaN na
    // intensidade violaria a ordem estrita fraca exigida por std::sort, cujo
    // resultado é comportamento indefinido, não apenas ordem errada.
    const auto trace = trace_simultaneo();
    auto       yaml  = yaml_para(trace.string());

    // Reescreve o YAML trocando a intensidade por `.nan` (NaN do YAML).
    std::ostringstream conteudo;
    {
        std::ifstream in{yaml};
        std::string   linha;
        while (std::getline(in, linha)) {
            if (linha.find("value_gco2_per_kwh") != std::string::npos) {
                linha = "  - { series_id: dc1, kind: flat, value_gco2_per_kwh: .nan }";
            }
            conteudo << linha << "\n";
        }
    }
    const auto yaml_nan = escrever("algosim_event_order_carbono_nan.yaml", conteudo.str());

    CHECK_THROWS(scenario::ScenarioBuilder::build(yaml_nan));
}

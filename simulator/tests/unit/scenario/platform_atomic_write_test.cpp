// Regression test: a transient out-of-space condition on /tmp once left a
// TRUNCATED platform XML at the final path, and 37 consecutive runs died at
// parse time. The generator must
// (a) produce a complete document ending in </platform> and (b) write
// atomically (temp file + rename), so a failed write never leaves a partial
// file at the destination. The ENOSPC path itself cannot be forced portably
// in a unit test; this pins the contract that IS testable.

#include "algosim/scenario/platform_generator.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace algosim;

namespace {

scenario::ScenarioSpec two_dc_spec(const std::string& name) {
    scenario::ScenarioSpec spec;
    spec.name = name;
    for (int i = 0; i < 2; ++i) {
        domain::Datacenter dc;
        dc.dc_id     = "dc" + std::to_string(i);
        dc.longitude = 3.0 * i;
        domain::Host h;
        h.host_id           = dc.dc_id + "-h000";
        h.dc_id             = dc.dc_id;
        h.cpu_cores         = 4;
        h.cpu_capacity_mips = 1000.0;
        h.ram_mb            = 1024;
        h.net_bw_mbps       = 1000.0;
        h.power_idle_w      = 50.0;
        h.power_peak_w      = 100.0;
        dc.hosts.push_back(h);
        spec.datacenters.push_back(dc);
    }
    return spec;
}

}  // namespace

TEST_CASE("generated platform XML is complete and no temp file remains",
          "[unit][scenario][platform][atomic]") {
    const auto out_dir = std::filesystem::temp_directory_path()
                         / "algosim_atomic_write_test";
    std::filesystem::remove_all(out_dir);

    const auto spec = two_dc_spec("atomic_test");
    const auto path = scenario::generate_platform_xml(spec, out_dir);

    std::ifstream in{path};
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto content = ss.str();
    REQUIRE(!content.empty());
    CHECK(content.rfind("</platform>\n") == content.size() - 12);

    // Atomicidade: nenhum arquivo temporario sobra no diretorio.
    std::size_t files = 0;
    for (const auto& e : std::filesystem::directory_iterator(out_dir)) {
        (void)e;
        ++files;
    }
    CHECK(files == 1);
}

// As duas propriedades de que a execucao paralela depende. O diretorio de
// saida e compartilhado entre processos (/tmp/algosim) e a campanha canonica
// roda varios processos ao mesmo tempo: o destino precisa ser exclusivo de
// cada processo, e o conteudo precisa depender apenas da especificacao.

TEST_CASE("o caminho final do XML e exclusivo do processo",
          "[unit][scenario][platform][paralelo]") {
    const auto out_dir = std::filesystem::temp_directory_path()
                         / "algosim_pid_path_test";
    std::filesystem::remove_all(out_dir);

    const auto path = scenario::generate_platform_xml(two_dc_spec("pid_test"),
                                                      out_dir);
    const auto nome = path.filename().string();
    CHECK(nome.find("." + std::to_string(::getpid()) + ".xml") != std::string::npos);
    CHECK(nome != "pid_test_platform.xml");
}

TEST_CASE("o conteudo do XML e funcao apenas da especificacao",
          "[unit][scenario][platform][paralelo]") {
    const auto base = std::filesystem::temp_directory_path()
                      / "algosim_pure_fn_test";
    std::filesystem::remove_all(base);

    const auto spec = two_dc_spec("pure_fn_test");
    const auto a    = scenario::generate_platform_xml(spec, base / "a");
    const auto b    = scenario::generate_platform_xml(spec, base / "b");

    const auto ler = [](const std::filesystem::path& p) {
        std::ifstream      in{p};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    CHECK(ler(a) == ler(b));
}

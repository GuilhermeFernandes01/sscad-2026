#include "algosim/scenario/platform_generator.hpp"
#include "algosim/scenario/scenario_builder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace algosim;

namespace {

std::filesystem::path write_yaml(const std::string& name,
                                 const std::string& network_block) {
    auto path = std::filesystem::temp_directory_path()
                / ("algosim_network_test_" + name + ".yaml");
    std::ofstream out{path};
    out << R"(
scenario:
  name: net_test_)" << name << R"(
  duration_seconds: 600
  dt_seconds: 60
  seed: 42
  start_datetime: "2020-01-01T00:00:00"
)" << network_block << R"(
datacenters:
  - id: dc1
    name: "DC1"
    latitude: 0.0
    longitude: 0.0
    pue: 1.2
    carbon_series_id: dc1
    host_template:
      cpu_cores: 8
      cpu_capacity_mips: 10000
      ram_mb: 16384
      net_bw_mbps: 1000
      power_idle_w: 50
      power_peak_w: 100
    host_count: 2
  - id: dc2
    name: "DC2"
    latitude: 0.0
    longitude: 3.0
    pue: 1.2
    carbon_series_id: dc2
    host_template:
      cpu_cores: 8
      cpu_capacity_mips: 10000
      ram_mb: 16384
      net_bw_mbps: 1000
      power_idle_w: 50
      power_peak_w: 100
    host_count: 2

carbon:
  - { series_id: dc1, kind: flat, value_gco2_per_kwh: 100.0 }
  - { series_id: dc2, kind: flat, value_gco2_per_kwh: 200.0 }

workload:
  kind: poisson_diurnal
  n_vms: 2
  mean_arrival_rate: 0.1
  demand_mean_mips: 1000
  ram_mb: 1024
)";
    return path;
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in{p};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("network profile defaults preserve current behaviour",
          "[unit][scenario][network]") {
    auto yaml = write_yaml("defaults", "");
    auto spec = scenario::ScenarioBuilder::build(yaml);

    CHECK(spec.network.inter_dc_bw_mbps == 1000.0);
    CHECK(spec.network.inter_dc_latency_factor == 1.0);
}

TEST_CASE("network section overrides inter-DC bandwidth and latency factor",
          "[unit][scenario][network]") {
    auto yaml = write_yaml("restricted", R"(
network:
  inter_dc_bw_mbps: 100
  inter_dc_latency_factor: 5.0
)");
    auto spec = scenario::ScenarioBuilder::build(yaml);

    CHECK(spec.network.inter_dc_bw_mbps == 100.0);
    CHECK(spec.network.inter_dc_latency_factor == 5.0);
}

TEST_CASE("generated platform XML applies the network profile",
          "[unit][scenario][network]") {
    auto yaml = write_yaml("xmlgen", R"(
network:
  inter_dc_bw_mbps: 100
  inter_dc_latency_factor: 5.0
)");
    auto spec = scenario::ScenarioBuilder::build(yaml);

    const auto out_dir = std::filesystem::temp_directory_path()
                         / "algosim_network_test_xml";
    const auto xml_path = scenario::generate_platform_xml(spec, out_dir);
    const auto xml      = read_file(xml_path);

    // dc1 (0,0) to dc2 (0,3): dist = 3 deg -> base latency = max(1, 1.5) = 1.5ms,
    // scaled by factor 5 -> 7.5ms.
    CHECK(xml.find("bandwidth=\"100Mbps\"") != std::string::npos);
    CHECK(xml.find("latency=\"7.5ms\"") != std::string::npos);
}

TEST_CASE("generated platform XML defaults to 1 Gbps inter-DC",
          "[unit][scenario][network]") {
    auto yaml = write_yaml("xmldefault", "");
    auto spec = scenario::ScenarioBuilder::build(yaml);

    const auto out_dir = std::filesystem::temp_directory_path()
                         / "algosim_network_test_xml_default";
    const auto xml_path = scenario::generate_platform_xml(spec, out_dir);
    const auto xml      = read_file(xml_path);

    CHECK(xml.find("bandwidth=\"1000Mbps\"") != std::string::npos);
    CHECK(xml.find("latency=\"1.5ms\"") != std::string::npos);
}

// Regression test: carbon CSV series were once anchored at the SCENARIO
// start_datetime instead of the DATA epoch, so changing start_datetime never
// changed which slice of the series the simulation consumed (realjan cells
// were bit-identical to realjun). The series must be anchored at
// `data_start_datetime` (default 2020-01-01T00:00:00, the epoch of the
// LowCarbonCloud 2020 exports).

#include "algosim/scenario/scenario_builder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace algosim;

namespace {

std::filesystem::path write_hourly_csv(int hours) {
    auto path = std::filesystem::temp_directory_path() / "algosim_anchor_test.csv";
    std::ofstream out{path};
    out << "t_seconds,gco2_per_kwh\n";
    for (int h = 0; h < hours; ++h) {
        out << h * 3600 << ',' << static_cast<double>(h) << '\n';  // valor = hora
    }
    return path;
}

std::filesystem::path write_yaml(const std::string& name,
                                 const std::string& start_datetime,
                                 const std::string& csv_path,
                                 const std::string& extra_carbon_fields = "") {
    auto path = std::filesystem::temp_directory_path()
                / ("algosim_anchor_test_" + name + ".yaml");
    std::ofstream out{path};
    out << R"(
scenario:
  name: anchor_)" << name << R"(
  duration_seconds: 600
  dt_seconds: 60
  seed: 42
  start_datetime: ")" << start_datetime << R"("

datacenters:
  - id: dc1
    name: "DC1"
    pue: 1.2
    carbon_series_id: dc1
    host_template:
      cpu_cores: 8
      cpu_capacity_mips: 10000
      ram_mb: 16384
      power_idle_w: 50
      power_peak_w: 100
    host_count: 2

carbon:
  - { series_id: dc1, kind: csv, path: ")" << csv_path
        << R"(", step_seconds: 3600)" << extra_carbon_fields << R"( }

workload:
  kind: poisson_diurnal
  n_vms: 2
  mean_arrival_rate: 0.1
  demand_mean_mips: 1000
  ram_mb: 1024
)";
    return path;
}

}  // namespace

TEST_CASE("carbon csv is anchored at the data epoch, not the scenario start",
          "[unit][scenario][carbon][anchor]") {
    const auto csv = write_hourly_csv(72);

    auto spec_jan1 = scenario::ScenarioBuilder::build(
        write_yaml("jan1", "2020-01-01T00:00:00", csv.string()));
    auto spec_jan2 = scenario::ScenarioBuilder::build(
        write_yaml("jan2", "2020-01-02T00:00:00", csv.string()));

    const auto& s1 = spec_jan1.carbon_series.at("dc1");
    const auto& s2 = spec_jan2.carbon_series.at("dc1");

    // No inicio da simulacao, jan1 le a hora 0 do dado e jan2 le a hora 24.
    CHECK(s1.at(spec_jan1.start_datetime) == 0.0);
    CHECK(s2.at(spec_jan2.start_datetime) == 24.0);
}

TEST_CASE("data_start_datetime can override the default epoch",
          "[unit][scenario][carbon][anchor]") {
    const auto csv = write_hourly_csv(72);

    auto spec = scenario::ScenarioBuilder::build(write_yaml(
        "custom", "2021-06-01T02:00:00", csv.string(),
        ", data_start_datetime: \"2021-06-01T00:00:00\""));

    // Epoca do dado = 2021-06-01T00:00; simulacao comeca 2h depois -> hora 2.
    CHECK(spec.carbon_series.at("dc1").at(spec.start_datetime) == 2.0);
}

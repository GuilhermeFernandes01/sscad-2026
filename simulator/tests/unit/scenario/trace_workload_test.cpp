#include "algosim/scenario/scenario_builder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace algosim;

namespace {

std::filesystem::path create_test_trace(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / "algosim_test_trace.txt";
    std::ofstream out{path};
    out << content;
    return path;
}

std::filesystem::path create_test_yaml(const std::string& trace_path,
                                        int n_vms,
                                        double max_demand,
                                        double rescale) {
    auto path = std::filesystem::temp_directory_path() / "algosim_test_scenario.yaml";
    std::ofstream out{path};
    out << R"(
scenario:
  name: trace_test
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
  n_vms: )" << n_vms << R"(
  ram_mb: 4096
  image_size_mb: 4096
  dirty_rate_mbps: 20
  lifetime_mean_seconds: 500
  max_demand_mips: )" << max_demand << R"(
  rescale_arrival_seconds: )" << rescale << "\n";
    return path;
}

}  // namespace

TEST_CASE("trace_file workload parses cnemesis format",
          "[unit][scenario][trace]") {
    auto trace = create_test_trace(
        "vmdispatcher sendVM 0.0 vm_0 1 5000.0\n"
        "vmdispatcher sendVM 100.0 vm_1 2 8000.0\n"
        "vmdispatcher sendVM 200.0 vm_2 1 3000.0\n"
        "vmdispatcher stop 300.0\n");

    auto yaml = create_test_yaml(trace.string(), 0, 100000, 0);
    auto spec = scenario::ScenarioBuilder::build(yaml);

    int submits = 0;
    int terminates = 0;
    for (const auto& ev : spec.events) {
        if (ev.kind == domain::EventKind::Submit) ++submits;
        if (ev.kind == domain::EventKind::Terminate) ++terminates;
    }
    CHECK(submits == 3);
    CHECK(terminates == 3);
}

TEST_CASE("trace_file respects n_vms subsample",
          "[unit][scenario][trace]") {
    auto trace = create_test_trace(
        "vmdispatcher sendVM 0.0 vm_0 1 5000.0\n"
        "vmdispatcher sendVM 100.0 vm_1 2 8000.0\n"
        "vmdispatcher sendVM 200.0 vm_2 1 3000.0\n");

    auto yaml = create_test_yaml(trace.string(), 2, 100000, 0);
    auto spec = scenario::ScenarioBuilder::build(yaml);

    int submits = 0;
    for (const auto& ev : spec.events) {
        if (ev.kind == domain::EventKind::Submit) ++submits;
    }
    CHECK(submits == 2);
}

TEST_CASE("trace_file clamps demand at max_demand_mips",
          "[unit][scenario][trace]") {
    auto trace = create_test_trace(
        "vmdispatcher sendVM 0.0 vm_0 1 50000.0\n");

    auto yaml = create_test_yaml(trace.string(), 0, 10000, 0);
    auto spec = scenario::ScenarioBuilder::build(yaml);

    bool found = false;
    for (const auto& ev : spec.events) {
        if (ev.kind == domain::EventKind::Submit) {
            CHECK(ev.vm.cpu_demand_mips <= 10000.0);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("trace_file rescales arrival times",
          "[unit][scenario][trace]") {
    auto trace = create_test_trace(
        "vmdispatcher sendVM 0.0 vm_0 1 5000.0\n"
        "vmdispatcher sendVM 600000.0 vm_1 1 5000.0\n");

    auto yaml = create_test_yaml(trace.string(), 0, 100000, 500);
    auto spec = scenario::ScenarioBuilder::build(yaml);

    double max_arrival = 0;
    for (const auto& ev : spec.events) {
        if (ev.kind == domain::EventKind::Submit) {
            max_arrival = std::max(max_arrival, ev.t_seconds);
        }
    }
    CHECK(max_arrival <= 500.1);
}

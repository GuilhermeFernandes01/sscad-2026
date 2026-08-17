#pragma once

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/placement_algorithm.hpp"
#include "algosim/domain/cluster_state.hpp"
#include "algosim/metrics/metric_collector.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <memory>
#include <string>

namespace algosim::backend {

// Version string of the SimGrid library this backend was linked against
// (e.g. "4.1.1"), for run provenance.
[[nodiscard]] std::string simgrid_version_string();

// Result of a full simulation run.
struct DecisionRecord {
    double      t_sim;
    std::string kind;   // "placement" or "migration"
    domain::PlacementDecision  placement;
    domain::MigrationDecision  migration;
};

struct RunResult {
    std::vector<metrics::MetricTick> time_series;
    metrics::MetricSummary           summary;
    std::vector<DecisionRecord>      decisions;
};

// Wrapper around simgrid::s4u that runs a complete experiment.
//
// Because SimGrid operations (VM creation, migration) are "simcalls" that
// must execute inside an actor context, the backend encapsulates the entire
// run loop in an internal orchestrator actor. The caller provides the
// algorithms and the collector; the backend drives the simulation from
// inside SimGrid, calling `placement.place()` and `migration->migrate()` at
// each tick.
//
// All SimGrid includes are confined to the .cpp to keep the rest of the
// codebase free of SimGrid coupling.
class SimGridBackend {
public:
    explicit SimGridBackend(const scenario::ScenarioSpec& spec);
    SimGridBackend(const SimGridBackend&)            = delete;
    SimGridBackend(SimGridBackend&&)                 = delete;
    SimGridBackend& operator=(const SimGridBackend&) = delete;
    SimGridBackend& operator=(SimGridBackend&&)      = delete;
    ~SimGridBackend();

    // Run the full simulation loop. Blocks until the simulation ends.
    //
    // Internally creates a SimGrid orchestrator actor that:
    //   1. spawns workload actors for submitted VMs
    //   2. calls placement.place() on each tick with pending VMs
    //   3. calls migration->migrate() every migration_interval
    //   4. sleeps for dt_seconds between ticks
    //   5. records metrics each tick
    //
    // `migration` may be nullptr (no migration). `writer` may be nullptr
    // (no incremental decision output).
    RunResult run(algorithms::PlacementAlgorithm& placement,
                  algorithms::MigrationAlgorithm* migration);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace algosim::backend

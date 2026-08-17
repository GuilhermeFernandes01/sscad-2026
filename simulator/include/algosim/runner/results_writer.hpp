#pragma once

#include "algosim/domain/decisions.hpp"
#include "algosim/metrics/metric_collector.hpp"
#include "algosim/runner/build_info.hpp"
#include "algosim/scenario/scenario_spec.hpp"

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unordered_map>

namespace algosim::runner {

// Canonicalises run outputs into results/{run_id}/:
//     manifest.json, config.yaml, metrics.csv, decisions.jsonl, summary.json
//
// All writes are incremental where it matters (decisions.jsonl) so large
// runs don't accumulate state in memory.
class ResultsWriter {
public:
    using AlgoParams = std::unordered_map<std::string, std::string>;

    ResultsWriter(const std::filesystem::path&   output_root,
                  const std::string&             run_id,
                  const scenario::ScenarioSpec&  spec,
                  const std::string&             placement_algo,
                  const std::string&             migration_algo,
                  const AlgoParams&              algo_params = {});

    void write_manifest(const BuildInfo& build);
    void freeze_config();
    void append_decisions(std::span<const domain::PlacementDecision> decisions, double t_sim);
    void append_decisions(std::span<const domain::MigrationDecision> decisions, double t_sim);

    void write_metrics_csv(const std::vector<metrics::MetricTick>& ticks);
    void write_summary(const metrics::MetricSummary& summary);

    [[nodiscard]] const std::filesystem::path& run_dir() const noexcept { return run_dir_; }

private:
    std::filesystem::path         output_root_;
    std::string                   run_id_;
    const scenario::ScenarioSpec& spec_;
    std::string                   placement_algo_;
    std::string                   migration_algo_;
    AlgoParams                    algo_params_;
    std::filesystem::path         run_dir_;
    std::ofstream                 decisions_stream_;
};

[[nodiscard]] std::string make_run_id(const std::string& placement_algo,
                                      const std::string& migration_algo,
                                      const std::string& scenario_stem,
                                      std::uint64_t      seed);

}  // namespace algosim::runner

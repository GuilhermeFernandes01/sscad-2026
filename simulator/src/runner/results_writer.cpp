#include "algosim/runner/results_writer.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace algosim::runner {

namespace {

[[nodiscard]] std::string iso_timestamp_now() {
    const auto now   = std::chrono::system_clock::now();
    const auto tt    = std::chrono::system_clock::to_time_t(now);
    std::tm    tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &tt);
#else
    gmtime_r(&tt, &tm_utc);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

[[nodiscard]] std::string compact_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm    tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &tt);
#else
    gmtime_r(&tt, &tm_utc);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_utc, "%Y%m%dT%H%M%S");
    return ss.str();
}

}  // namespace

std::string make_run_id(const std::string& placement_algo,
                        const std::string& migration_algo,
                        const std::string& scenario_stem,
                        std::uint64_t      seed) {
    std::ostringstream ss;
    ss << placement_algo << '_' << migration_algo << '_' << scenario_stem << '_'
       << "seed" << seed << '_' << compact_timestamp();
    return ss.str();
}

ResultsWriter::ResultsWriter(const std::filesystem::path&  output_root,
                             const std::string&            run_id,
                             const scenario::ScenarioSpec& spec,
                             const std::string&            placement_algo,
                             const std::string&            migration_algo,
                             const AlgoParams&             algo_params)
    : output_root_{output_root},
      run_id_{run_id},
      spec_{spec},
      placement_algo_{placement_algo},
      migration_algo_{migration_algo},
      algo_params_{algo_params} {
    run_dir_ = output_root_ / run_id_;
    std::filesystem::create_directories(run_dir_);
    decisions_stream_.open(run_dir_ / "decisions.jsonl", std::ios::out | std::ios::trunc);
}

void ResultsWriter::write_manifest(const BuildInfo& build) {
    nlohmann::json j;
    j["run_id"]                   = run_id_;
    j["algosim_version"]          = "0.1.0";
    j["git_sha"]                  = build.git_sha;
    j["git_dirty"]                = build.git_dirty;
    j["simgrid_version"]          = build.simgrid_version;
#if defined(__GNUC__) && !defined(__clang__)
    j["compiler"] = std::string{"g++ "} + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#elif defined(__clang__)
    j["compiler"] = std::string{"clang++ "} + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#else
    j["compiler"] = "unknown";
#endif
    j["seed"]                     = spec_.seed;
    j["algorithm"]["placement"]   = placement_algo_;
    j["algorithm"]["migration"]   = migration_algo_;
    if (!algo_params_.empty()) {
        j["algorithm"]["params"]  = algo_params_;
    }
    j["scenario_path"]            = spec_.source_config_path;
    j["scenario_name"]            = spec_.name;
    j["started_at"]               = iso_timestamp_now();
    j["duration_seconds"]         = spec_.duration_seconds;
    j["dt_seconds"]               = spec_.dt_seconds;
    j["migration_interval_seconds"] = spec_.migration_interval_seconds;
    j["carbon_forecast_hours"]      = spec_.carbon_forecast_hours;

    std::size_t host_count = 0;
    for (const auto& dc : spec_.datacenters) {
        host_count += dc.hosts.size();
    }
    j["dc_count"]   = spec_.datacenters.size();
    j["host_count"] = host_count;
    j["vm_count"]   = spec_.events.size() / 2;  // Submit + optional Terminate

    std::ofstream out{run_dir_ / "manifest.json"};
    out << j.dump(2);
}

void ResultsWriter::freeze_config() {
    const std::filesystem::path src{spec_.source_config_path};
    if (!std::filesystem::exists(src)) {
        return;
    }
    std::filesystem::copy_file(src, run_dir_ / "config.yaml",
                               std::filesystem::copy_options::overwrite_existing);
}

void ResultsWriter::append_decisions(std::span<const domain::PlacementDecision> decisions,
                                     double                                     t_sim) {
    for (const auto& d : decisions) {
        nlohmann::json j;
        j["t"]      = t_sim;
        j["kind"]   = "placement";
        j["vm_id"]  = d.vm_id;
        j["src"]    = nullptr;
        j["dst"]    = d.target_host_id;
        j["reason"] = d.reason;
        decisions_stream_ << j.dump() << '\n';
    }
}

void ResultsWriter::append_decisions(std::span<const domain::MigrationDecision> decisions,
                                     double                                     t_sim) {
    for (const auto& d : decisions) {
        nlohmann::json j;
        j["t"]      = t_sim;
        j["kind"]   = "migration";
        j["vm_id"]  = d.vm_id;
        j["src"]    = d.source_host_id;
        j["dst"]    = d.target_host_id;
        j["reason"] = d.reason;
        decisions_stream_ << j.dump() << '\n';
    }
}

void ResultsWriter::write_metrics_csv(const std::vector<metrics::MetricTick>& ticks) {
    std::ofstream out{run_dir_ / "metrics.csv"};
    out << "t,total_kwh,total_gco2,active_hosts,mean_util,p95_util,"
           "migrations,mig_bytes,sla_violations,unplaced_vms,algo_wall_us\n";
    for (const auto& t : ticks) {
        out << t.t_seconds << ',' << t.total_kwh << ',' << t.total_gco2 << ','
            << t.active_hosts << ',' << t.mean_util << ',' << t.p95_util << ','
            << t.migrations << ',' << t.mig_bytes << ',' << t.sla_violations << ','
            << t.unplaced_vms << ',' << t.algo_wall_us << '\n';
    }
}

void ResultsWriter::write_summary(const metrics::MetricSummary& summary) {
    nlohmann::json j;
    j["total_kwh"]             = summary.total_kwh;
    j["total_gco2"]            = summary.total_gco2;
    j["total_migrations"]      = summary.total_migrations;
    j["total_mig_bytes"]       = summary.total_mig_bytes;
    j["mean_active_hosts"]     = summary.mean_active_hosts;
    j["final_sla_violations"]  = summary.final_sla_violations;
    j["total_unplaced_vms"]    = summary.total_unplaced_vms;
    j["algo_wall_total_us"]    = summary.algo_wall_total_us;
    j["finished_at"]           = iso_timestamp_now();

    std::ofstream out{run_dir_ / "summary.json"};
    out << j.dump(2);
}

}  // namespace algosim::runner

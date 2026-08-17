// CLI entrypoint for the algosim runner.
//
// Usage:
//     algosim run <scenario.yaml> [--placement <name>] [--migration <name>]
//                                 [--seed <n>] [--output <dir>] [--verbose]
//     algosim list
//
// The `run` subcommand is only meaningful when the SimGrid backend is built
// into the binary (ALGOSIM_ENABLE_SIMGRID=ON). The `list` subcommand works
// regardless and prints the registered algorithms.

#include "algosim/algorithms/registry.hpp"
#include "algosim/scenario/scenario_builder.hpp"

#ifdef ALGOSIM_WITH_SIMGRID
#    include "algosim/backend/simgrid_backend.hpp"
#endif

#include "algosim/metrics/metric_collector.hpp"
#include "algosim/runner/results_writer.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct CliArgs {
    std::string           subcommand;
    std::filesystem::path config_path;
    std::string           placement_algo = "first_fit";
    std::string           migration_algo = "none";
    std::optional<std::uint64_t> seed_override;
    std::filesystem::path output_root = "results";
    bool                  verbose     = false;
    std::unordered_map<std::string, std::string> algo_params;
};

[[nodiscard]] int usage() {
    std::cerr << "usage: algosim <run|list> [options]\n"
                 "  run <scenario.yaml> [--placement <name>] [--migration <name>]\n"
                 "                      [--seed <n>] [--output <dir>] [--verbose]\n"
                 "                      [--param key=value ...]\n"
                 "  list\n";
    return 2;
}

[[nodiscard]] CliArgs parse_cli(int argc, char** argv) {
    if (argc < 2) {
        std::exit(usage());
    }
    CliArgs args;
    args.subcommand = argv[1];
    if (args.subcommand == "list") {
        return args;
    }
    if (args.subcommand != "run") {
        std::exit(usage());
    }
    if (argc < 3) {
        std::exit(usage());
    }
    args.config_path = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "error: " << flag << " requires an argument\n";
                std::exit(usage());
            }
            return argv[++i];
        };
        if (a == "--placement") {
            args.placement_algo = next("--placement");
        } else if (a == "--migration") {
            args.migration_algo = next("--migration");
        } else if (a == "--seed") {
            args.seed_override = std::stoull(next("--seed"));
        } else if (a == "--output") {
            args.output_root = next("--output");
        } else if (a == "--verbose") {
            args.verbose = true;
        } else if (a == "--param") {
            const auto kv = next("--param");
            const auto eq = kv.find('=');
            if (eq == std::string::npos) {
                std::cerr << "error: --param requires key=value format\n";
                std::exit(usage());
            }
            args.algo_params[kv.substr(0, eq)] = kv.substr(eq + 1);
        } else {
            std::cerr << "error: unknown argument '" << a << "'\n";
            std::exit(usage());
        }
    }
    return args;
}

int cmd_list() {
    algosim::algorithms::register_builtin_algorithms();
    std::cout << "Placement algorithms:\n";
    for (const auto& n : algosim::algorithms::AlgorithmRegistry::placement_names()) {
        std::cout << "  - " << n << '\n';
    }
    std::cout << "Migration algorithms:\n";
    for (const auto& n : algosim::algorithms::AlgorithmRegistry::migration_names()) {
        std::cout << "  - " << n << '\n';
    }
    return 0;
}

#ifdef ALGOSIM_WITH_SIMGRID
int cmd_run(const CliArgs& args) {
    using namespace algosim;

    algorithms::register_builtin_algorithms();

    auto spec = scenario::ScenarioBuilder::build(args.config_path, args.seed_override);

    // Apply forecast_hours from --param if provided (overrides YAML).
    if (auto it = args.algo_params.find("forecast_hours"); it != args.algo_params.end()) {
        spec.carbon_forecast_hours = std::stoi(it->second);
    }

    const auto scenario_stem = args.config_path.stem().string();
    const auto run_id        = runner::make_run_id(
        args.placement_algo, args.migration_algo, scenario_stem, spec.seed);

    runner::ResultsWriter writer{
        args.output_root, run_id, spec, args.placement_algo, args.migration_algo,
        args.algo_params};
    auto build            = runner::build_info();
    build.simgrid_version = backend::simgrid_version_string();
    writer.write_manifest(build);
    writer.freeze_config();

    auto placement = args.algo_params.empty()
        ? algorithms::AlgorithmRegistry::make_placement(args.placement_algo)
        : algorithms::AlgorithmRegistry::make_placement(args.placement_algo, args.algo_params);
    std::unique_ptr<algorithms::MigrationAlgorithm> migration;
    if (args.migration_algo != "none") {
        migration = args.algo_params.empty()
            ? algorithms::AlgorithmRegistry::make_migration(args.migration_algo)
            : algorithms::AlgorithmRegistry::make_migration(args.migration_algo, args.algo_params);
    }

    backend::SimGridBackend backend_inst{spec};
    auto result = backend_inst.run(*placement, migration.get());

    for (const auto& d : result.decisions) {
        if (d.kind == "placement") {
            writer.append_decisions(std::span{&d.placement, 1}, d.t_sim);
        } else {
            writer.append_decisions(std::span{&d.migration, 1}, d.t_sim);
        }
    }
    writer.write_metrics_csv(result.time_series);
    writer.write_summary(result.summary);

    std::cout << "run complete: " << writer.run_dir() << '\n';
    return 0;
}
#endif  // ALGOSIM_WITH_SIMGRID

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto args = parse_cli(argc, argv);
        if (args.subcommand == "list") {
            return cmd_list();
        }
#ifdef ALGOSIM_WITH_SIMGRID
        return cmd_run(args);
#else
        std::cerr << "error: this build does not include the SimGrid backend; "
                     "reconfigure with -DALGOSIM_ENABLE_SIMGRID=ON\n";
        return 3;
#endif
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << '\n';
        return 1;
    }
}

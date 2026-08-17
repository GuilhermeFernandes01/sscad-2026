#include "algosim/algorithms/registry.hpp"

#include "algosim/algorithms/best_fit.hpp"
#include "algosim/algorithms/bfd.hpp"
#include "algosim/algorithms/cnemesis.hpp"
#include "algosim/algorithms/ffd.hpp"
#include "algosim/algorithms/first_fit.hpp"
#include "algosim/algorithms/follow_renewables.hpp"
#include "algosim/algorithms/followme_s.hpp"
#include "algosim/algorithms/lowest_carbon_dc.hpp"
#include "algosim/algorithms/round_robin.hpp"
#include "algosim/algorithms/worst_fit.hpp"
#include "algosim/algorithms/wsnb.hpp"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace algosim::algorithms {

namespace {

struct Registries {
    std::mutex                                         mtx;
    std::unordered_map<std::string, AlgorithmRegistry::PlacementFactory> placement;
    std::unordered_map<std::string, AlgorithmRegistry::MigrationFactory> migration;
};

Registries& registries() {
    static Registries instance;
    return instance;
}

std::once_flag& builtin_once() {
    static std::once_flag flag;
    return flag;
}

}  // namespace

void AlgorithmRegistry::register_placement(std::string name, PlacementFactory factory) {
    auto& reg = registries();
    const std::lock_guard<std::mutex> lock{reg.mtx};
    reg.placement.insert_or_assign(std::move(name), std::move(factory));
}

void AlgorithmRegistry::register_migration(std::string name, MigrationFactory factory) {
    auto& reg = registries();
    const std::lock_guard<std::mutex> lock{reg.mtx};
    reg.migration.insert_or_assign(std::move(name), std::move(factory));
}

std::unique_ptr<PlacementAlgorithm>
AlgorithmRegistry::make_placement(const std::string& name) {
    auto& reg = registries();
    const std::lock_guard<std::mutex> lock{reg.mtx};
    const auto                        it = reg.placement.find(name);
    if (it == reg.placement.end()) {
        throw std::out_of_range{"AlgorithmRegistry: unknown placement algorithm '" + name + "'"};
    }
    return it->second();
}

std::unique_ptr<MigrationAlgorithm>
AlgorithmRegistry::make_migration(const std::string& name) {
    auto& reg = registries();
    const std::lock_guard<std::mutex> lock{reg.mtx};
    const auto                        it = reg.migration.find(name);
    if (it == reg.migration.end()) {
        throw std::out_of_range{"AlgorithmRegistry: unknown migration algorithm '" + name + "'"};
    }
    return it->second();
}

std::vector<std::string> AlgorithmRegistry::placement_names() {
    auto& reg = registries();
    const std::lock_guard<std::mutex> lock{reg.mtx};
    std::vector<std::string>          names;
    names.reserve(reg.placement.size());
    for (const auto& [n, _] : reg.placement) {
        names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> AlgorithmRegistry::migration_names() {
    auto& reg = registries();
    const std::lock_guard<std::mutex> lock{reg.mtx};
    std::vector<std::string>          names;
    names.reserve(reg.migration.size());
    for (const auto& [n, _] : reg.migration) {
        names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void AlgorithmRegistry::clear() {
    auto& reg = registries();
    const std::lock_guard<std::mutex> lock{reg.mtx};
    reg.placement.clear();
    reg.migration.clear();
}

namespace {

using AlgoParams = AlgorithmRegistry::AlgoParams;

double get_double(const AlgoParams& p, const std::string& key, double def) {
    auto it = p.find(key);
    if (it == p.end()) return def;
    return std::stod(it->second);
}

int get_int(const AlgoParams& p, const std::string& key, int def) {
    auto it = p.find(key);
    if (it == p.end()) return def;
    return std::stoi(it->second);
}

}  // namespace

std::unique_ptr<PlacementAlgorithm>
AlgorithmRegistry::make_placement(const std::string& name, const AlgoParams& params) {
    if (params.empty()) {
        return make_placement(name);
    }
    if (name == "cnemesis") {
        return std::make_unique<CNemesis>(
            get_int(params, "max_concurrent", 20),
            get_double(params, "min_benefit_ratio", 0.9),
            get_double(params, "under_threshold", 0.20));
    }
    if (name == "wsnb") {
        return std::make_unique<WSNB>(
            get_double(params, "carbon_threshold", 400.0));
    }
    if (name == "followme_s") {
        return std::make_unique<FollowMeS>(
            get_double(params, "under_threshold", 0.20),
            get_int(params, "max_migs_per_tick", 50));
    }
    return make_placement(name);
}

std::unique_ptr<MigrationAlgorithm>
AlgorithmRegistry::make_migration(const std::string& name, const AlgoParams& params) {
    if (params.empty()) {
        return make_migration(name);
    }
    if (name == "cnemesis") {
        return std::make_unique<CNemesis>(
            get_int(params, "max_concurrent", 20),
            get_double(params, "min_benefit_ratio", 0.9),
            get_double(params, "under_threshold", 0.20));
    }
    if (name == "followme_s") {
        return std::make_unique<FollowMeS>(
            get_double(params, "under_threshold", 0.20),
            get_int(params, "max_migs_per_tick", 50));
    }
    return make_migration(name);
}

void register_builtin_algorithms() {
    std::call_once(builtin_once(), [] {
        AlgorithmRegistry::register_placement(
            "first_fit", [] { return std::make_unique<FirstFit>(); });
        AlgorithmRegistry::register_placement(
            "best_fit", [] { return std::make_unique<BestFit>(); });
        AlgorithmRegistry::register_placement(
            "worst_fit", [] { return std::make_unique<WorstFit>(); });
        AlgorithmRegistry::register_placement(
            "round_robin", [] { return std::make_unique<RoundRobin>(); });
        AlgorithmRegistry::register_placement(
            "ffd", [] { return std::make_unique<FFD>(); });
        AlgorithmRegistry::register_placement(
            "bfd", [] { return std::make_unique<BFD>(); });
        AlgorithmRegistry::register_placement(
            "lowest_carbon_dc", [] { return std::make_unique<LowestCarbonDC>(); });
        AlgorithmRegistry::register_placement(
            "follow_renewables", [] { return std::make_unique<FollowRenewables>(); });
        AlgorithmRegistry::register_migration(
            "follow_renewables", [] { return std::make_unique<FollowRenewables>(); });

        // Ports from c-nemesis-master (SMARTGREENS 2022).
        AlgorithmRegistry::register_placement(
            "wsnb", [] { return std::make_unique<WSNB>(); });
        AlgorithmRegistry::register_placement(
            "followme_s", [] { return std::make_unique<FollowMeS>(); });
        AlgorithmRegistry::register_migration(
            "followme_s", [] { return std::make_unique<FollowMeS>(); });
        AlgorithmRegistry::register_placement(
            "cnemesis", [] { return std::make_unique<CNemesis>(); });
        AlgorithmRegistry::register_migration(
            "cnemesis", [] { return std::make_unique<CNemesis>(); });
    });
}

}  // namespace algosim::algorithms

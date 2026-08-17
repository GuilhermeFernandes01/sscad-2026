#pragma once

#include "algosim/algorithms/migration_algorithm.hpp"
#include "algosim/algorithms/placement_algorithm.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace algosim::algorithms {

// Global registry mapping algorithm names to factory functions.
// Factories are process-global and registered once at startup via
// `register_builtin_algorithms()` (called implicitly by the runner).
class AlgorithmRegistry {
public:
    using PlacementFactory = std::function<std::unique_ptr<PlacementAlgorithm>()>;
    using MigrationFactory = std::function<std::unique_ptr<MigrationAlgorithm>()>;
    using AlgoParams = std::unordered_map<std::string, std::string>;

    static void register_placement(std::string name, PlacementFactory factory);
    static void register_migration(std::string name, MigrationFactory factory);

    // Throws std::out_of_range if the name is not registered.
    [[nodiscard]] static std::unique_ptr<PlacementAlgorithm>
        make_placement(const std::string& name);
    [[nodiscard]] static std::unique_ptr<MigrationAlgorithm>
        make_migration(const std::string& name);

    // Overloads with algorithm-specific parameters for parameterized algorithms.
    [[nodiscard]] static std::unique_ptr<PlacementAlgorithm>
        make_placement(const std::string& name, const AlgoParams& params);
    [[nodiscard]] static std::unique_ptr<MigrationAlgorithm>
        make_migration(const std::string& name, const AlgoParams& params);

    [[nodiscard]] static std::vector<std::string> placement_names();
    [[nodiscard]] static std::vector<std::string> migration_names();

    // Clears all registered algorithms; only meant for tests.
    static void clear();
};

// Registers all built-in algorithms. Idempotent; safe to call multiple times.
void register_builtin_algorithms();

}  // namespace algosim::algorithms

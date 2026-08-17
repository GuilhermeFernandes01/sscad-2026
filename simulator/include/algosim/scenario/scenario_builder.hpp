#pragma once

#include "algosim/scenario/scenario_spec.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace algosim::scenario {

class ScenarioBuilder {
public:
    // Parse a scenario YAML and materialize a fully-expanded ScenarioSpec,
    // including the deterministic workload event stream.
    //
    // `seed_override` replaces the seed read from the YAML *before* the
    // workload is generated, so seed-varying matrix runs produce distinct
    // event streams.
    //
    // Throws std::runtime_error on any parse/validation failure.
    [[nodiscard]] static ScenarioSpec
        build(const std::filesystem::path& yaml_path,
              std::optional<std::uint64_t> seed_override = std::nullopt);
};

}  // namespace algosim::scenario

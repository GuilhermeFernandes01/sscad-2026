#pragma once

#include "algosim/domain/carbon.hpp"

#include <filesystem>

namespace algosim::scenario {

// Loads a carbon-intensity time series from a two-column CSV:
//     t_seconds,gco2_per_kwh
//     0,432.1
//     3600,429.0
//     ...
//
// `start` and `step_seconds` are taken from the YAML scenario, not from the
// file; the CSV provides only the values.
[[nodiscard]] domain::CarbonIntensitySeries
    load_carbon_csv(const std::filesystem::path& csv_path,
                    const std::string&           series_id,
                    std::chrono::system_clock::time_point start,
                    int                          step_seconds);

}  // namespace algosim::scenario

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace algosim::domain {

// A time series of grid carbon intensity for one datacenter location.
//
// Stored as a uniform-step array: `gco2_per_kwh[i]` is the value at
// `start + i * step_seconds`. `at(ts)` performs nearest-previous lookup; the
// series is assumed to cover the full simulation window.
struct CarbonIntensitySeries {
    std::string                           series_id;
    std::chrono::system_clock::time_point start;
    int                                   step_seconds = 3600;   // hourly default
    std::vector<double>                   gco2_per_kwh;

    // Optional companion series used by Follow-The-Renewables heuristics.
    std::optional<std::vector<double>> solar_irradiance_wm2;

    [[nodiscard]] double at(std::chrono::system_clock::time_point ts) const {
        if (gco2_per_kwh.empty()) {
            return 0.0;
        }
        const auto offset  = std::chrono::duration_cast<std::chrono::seconds>(ts - start).count();
        const auto step    = static_cast<long long>(step_seconds);
        auto       idx_raw = offset / step;
        if (idx_raw < 0) {
            idx_raw = 0;
        }
        const auto size_ll  = static_cast<long long>(gco2_per_kwh.size());
        const auto idx_safe = (idx_raw >= size_ll) ? (size_ll - 1) : idx_raw;
        return gco2_per_kwh[static_cast<std::size_t>(idx_safe)];
    }
};

}  // namespace algosim::domain

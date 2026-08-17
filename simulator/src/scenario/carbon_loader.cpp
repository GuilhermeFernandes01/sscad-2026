#include "algosim/scenario/carbon_loader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace algosim::scenario {

domain::CarbonIntensitySeries
load_carbon_csv(const std::filesystem::path& csv_path,
                const std::string&           series_id,
                std::chrono::system_clock::time_point start,
                int                          step_seconds) {
    std::ifstream in{csv_path};
    if (!in) {
        throw std::runtime_error{"carbon_loader: cannot open " + csv_path.string()};
    }

    domain::CarbonIntensitySeries series;
    series.series_id    = series_id;
    series.start        = start;
    series.step_seconds = step_seconds;

    std::string line;
    bool        first = true;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (first && (line.find("t_seconds") != std::string::npos
                       || line.find("gco2") != std::string::npos)) {
            first = false;
            continue;
        }
        first = false;
        const auto comma = line.find(',');
        if (comma == std::string::npos) {
            continue;
        }
        try {
            const double value = std::stod(line.substr(comma + 1));
            series.gco2_per_kwh.push_back(value);
        } catch (const std::exception&) {
            // Skip malformed lines silently; real validation belongs in a
            // separate linter target if needed.
        }
    }

    if (series.gco2_per_kwh.empty()) {
        throw std::runtime_error{"carbon_loader: no samples parsed from " + csv_path.string()};
    }
    return series;
}

}  // namespace algosim::scenario

#pragma once

#include "algosim/domain/host.hpp"

#include <string>
#include <vector>

namespace algosim::domain {

struct Datacenter {
    std::string       dc_id;
    std::string       name;
    double            latitude  = 0.0;
    double            longitude = 0.0;
    std::string       timezone;
    std::vector<Host> hosts;
    // Power Usage Effectiveness, applied by MetricCollector, NOT by SimGrid.
    double      pue = 1.5;
    // Identifier of the CarbonIntensitySeries to use for this datacenter.
    std::string carbon_series_id;
};

}  // namespace algosim::domain

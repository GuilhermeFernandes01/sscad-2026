#pragma once

#include "algosim/scenario/scenario_spec.hpp"

#include <filesystem>

namespace algosim::scenario {

// Generates a SimGrid platform XML from a ScenarioSpec when no explicit
// platform_xml is provided.
//
// The generated XML uses a flat-zone topology per datacenter with
// full-duplex backbone links and Vivaldi inter-zone routing based on
// (latitude, longitude) coordinates.
//
// Each host has `wattage_per_state` set to "idle:peak" and
// `carbon_intensity` set to the first value of the DC's carbon series
// (the time-varying trace is loaded separately via the carbon_footprint
// plugin at runtime).
[[nodiscard]] std::filesystem::path
generate_platform_xml(const ScenarioSpec& spec,
                      const std::filesystem::path& output_dir);

}  // namespace algosim::scenario

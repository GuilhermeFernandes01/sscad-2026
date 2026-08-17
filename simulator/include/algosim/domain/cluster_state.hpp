#pragma once

#include "algosim/domain/datacenter.hpp"
#include "algosim/domain/vm.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace algosim::domain {

// Immutable-by-convention snapshot of the whole simulated cluster, passed to
// algorithms on each decision tick.
//
// Fields are non-const only so that the backend can construct and populate
// instances; algorithms must treat the snapshot as read-only.
struct ClusterState {
    double                                  t_seconds = 0.0;
    std::chrono::system_clock::time_point   wall_datetime{};
    std::vector<Datacenter>                 datacenters;
    std::vector<VM>                         pending_vms;
    std::vector<VM>                         running_vms;
    // dc_id -> current carbon intensity in gCO2/kWh.
    std::unordered_map<std::string, double> carbon_now;
    // dc_id -> forecast values [+1h, +2h, ...] in gCO2/kWh (empty when disabled).
    std::unordered_map<std::string, std::vector<double>> carbon_forecast;
    int                                     forecast_hours = 0;
    std::uint64_t                           rng_seed = 0;

    [[nodiscard]] const Host& host(std::string_view id) const {
        for (const auto& dc : datacenters) {
            for (const auto& h : dc.hosts) {
                if (h.host_id == id) {
                    return h;
                }
            }
        }
        throw std::out_of_range{"ClusterState::host: unknown host_id"};
    }

    [[nodiscard]] const Datacenter& dc(std::string_view id) const {
        for (const auto& d : datacenters) {
            if (d.dc_id == id) {
                return d;
            }
        }
        throw std::out_of_range{"ClusterState::dc: unknown dc_id"};
    }

    // Flat view of all hosts across all datacenters, preserving YAML order
    // (which is lexicographic by host_id after ScenarioBuilder normalization).
    [[nodiscard]] std::vector<const Host*> all_hosts() const {
        std::vector<const Host*> out;
        std::size_t              total = 0;
        for (const auto& d : datacenters) {
            total += d.hosts.size();
        }
        out.reserve(total);
        for (const auto& d : datacenters) {
            for (const auto& h : d.hosts) {
                out.push_back(&h);
            }
        }
        return out;
    }

    // Find a pending VM by id (used by the runner to pass the full VM record
    // to `backend.submit_vm` after the algorithm returns only an id).
    [[nodiscard]] const VM* pending_vm(std::string_view id) const noexcept {
        for (const auto& v : pending_vms) {
            if (v.vm_id == id) {
                return &v;
            }
        }
        return nullptr;
    }

    [[nodiscard]] double carbon_at(std::string_view dc_id) const {
        const auto it = carbon_now.find(std::string{dc_id});
        if (it == carbon_now.end()) {
            return 0.0;
        }
        return it->second;
    }

    [[nodiscard]] double carbon_avg(std::string_view dc_id) const {
        double now = carbon_at(dc_id);
        const auto it = carbon_forecast.find(std::string{dc_id});
        if (it == carbon_forecast.end() || it->second.empty()) {
            return now;
        }
        double sum = now;
        for (double v : it->second) {
            sum += v;
        }
        return sum / (1.0 + static_cast<double>(it->second.size()));
    }
};

}  // namespace algosim::domain

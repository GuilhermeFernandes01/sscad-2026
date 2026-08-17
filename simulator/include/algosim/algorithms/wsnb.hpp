#pragma once

#include "algosim/algorithms/placement_algorithm.hpp"

namespace algosim::algorithms {

// WSNB: Workload Shifting Non-Brownout (Xu & Buyya 2020, adapted).
//
// Locality-first placement with a green-energy fallback:
//   1. For each pending VM, pick the "home" datacenter by hashing the VM id
//      with FNV-1a de 64 bits (especificado no repositório) sobre a lista de
//      DCs ordenada por dc_id. O DC de origem é um proxy de afinidade de
//      localidade; implantações reais usam a origem da requisição.
//   2. If the home DC has capacity AND `carbon_now[home] <= threshold`, place
//      the VM there (Best Fit within the DC).
//   3. Otherwise, fall back to the DC with the lowest `carbon_now` that has
//      capacity. Empates resolvidos pelo menor dc_id (ver
//      algosim/algorithms/tiebreak.hpp), não pela ordem de declaração no
//      arquivo de cenário.
//
// Placement only: WSNB does NOT migrate running VMs. This is the intended
// behavior from the original paper (locality is preserved to protect SLAs).
class WSNB final : public PlacementAlgorithm {
public:
    explicit WSNB(double carbon_threshold_gco2 = 400.0)
        : threshold_{carbon_threshold_gco2} {}

    [[nodiscard]] std::string_view name() const override { return "wsnb"; }
    [[nodiscard]] std::vector<domain::PlacementDecision>
        place(const domain::ClusterState& state) override;

private:
    double threshold_;
};

}  // namespace algosim::algorithms

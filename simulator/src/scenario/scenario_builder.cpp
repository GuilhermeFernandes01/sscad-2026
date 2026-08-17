#include "algosim/scenario/scenario_builder.hpp"

#include "algosim/scenario/carbon_loader.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace algosim::scenario {

namespace {

[[nodiscard]] std::chrono::system_clock::time_point
parse_iso8601(const std::string& s) {
    // Accepts "YYYY-MM-DDTHH:MM:SS" (UTC). Timezone suffix is ignored.
    std::tm tm{};
    std::istringstream in{s};
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (in.fail()) {
        throw std::runtime_error{"scenario: invalid start_datetime '" + s + "'"};
    }
#if defined(_WIN32)
    const auto tt = _mkgmtime(&tm);
#else
    const auto tt = timegm(&tm);
#endif
    return std::chrono::system_clock::from_time_t(tt);
}

void load_datacenters(const YAML::Node& node, ScenarioSpec& spec) {
    if (!node || !node.IsSequence()) {
        throw std::runtime_error{"scenario: `datacenters` must be a sequence"};
    }
    for (const auto& dc_node : node) {
        domain::Datacenter dc;
        dc.dc_id            = dc_node["id"].as<std::string>();
        dc.name             = dc_node["name"] ? dc_node["name"].as<std::string>() : dc.dc_id;
        dc.latitude         = dc_node["latitude"]  ? dc_node["latitude"].as<double>()  : 0.0;
        dc.longitude        = dc_node["longitude"] ? dc_node["longitude"].as<double>() : 0.0;
        dc.timezone         = dc_node["timezone"] ? dc_node["timezone"].as<std::string>() : "";
        dc.pue              = dc_node["pue"] ? dc_node["pue"].as<double>() : 1.5;
        dc.carbon_series_id = dc_node["carbon_series_id"]
                                  ? dc_node["carbon_series_id"].as<std::string>()
                                  : dc.dc_id;

        if (dc_node["hosts"]) {
            for (const auto& h_node : dc_node["hosts"]) {
                domain::Host h;
                h.host_id           = h_node["id"].as<std::string>();
                h.dc_id             = dc.dc_id;
                h.cpu_cores         = h_node["cpu_cores"].as<int>();
                h.cpu_capacity_mips = h_node["cpu_capacity_mips"].as<double>();
                h.ram_mb            = h_node["ram_mb"].as<int>();
                h.disk_gb           = h_node["disk_gb"] ? h_node["disk_gb"].as<int>() : 0;
                h.net_bw_mbps       = h_node["net_bw_mbps"] ? h_node["net_bw_mbps"].as<double>() : 1000.0;
                h.power_idle_w      = h_node["power_idle_w"].as<double>();
                h.power_peak_w      = h_node["power_peak_w"].as<double>();
                dc.hosts.push_back(h);
            }
        } else if (dc_node["host_groups"]) {
            int global_idx = 0;
            for (const auto& grp : dc_node["host_groups"]) {
                const auto& tmpl  = grp["template"];
                const auto  count = grp["count"].as<int>();
                for (int i = 0; i < count; ++i, ++global_idx) {
                    domain::Host h;
                    h.dc_id             = dc.dc_id;
                    const auto i_str    = std::to_string(global_idx);
                    const auto pad_len  = static_cast<std::size_t>(
                        3 - std::min<std::size_t>(3, i_str.size()));
                    h.host_id           = dc.dc_id + "-h" + std::string(pad_len, '0') + i_str;
                    h.cpu_cores         = tmpl["cpu_cores"].as<int>();
                    h.cpu_capacity_mips = tmpl["cpu_capacity_mips"].as<double>();
                    h.ram_mb            = tmpl["ram_mb"].as<int>();
                    h.disk_gb           = tmpl["disk_gb"] ? tmpl["disk_gb"].as<int>() : 0;
                    h.net_bw_mbps       = tmpl["net_bw_mbps"] ? tmpl["net_bw_mbps"].as<double>() : 1000.0;
                    h.power_idle_w      = tmpl["power_idle_w"].as<double>();
                    h.power_peak_w      = tmpl["power_peak_w"].as<double>();
                    dc.hosts.push_back(h);
                }
            }
        } else if (dc_node["host_template"]) {
            const auto& tmpl = dc_node["host_template"];
            const auto  count = dc_node["host_count"].as<int>();
            for (int i = 0; i < count; ++i) {
                domain::Host h;
                h.dc_id             = dc.dc_id;
                const auto i_str    = std::to_string(i);
                const auto pad_len  = static_cast<std::size_t>(
                    3 - std::min<std::size_t>(3, i_str.size()));
                h.host_id           = dc.dc_id + "-h" + std::string(pad_len, '0') + i_str;
                h.cpu_cores         = tmpl["cpu_cores"].as<int>();
                h.cpu_capacity_mips = tmpl["cpu_capacity_mips"].as<double>();
                h.ram_mb            = tmpl["ram_mb"].as<int>();
                h.disk_gb           = tmpl["disk_gb"] ? tmpl["disk_gb"].as<int>() : 0;
                h.net_bw_mbps       = tmpl["net_bw_mbps"] ? tmpl["net_bw_mbps"].as<double>() : 1000.0;
                h.power_idle_w      = tmpl["power_idle_w"].as<double>();
                h.power_peak_w      = tmpl["power_peak_w"].as<double>();
                dc.hosts.push_back(h);
            }
        }

        // Normalize host order lexicographically for determinism.
        std::sort(dc.hosts.begin(), dc.hosts.end(),
                  [](const domain::Host& a, const domain::Host& b) { return a.host_id < b.host_id; });
        spec.datacenters.push_back(std::move(dc));
    }
}

void load_carbon(const YAML::Node& node, ScenarioSpec& spec) {
    if (!node) {
        return;
    }
    for (const auto& src : node) {
        const auto  series_id = src["series_id"].as<std::string>();
        const auto  kind      = src["kind"] ? src["kind"].as<std::string>() : "flat";
        if (kind == "flat") {
            domain::CarbonIntensitySeries series;
            series.series_id    = series_id;
            series.start        = spec.start_datetime;
            series.step_seconds = 3600;
            series.gco2_per_kwh = {src["value_gco2_per_kwh"].as<double>()};
            spec.carbon_series.emplace(series_id, std::move(series));
        } else if (kind == "csv") {
            const auto path = src["path"].as<std::string>();
            const auto step = src["step_seconds"] ? src["step_seconds"].as<int>() : 3600;
            // Anchor at the DATA epoch, never at the scenario start: t_seconds=0
            // in the CSV is a fixed calendar instant (default: 2020-01-01T00:00,
            // the LowCarbonCloud export epoch). Anchoring at spec.start_datetime
            // made start_datetime inert.
            const auto data_start = parse_iso8601(
                src["data_start_datetime"]
                    ? src["data_start_datetime"].as<std::string>()
                    : std::string{"2020-01-01T00:00:00"});
            auto series = load_carbon_csv(path, series_id, data_start, step);
            spec.carbon_series.emplace(series_id, std::move(series));
        } else {
            throw std::runtime_error{"scenario: unknown carbon source kind '" + kind + "'"};
        }
    }
}

// Ordem total estrita da fila de eventos do workload.
//
// MOTIVAÇÃO
//   Esta é a ordenação mais consequente do simulador: a sequência dos eventos
//   Submit define a ordem de `ClusterState::pending_vms`, que é a ordem de
//   alocação de NOVE dos onze algoritmos: todos os que não reordenam a fila
//   (first_fit, best_fit, worst_fit, round_robin, lowest_carbon_dc, wsnb,
//   follow_renewables, followme_s, cnemesis). Ordenar apenas por `t_seconds`
//   deixava a ordem relativa dos eventos simultâneos a cargo da implementação
//   de `std::sort`. Não é caso de canto: o trace azure_2020 concentra 10.000
//   chegadas em 16 instantes distintos, de modo que praticamente TODOS os
//   eventos Submit empatam na chave principal.
//
// ESPECIFICAÇÃO ADOTADA
//   (1) t_seconds crescente - instante do evento;
//   (2) kind crescente      - Submit antes de Terminate no mesmo instante, que
//       é a ordem causalmente correta (uma VM só termina depois de submetida);
//   (3) vm_id crescente     - único por construção; fecha a ordem.
//
//   A chave (3) reproduz, para cargas de trace, a mesma ordem (chegada, vm_id)
//   que o carregador já estabelece ao ler o arquivo, ordem que o `std::sort`
//   por t_seconds podia embaralhar. A chave (2) é praticamente inerte no
//   backend atual, que aplica todos os Terminate da janela antes de qualquer
//   alocação; ela é declarada mesmo assim, para que a fila não dependa de um
//   detalhe do consumidor.
[[nodiscard]] bool evento_antes(const domain::WorkloadEvent& a,
                                const domain::WorkloadEvent& b) noexcept {
    if (a.t_seconds != b.t_seconds) {
        return a.t_seconds < b.t_seconds;
    }
    if (a.kind != b.kind) {
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    }
    return a.vm.vm_id < b.vm.vm_id;
}

// Generate a Poisson-with-diurnal-modulation workload stream, deterministic
// in `seed`. Simplified: arrivals drawn from a homogeneous Poisson process
// with rate modulated by `1 + amplitude * sin(2*pi*t / period)`.
void generate_poisson_workload(const YAML::Node& node, ScenarioSpec& spec) {
    const auto n_vms              = node["n_vms"].as<int>();
    const auto mean_arrival_rate  = node["mean_arrival_rate"].as<double>();   // VMs/sec baseline
    const auto amplitude          = node["diurnal_modulation"]
                                       ? node["diurnal_modulation"].as<double>() : 0.0;
    const auto demand_mean        = node["demand_mean_mips"].as<double>();
    const auto demand_sigma       = node["demand_sigma_mips"] ? node["demand_sigma_mips"].as<double>() : 0.0;
    const auto ram_mb             = node["ram_mb"].as<int>();
    const auto image_size_mb      = node["image_size_mb"] ? node["image_size_mb"].as<int>() : ram_mb;
    const auto lifetime_mean_s    = node["lifetime_mean_seconds"] ? node["lifetime_mean_seconds"].as<double>() : spec.duration_seconds;
    const auto dirty_rate_mbps    = node["dirty_rate_mbps"] ? node["dirty_rate_mbps"].as<double>() : 50.0;
    const auto arrival_window_s   = node["arrival_window_seconds"]
                                        ? node["arrival_window_seconds"].as<double>()
                                        : spec.duration_seconds;
    constexpr double period_s     = 86400.0;

    std::mt19937_64                   rng{spec.seed};
    std::uniform_real_distribution<>  unif{0.0, 1.0};
    std::normal_distribution<>        demand_dist{demand_mean, std::max(1e-6, demand_sigma)};
    std::exponential_distribution<>   life_dist{1.0 / std::max(1.0, lifetime_mean_s)};

    // Thinning: sample at the max rate (1 + amplitude) * base, accept with
    // probability equal to the ratio of true rate to max.
    const double max_rate = mean_arrival_rate * (1.0 + std::abs(amplitude));
    double       t        = 0.0;
    int          emitted  = 0;
    while (emitted < n_vms && t < arrival_window_s) {
        std::exponential_distribution<> inter{max_rate};
        t += inter(rng);
        if (t >= arrival_window_s) {
            break;
        }
        const double modulation = 1.0 + amplitude * std::sin(2.0 * 3.14159265358979 * t / period_s);
        const double accept_p   = std::clamp(modulation / (1.0 + std::abs(amplitude)), 0.0, 1.0);
        if (unif(rng) > accept_p) {
            continue;
        }

        domain::VM vm;
        vm.vm_id           = "vm-" + std::to_string(emitted);
        vm.cpu_cores       = 1;
        vm.cpu_demand_mips = std::max(1.0, demand_dist(rng));
        vm.ram_mb          = ram_mb;
        vm.disk_gb         = 0;
        vm.image_size_mb   = image_size_mb;
        vm.dirty_rate_mbps = dirty_rate_mbps;
        vm.arrival_time_s  = t;
        vm.lifetime_s      = life_dist(rng);

        domain::WorkloadEvent ev;
        ev.t_seconds = t;
        ev.kind      = domain::EventKind::Submit;
        ev.vm        = vm;
        spec.events.push_back(std::move(ev));

        // Optional terminate event for finite-lifetime VMs.
        if (std::isfinite(vm.lifetime_s)) {
            domain::WorkloadEvent term;
            term.t_seconds = t + vm.lifetime_s;
            term.kind      = domain::EventKind::Terminate;
            term.vm        = vm;
            spec.events.push_back(std::move(term));
        }
        ++emitted;
    }
    std::sort(spec.events.begin(), spec.events.end(), evento_antes);
}

void generate_trace_workload(const YAML::Node& node, ScenarioSpec& spec) {
    const auto path          = node["path"].as<std::string>();
    const auto n_vms         = node["n_vms"] ? node["n_vms"].as<int>() : 0;
    const auto ram_mb        = node["ram_mb"].as<int>();
    const auto image_size_mb = node["image_size_mb"] ? node["image_size_mb"].as<int>() : ram_mb;
    const auto dirty_rate    = node["dirty_rate_mbps"] ? node["dirty_rate_mbps"].as<double>() : 50.0;
    const auto lifetime_s    = node["lifetime_mean_seconds"]
                                   ? node["lifetime_mean_seconds"].as<double>()
                                   : spec.duration_seconds;
    const auto max_demand    = node["max_demand_mips"]
                                   ? node["max_demand_mips"].as<double>()
                                   : std::numeric_limits<double>::infinity();
    const auto rescale_window = node["rescale_arrival_seconds"]
                                    ? node["rescale_arrival_seconds"].as<double>()
                                    : 0.0;

    std::ifstream in{path};
    if (!in) {
        throw std::runtime_error{"scenario: cannot open trace file '" + path + "'"};
    }

    struct RawVM {
        double      arrival;
        std::string vm_id;
        int         cores;
        double      demand;
    };
    std::vector<RawVM> raw_vms;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream iss{line};
        std::string cmd, subcmd;
        iss >> cmd >> subcmd;
        if (cmd != "vmdispatcher" || subcmd != "sendVM") continue;

        RawVM r;
        iss >> r.arrival >> r.vm_id >> r.cores >> r.demand;
        // Uma linha `sendVM` malformada interrompe a carga. Antes ela era
        // descartada em silêncio (`if (iss.fail()) continue;`), o que mudaria a
        // carga de trabalho efetiva sem deixar rastro nos artefatos; e o clamp
        // adiante agrava o problema, porque `std::max(1.0, NaN)` devolve 1.0 e
        // converteria uma demanda inválida numa VM de 1 MIPS.
        if (iss.fail() || !std::isfinite(r.arrival) || !std::isfinite(r.demand)) {
            throw std::runtime_error{"scenario: linha sendVM inválida no trace '"
                                     + path + "': " + line};
        }
        raw_vms.push_back(std::move(r));
    }

    std::sort(raw_vms.begin(), raw_vms.end(), [](const RawVM& a, const RawVM& b) {
        if (a.arrival != b.arrival) return a.arrival < b.arrival;
        return a.vm_id < b.vm_id;
    });

    if (n_vms > 0 && static_cast<int>(raw_vms.size()) > n_vms) {
        raw_vms.resize(static_cast<std::size_t>(n_vms));
    }

    double time_scale  = 1.0;
    double time_offset = 0.0;
    if (rescale_window > 0.0 && raw_vms.size() > 1) {
        double min_t = raw_vms.front().arrival;
        double max_t = raw_vms.back().arrival;
        time_offset  = min_t;
        double span  = max_t - min_t;
        time_scale   = (span > 0.0) ? rescale_window / span : 1.0;
    }

    std::mt19937_64              rng{spec.seed};
    std::exponential_distribution<> life_dist{1.0 / std::max(1.0, lifetime_s)};

    for (std::size_t i = 0; i < raw_vms.size(); ++i) {
        const auto& r = raw_vms[i];

        double t = (rescale_window > 0.0)
            ? (r.arrival - time_offset) * time_scale
            : r.arrival;

        domain::VM vm;
        vm.vm_id           = r.vm_id;
        vm.cpu_cores       = r.cores;
        vm.cpu_demand_mips = std::min(std::max(1.0, r.demand), max_demand);
        vm.ram_mb          = ram_mb;
        vm.disk_gb         = 0;
        vm.image_size_mb   = image_size_mb;
        vm.dirty_rate_mbps = dirty_rate;
        vm.arrival_time_s  = t;
        vm.lifetime_s      = life_dist(rng);

        domain::WorkloadEvent ev;
        ev.t_seconds = t;
        ev.kind      = domain::EventKind::Submit;
        ev.vm        = vm;
        spec.events.push_back(std::move(ev));

        if (std::isfinite(vm.lifetime_s)) {
            domain::WorkloadEvent term;
            term.t_seconds = t + vm.lifetime_s;
            term.kind      = domain::EventKind::Terminate;
            term.vm        = vm;
            spec.events.push_back(std::move(term));
        }
    }

    std::sort(spec.events.begin(), spec.events.end(), evento_antes);
}

// Guarda de finitude: pré-condição declarada em algosim/algorithms/tiebreak.hpp.
//
// Os comparadores de desempate exigem uma ordem estrita fraca. Um NaN em
// demanda, intensidade de carbono ou capacidade violaria essa exigência
// (`x != x` e todas as comparações falsas), e `std::sort` sobre um comparador
// inválido é comportamento indefinido: pode corromper memória, não apenas
// produzir ordem errada. O cenário é, portanto, rejeitado na carga, e não
// silenciosamente simulado.
//
// `lifetime_s` infinito é legítimo e significa "VM não termina"; é o único
// valor não finito aceito.
void validar_finitude(const ScenarioSpec& spec) {
    std::vector<std::string> problemas;
    const auto exigir = [&problemas](bool ok, const std::string& onde) {
        if (!ok) {
            problemas.push_back(onde);
        }
    };

    for (const auto& dc : spec.datacenters) {
        exigir(std::isfinite(dc.pue) && dc.pue > 0.0, "datacenter '" + dc.dc_id + "': pue");
        for (const auto& h : dc.hosts) {
            exigir(std::isfinite(h.cpu_capacity_mips) && h.cpu_capacity_mips > 0.0,
                   "host '" + h.host_id + "': cpu_capacity_mips");
            exigir(std::isfinite(h.power_idle_w), "host '" + h.host_id + "': power_idle_w");
            exigir(std::isfinite(h.power_peak_w), "host '" + h.host_id + "': power_peak_w");
            exigir(std::isfinite(h.net_bw_mbps), "host '" + h.host_id + "': net_bw_mbps");
        }
    }

    for (const auto& [series_id, series] : spec.carbon_series) {
        for (std::size_t i = 0; i < series.gco2_per_kwh.size(); ++i) {
            exigir(std::isfinite(series.gco2_per_kwh[i]) && series.gco2_per_kwh[i] >= 0.0,
                   "carbon series '" + series_id + "': amostra " + std::to_string(i));
        }
    }

    for (const auto& ev : spec.events) {
        exigir(std::isfinite(ev.t_seconds), "evento de '" + ev.vm.vm_id + "': t_seconds");
        if (ev.kind == domain::EventKind::Submit) {
            exigir(std::isfinite(ev.vm.cpu_demand_mips) && ev.vm.cpu_demand_mips > 0.0,
                   "vm '" + ev.vm.vm_id + "': cpu_demand_mips");
            exigir(std::isfinite(ev.vm.arrival_time_s), "vm '" + ev.vm.vm_id + "': arrival_time_s");
        }
    }

    if (!problemas.empty()) {
        // Ordena para que a mensagem não dependa da ordem de iteração do
        // `unordered_map` das séries de carbono.
        std::sort(problemas.begin(), problemas.end());
        throw std::runtime_error{"scenario: valor não finito ou inválido em "
                                 + std::to_string(problemas.size()) + " campo(s); primeiro: "
                                 + problemas.front()};
    }
}

void load_workload(const YAML::Node& node, ScenarioSpec& spec) {
    if (!node) {
        throw std::runtime_error{"scenario: missing `workload` section"};
    }
    const auto kind = node["kind"].as<std::string>();
    if (kind == "poisson_diurnal") {
        generate_poisson_workload(node, spec);
    } else if (kind == "trace_file") {
        generate_trace_workload(node, spec);
    } else {
        throw std::runtime_error{"scenario: unsupported workload kind '" + kind + "'"};
    }
}

}  // namespace

ScenarioSpec ScenarioBuilder::build(const std::filesystem::path& yaml_path,
                                    std::optional<std::uint64_t>  seed_override) {
    const auto root = YAML::LoadFile(yaml_path.string());

    ScenarioSpec spec;
    spec.source_config_path         = yaml_path.string();
    spec.name                       = root["scenario"]["name"].as<std::string>();
    spec.duration_seconds           = root["scenario"]["duration_seconds"].as<double>();
    spec.dt_seconds                 = root["scenario"]["dt_seconds"]
                                          ? root["scenario"]["dt_seconds"].as<double>() : 60.0;
    spec.migration_interval_seconds = root["scenario"]["migration_interval_seconds"]
                                          ? root["scenario"]["migration_interval_seconds"].as<double>()
                                          : 600.0;
    spec.carbon_forecast_hours      = root["scenario"]["carbon_forecast_hours"]
                                          ? root["scenario"]["carbon_forecast_hours"].as<int>()
                                          : 0;
    spec.seed                       = seed_override.value_or(
        root["scenario"]["seed"].as<std::uint64_t>());
    spec.start_datetime             = parse_iso8601(
        root["scenario"]["start_datetime"]
            ? root["scenario"]["start_datetime"].as<std::string>()
            : std::string{"2020-01-01T00:00:00"});

    if (root["topology"] && root["topology"]["platform_xml"]) {
        spec.platform_xml_path = root["topology"]["platform_xml"].as<std::string>();
    }

    if (root["network"]) {
        const auto& net = root["network"];
        if (net["inter_dc_bw_mbps"]) {
            spec.network.inter_dc_bw_mbps = net["inter_dc_bw_mbps"].as<double>();
        }
        if (net["inter_dc_latency_factor"]) {
            spec.network.inter_dc_latency_factor =
                net["inter_dc_latency_factor"].as<double>();
        }
    }

    load_datacenters(root["datacenters"], spec);
    load_carbon(root["carbon"], spec);
    load_workload(root["workload"], spec);

    validar_finitude(spec);

    return spec;
}

}  // namespace algosim::scenario

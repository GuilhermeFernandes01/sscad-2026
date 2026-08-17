#include "algosim/scenario/platform_generator.hpp"

#include <cmath>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace algosim::scenario {

namespace {

std::string escape_xml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

double initial_carbon(const ScenarioSpec& spec, const std::string& series_id) {
    const auto it = spec.carbon_series.find(series_id);
    if (it == spec.carbon_series.end() || it->second.gco2_per_kwh.empty()) {
        return 0.0;
    }
    return it->second.gco2_per_kwh.front();
}

}  // namespace

std::filesystem::path
generate_platform_xml(const ScenarioSpec& spec,
                      const std::filesystem::path& output_dir) {
    std::filesystem::create_directories(output_dir);
    // O destino inclui o PID porque o diretorio de saida e compartilhado
    // (/tmp/algosim) e a campanha canonica roda varios processos em paralelo.
    // Com um nome unico por cenario, dois processos renomeariam para o MESMO
    // caminho e um deles poderia carregar o arquivo escrito pelo outro. Hoje
    // isso seria inofensivo apenas por coincidencia (os dois pares de cenarios
    // que compartilham scenario.name descrevem a mesma infraestrutura), e
    // depender de coincidencia nao e garantia.
    const auto stem = spec.name + "_platform." + std::to_string(::getpid());
    const auto xml_path = output_dir / (stem + ".xml");
    // Escrita atomica: emite num temporario e renomeia no fim, verificando
    // antes o estado do fluxo. Uma escrita falha (por exemplo ENOSPC) nunca
    // deve deixar um XML truncado no caminho final.
    const auto tmp_path = output_dir / (stem + ".xml.tmp");
    std::ofstream out{tmp_path};
    if (!out) {
        throw std::runtime_error{"platform_generator: cannot write " + tmp_path.string()};
    }

    out << "<?xml version='1.0'?>\n"
        << "<!DOCTYPE platform SYSTEM \"https://simgrid.org/simgrid.dtd\">\n"
        << "<platform version=\"4.1\">\n";

    if (spec.datacenters.size() == 1) {
        // Single-DC: each host connects to a central router via a private
        // link; Floyd routing discovers shortest paths automatically.
        const auto& dc = spec.datacenters.front();
        const auto  router_id = "router-" + dc.dc_id;
        out << "  <zone id=\"" << escape_xml(dc.dc_id) << "\" routing=\"Floyd\">\n";
        // DTD order: hosts, then links, then routes.
        out << "    <host id=\"" << escape_xml(router_id)
            << "\" speed=\"1Mf\" core=\"1\">\n"
            << "      <prop id=\"wattage_per_state\" value=\"0:0\"/>\n"
            << "      <prop id=\"wattage_off\" value=\"0\"/>\n"
            << "    </host>\n";
        for (const auto& h : dc.hosts) {
            const auto wattage = std::to_string(h.power_idle_w) + ":"
                                 + std::to_string(h.power_peak_w);
            out << "    <host id=\"" << escape_xml(h.host_id) << "\""
                << " speed=\"" << h.cpu_capacity_mips << "Mf\""
                << " core=\"" << h.cpu_cores << "\">\n"
                << "      <prop id=\"wattage_per_state\" value=\"" << wattage << "\"/>\n"
                << "      <prop id=\"wattage_off\" value=\"0\"/>\n"
                << "      <prop id=\"carbon_intensity\" value=\""
                << initial_carbon(spec, dc.carbon_series_id) << "\"/>\n"
                << "      <prop id=\"ram\" value=\"" << h.ram_mb << "\"/>\n"
                << "    </host>\n";
        }
        for (const auto& h : dc.hosts) {
            out << "    <link id=\"link-" << escape_xml(h.host_id)
                << "\" bandwidth=\"" << h.net_bw_mbps
                << "MBps\" latency=\"50us\"/>\n";
        }
        for (const auto& h : dc.hosts) {
            out << "    <route src=\"" << escape_xml(h.host_id)
                << "\" dst=\"" << escape_xml(router_id) << "\">"
                << "<link_ctn id=\"link-" << escape_xml(h.host_id) << "\"/>"
                << "</route>\n";
        }
        out << "  </zone>\n";
    } else {
        // Multi-DC: single flat Floyd zone containing all hosts from all DCs
        // plus one router per DC. Inter-DC traffic goes host→DC_router→DC_router→host.
        out << "  <zone id=\"world\" routing=\"Floyd\">\n";
        // All hosts
        for (const auto& dc : spec.datacenters) {
            const auto router_id = "router-" + dc.dc_id;
            out << "    <host id=\"" << escape_xml(router_id)
                << "\" speed=\"1Mf\" core=\"1\">\n"
                << "      <prop id=\"wattage_per_state\" value=\"0:0\"/>\n"
                << "      <prop id=\"wattage_off\" value=\"0\"/>\n"
                << "    </host>\n";
            for (const auto& h : dc.hosts) {
                const auto wattage = std::to_string(h.power_idle_w) + ":"
                                     + std::to_string(h.power_peak_w);
                out << "    <host id=\"" << escape_xml(h.host_id) << "\""
                    << " speed=\"" << h.cpu_capacity_mips << "Mf\""
                    << " core=\"" << h.cpu_cores << "\">\n"
                    << "      <prop id=\"wattage_per_state\" value=\"" << wattage << "\"/>\n"
                    << "      <prop id=\"wattage_off\" value=\"0\"/>\n"
                    << "      <prop id=\"carbon_intensity\" value=\""
                    << initial_carbon(spec, dc.carbon_series_id) << "\"/>\n"
                    << "      <prop id=\"ram\" value=\"" << h.ram_mb << "\"/>\n"
                    << "    </host>\n";
            }
        }
        // All links: per-host intra-DC + inter-DC
        for (const auto& dc : spec.datacenters) {
            for (const auto& h : dc.hosts) {
                out << "    <link id=\"link-" << escape_xml(h.host_id)
                    << "\" bandwidth=\"" << h.net_bw_mbps
                    << "MBps\" latency=\"10us\"/>\n";
            }
        }
        for (std::size_t i = 0; i < spec.datacenters.size(); ++i) {
            for (std::size_t j = i + 1; j < spec.datacenters.size(); ++j) {
                const auto& a = spec.datacenters[i];
                const auto& b = spec.datacenters[j];
                const double dist_deg = std::sqrt(
                    std::pow(a.latitude - b.latitude, 2)
                    + std::pow(a.longitude - b.longitude, 2));
                const double latency_ms = std::max(1.0, dist_deg * 0.5)
                                          * spec.network.inter_dc_latency_factor;
                out << "    <link id=\"inter-" << escape_xml(a.dc_id)
                    << "-" << escape_xml(b.dc_id)
                    << "\" bandwidth=\"" << spec.network.inter_dc_bw_mbps
                    << "Mbps\" latency=\""
                    << latency_ms << "ms\"/>\n";
            }
        }
        // Routes: host→router (intra-DC) and router→router (inter-DC)
        for (const auto& dc : spec.datacenters) {
            const auto router_id = "router-" + dc.dc_id;
            for (const auto& h : dc.hosts) {
                out << "    <route src=\"" << escape_xml(h.host_id)
                    << "\" dst=\"" << escape_xml(router_id) << "\">"
                    << "<link_ctn id=\"link-" << escape_xml(h.host_id) << "\"/>"
                    << "</route>\n";
            }
        }
        for (std::size_t i = 0; i < spec.datacenters.size(); ++i) {
            for (std::size_t j = i + 1; j < spec.datacenters.size(); ++j) {
                const auto& a = spec.datacenters[i];
                const auto& b = spec.datacenters[j];
                out << "    <route src=\"router-" << escape_xml(a.dc_id)
                    << "\" dst=\"router-" << escape_xml(b.dc_id) << "\">"
                    << "<link_ctn id=\"inter-" << escape_xml(a.dc_id)
                    << "-" << escape_xml(b.dc_id) << "\"/>"
                    << "</route>\n";
            }
        }
        out << "  </zone>\n";
    }

    out << "</platform>\n";
    out.flush();
    if (!out) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        throw std::runtime_error{"platform_generator: incomplete write to "
                                 + tmp_path.string()};
    }
    out.close();
    std::filesystem::rename(tmp_path, xml_path);
    return xml_path;
}

}  // namespace algosim::scenario

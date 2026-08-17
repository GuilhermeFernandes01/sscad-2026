// SimGridBackend: full implementation for SimGrid 4.1.
//
// The orchestrator actor drives the main loop: on each tick it builds a
// ClusterState snapshot, calls the user's placement/migration algorithms,
// applies the decisions (creating VMs, launching workload actors, or
// migrating), then sleeps for dt. Energy and carbon metrics are read from
// the SimGrid plugins between ticks.
//
// Workload inside each VM is modeled as a single actor that continuously
// executes floating-point operations at a rate proportional to the VM's
// cpu_demand_mips. This drives the host's utilization and therefore the
// energy plugin's per-host power calculation.
//
// Timing discipline: the orchestrator must stay on schedule (tick k at
// simulated time k*dt). sg_vm_migrate() BLOCKS the calling actor for the
// whole live-migration transfer, so migrations are issued through detached
// helper actors and the orchestrator sleeps with sleep_until(), never
// sleep_for(). Calling sg_vm_migrate() inline serialized every transfer
// into the orchestrator's own timeline, drifting the SimGrid clock hours
// past the nominal duration and inflating the energy integral above the
// physical ceiling of the platform.
//
// VM destruction vs live migration: the live_migration plugin's RX actor
// clears is_migrating (VirtualMachineImpl::end_migration) BEFORE sending
// the stage-4 ACK back to the sg_vm_migrate() caller, and that ACK is a
// real communication with nonzero simulated latency. Destroying a VM in
// that window frees it while the helper actor still holds a raw pointer
// inside sg_vm_migrate() (the plugin's shutdown callback only kills the
// migration actors while is_migrating is true), a use-after-free that
// crashed or hung high-load migrator runs. Terminate events and the end-of-
// run cleanup therefore defer the destruction of a finalize-window VM to
// its helper actor (MigTracking::pending_destroy). Semantics: the migration
// still counts (it completed), and the terminated VM survives at most one
// ACK latency (sub-second at realistic inter-DC latencies, i.e. << dt)
// longer than the event time; engine.run() ends at most one ACK latency
// past the nominal duration.
//
// Dirty-page model (ressalva R-DIRTY, exp-20260523-cross-base-comparability):
// SimGrid's live_migration plugin retransmits, at each pre-copy stage-2
// round, updated_size = computed_flops * dp_rate bytes (capped at the
// working-set size), with dp_rate = mig_speed * dp_intensity / host_speed
// [bytes/flop] frozen at migration start (VmLiveMigration.cpp). The
// workload exec of a busy VM computes at the source PM's per-core speed
// (== host_speed), so the realized dirty rate is dp_intensity * mig_speed
// bytes/s, scaled down proportionally when the VM computes slower (idle or
// CPU-contended VM dirties less, physically plausible). The backend maps
// domain::VM::dirty_rate_mbps (megabits/s) by setting, at VM creation:
//   mig_speed    = creation-host NIC bandwidth (net_bw_mbps, which the
//                  platform generator emits as MBps), also the rate cap of
//                  every migration transfer of this VM;
//   dp_intensity = dirty_rate / mig_speed  (clamped to [0,1], i.e. the
//                  dirty rate saturates at the NIC speed);
//   working_set  = 90% of RAM (same assumption as sg_vm_create_migratable).
// Stage 2 converges iff dirty rate < the transfer's actual bandwidth;
// otherwise the plugin loops until an internal budget of 1e7 simulated
// seconds before forcing stage 3. The backend does not wait for that: VMs
// destroyed by Terminate events or by the end-of-run cleanup kill their
// migration actors (is_migrating still true -> plugin shutdown callback),
// so a non-convergent migration ends, at the latest, with the run itself
// and never inflates the clock (backend_dirty_pages_nonconvergence_test).
//
// Migration byte accounting: migrations_bytes_mb counts the bytes actually
// carried by the plugin's migration data comms (mailboxes
// "__mbox_mig_src_dst:<vm>(<src>-<dst>)"): Comm::on_send exposes each
// posted chunk's size, credited when that chunk's Comm completes. This
// replaces the earlier static image_size_mb-per-decision estimate, which
// ignored both the real RAM copy and the dirty-page retransmissions.
// Residual: a chunk aborted mid-transfer (timeout, killed migration) is
// not credited, so aborted migrations undercount their partial bytes.
//
// Carbon accounting: gCO2 is integrated here from per-tick energy deltas
// (delta_kwh * intensity(t)) instead of reading the carbon plugin's
// footprint. The plugin re-derives energy as instantaneous-power-at-update
// times elapsed-interval and misses several update events that the energy
// plugin observes (Exec::on_start, VM suspend/resume, speed changes), so
// its footprint drifts from the exact energy integral whenever migrations
// suspend/resume VMs.

#include "algosim/backend/simgrid_backend.hpp"

#include "algosim/metrics/sla.hpp"
#include "algosim/scenario/platform_generator.hpp"

#include <simgrid/Exception.hpp>
#include <simgrid/plugins/energy.h>
#include <simgrid/plugins/live_migration.h>
#include <simgrid/s4u.hpp>
#include <simgrid/s4u/VirtualMachine.hpp>

#if __has_include(<simgrid/plugins/carbon_footprint.h>)
#    include <simgrid/plugins/carbon_footprint.h>
#    define ALGOSIM_HAVE_CARBON_PLUGIN 1
#else
#    define ALGOSIM_HAVE_CARBON_PLUGIN 0
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <simgrid/version.h>

namespace sg4 = simgrid::s4u;

namespace {

// Measures the bytes actually transferred by live-migration data comms
// (see the "Migration byte accounting" header comment). Chunks on one
// mailbox are strictly sequential (one migration at a time per VM), so a
// per-mailbox "last posted size" map pairs each Comm::on_send (which sees
// the chunk size via get_remaining(); on_completion fires with 0) with the
// single completion that follows it.
class MigBytesProbe {
public:
    static MigBytesProbe& instance() {
        static MigBytesProbe probe;
        return probe;
    }

    // Comm signals are process-global: register exactly once, even if
    // run() is invoked again in the same process.
    static void install() {
        static const bool installed = [] {
            sg4::Comm::on_send_cb([](sg4::Comm const& c) {
                if (const auto* name = mig_data_mailbox(c)) {
                    instance().posted_[*name] = c.get_remaining();
                }
            });
            sg4::Comm::on_completion_cb([](sg4::Comm const& c) {
                if (const auto* name = mig_data_mailbox(c)) {
                    auto& probe = instance();
                    auto  it    = probe.posted_.find(*name);
                    if (it != probe.posted_.end()) {
                        probe.completed_bytes_ += it->second;
                        probe.posted_.erase(it);
                    }
                }
            });
            return true;
        }();
        static_cast<void>(installed);
    }

    void reset() {
        posted_.clear();
        completed_bytes_ = 0.0;
    }

    [[nodiscard]] double completed_bytes() const { return completed_bytes_; }

private:
    static const std::string* mig_data_mailbox(const sg4::Comm& c) {
        const auto* mbox = c.get_mailbox();
        if (mbox == nullptr) {
            return nullptr;
        }
        static const std::string prefix = "__mbox_mig_src_dst:";
        const std::string&       name   = mbox->get_name();
        return name.rfind(prefix, 0) == 0 ? &name : nullptr;
    }

    std::unordered_map<std::string, double> posted_;
    double                                  completed_bytes_ = 0.0;
};

}  // namespace

namespace algosim::backend {

std::string simgrid_version_string() {
    int major = 0;
    int minor = 0;
    int patch = 0;
    sg_version_get(&major, &minor, &patch);
    return std::to_string(major) + "." + std::to_string(minor) + "."
           + std::to_string(patch);
}

struct SimGridBackend::Impl {
    const scenario::ScenarioSpec& spec;
    std::filesystem::path         generated_xml;

    explicit Impl(const scenario::ScenarioSpec& s) : spec{s} {}

    // O XML gerado e exclusivo deste processo (sufixo de PID); remove-lo ao
    // final evita acumulo em /tmp/algosim ao longo da campanha. Em caso de
    // queda o arquivo sobrevive, o que ajuda o diagnostico e nao colide com
    // nenhum outro processo.
    ~Impl() {
        if (!generated_xml.empty()) {
            std::error_code ec;
            std::filesystem::remove(generated_xml, ec);
        }
    }
};

SimGridBackend::SimGridBackend(const scenario::ScenarioSpec& spec)
    : impl_{std::make_unique<Impl>(spec)} {
    if (spec.platform_xml_path.empty()
        || !std::filesystem::exists(spec.platform_xml_path)) {
        impl_->generated_xml = scenario::generate_platform_xml(
            spec, std::filesystem::temp_directory_path() / "algosim");
    }
}

SimGridBackend::~SimGridBackend() = default;

RunResult SimGridBackend::run(algorithms::PlacementAlgorithm& placement,
                              algorithms::MigrationAlgorithm* migration) {
    const auto& spec = impl_->spec;

    // Plugin init MUST come before Engine construction.
    sg_host_energy_plugin_init();
    sg_vm_live_migration_plugin_init();
    MigBytesProbe::install();
    MigBytesProbe::instance().reset();
#if ALGOSIM_HAVE_CARBON_PLUGIN
    sg_host_carbon_footprint_plugin_init();
#endif

    int   fake_argc    = 1;
    char  fake_argv0[] = "algosim";
    char* fake_argvp[] = {fake_argv0, nullptr};
    sg4::Engine engine(&fake_argc, fake_argvp);

    const auto xml_path = impl_->generated_xml.empty()
                              ? spec.platform_xml_path
                              : impl_->generated_xml.string();
    engine.load_platform(xml_path);

#if ALGOSIM_HAVE_CARBON_PLUGIN
    for (const auto& [series_id, series] : spec.carbon_series) {
        for (const auto& dc : spec.datacenters) {
            if (dc.carbon_series_id != series_id) {
                continue;
            }
            for (const auto& h : dc.hosts) {
                auto* sg_host = sg4::Host::by_name(h.host_id);
                sg_host_set_carbon_intensity(sg_host, series.gco2_per_kwh.front());
            }
        }
    }
#endif

    // Shared state between orchestrator actor and the result collection.
    metrics::MetricCollector   collector{spec};
    std::vector<DecisionRecord> all_decisions;

    // Tracks which VMs are alive and their SimGrid objects.
    struct VmRecord {
        domain::VM                definition;
        sg4::VirtualMachine*      sg_vm = nullptr;
    };

    // Build event schedule: events sorted by time, grouped into buckets per tick.
    const auto& events = spec.events;

    // NIC bandwidth per host (MBps, as emitted by the platform generator);
    // source of each VM's migration speed (see dirty-page header comment).
    std::unordered_map<std::string, double> host_net_bw_mbps;
    for (const auto& dc_spec : spec.datacenters) {
        for (const auto& h_spec : dc_spec.hosts) {
            host_net_bw_mbps.emplace(h_spec.host_id, h_spec.net_bw_mbps);
        }
    }

    // The orchestrator lambda captures everything by reference. This is safe
    // because engine.run() blocks until the actor finishes, so all referenced
    // objects are alive.
    const auto anchor_host_id = spec.datacenters.front().hosts.front().host_id;

    // In-flight live migrations. Shared (via shared_ptr) between the
    // orchestrator and the detached migration actors, so no reference ever
    // dangles regardless of which one finishes first.
    struct MigTracking {
        // vm_id -> (source_host_id, target_host_id)
        std::unordered_map<std::string, std::pair<std::string, std::string>> inflight;
        // host_id -> number of in-flight migrations touching this host.
        std::unordered_map<std::string, int> host_refs;
        // VMs whose Terminate arrived inside the migration finalize window
        // (RX already cleared is_migrating, stage-4 ACK still in flight).
        // Destroying such a VM immediately frees it while the helper actor
        // is still dereferencing it inside sg_vm_migrate() (use-after-free:
        // SIGSEGV or heap corruption that hangs engine.run()). The destroy
        // is deferred to the helper instead.
        std::unordered_set<std::string> pending_destroy;

        void release_host(const std::string& host_id) {
            auto it = host_refs.find(host_id);
            if (it != host_refs.end() && --it->second <= 0) {
                host_refs.erase(it);
            }
        }
        // Removes the VM's in-flight entry (if any) and its host pins.
        void release_vm(const std::string& vm_id) {
            auto node = inflight.extract(vm_id);
            if (!node.empty()) {
                release_host(node.mapped().first);
                release_host(node.mapped().second);
            }
        }
    };

    auto orchestrator = [&]() {
        std::unordered_map<std::string, VmRecord> live_vms;
        // Reverse index: host_id -> set of vm_ids on that host.
        std::unordered_map<std::string, std::vector<std::string>> host_to_vms;
        std::unordered_set<std::string>           terminated_vms;
        std::size_t                               event_cursor = 0;
        std::size_t                               total_migrations = 0;
        std::size_t                               total_sla_violations = 0;
        std::size_t                               total_unplaced_vms   = 0;

        auto  mig_track      = std::make_shared<MigTracking>();
        auto* mig_actor_host = sg4::Host::by_name(anchor_host_id);
        // Exact cumulative energy per host (J) and carbon per DC (g),
        // integrated tick by tick from the energy plugin's deltas.
        std::unordered_map<std::string, double> prev_host_joules;
        std::unordered_map<std::string, double> gco2_cum_by_dc;
        std::unordered_map<std::string, double> intensity_by_dc;

        const double dt     = spec.dt_seconds;
        const double mig_iv = spec.migration_interval_seconds;

        for (double t = 0.0; t < spec.duration_seconds; t += dt) {
            // 1. Apply workload events in [t, t+dt).
            std::vector<domain::VM> pending;
            while (event_cursor < events.size()
                   && events[event_cursor].t_seconds < t + dt) {
                const auto& ev = events[event_cursor];
                if (ev.kind == domain::EventKind::Submit) {
                    pending.push_back(ev.vm);
                } else if (ev.kind == domain::EventKind::Terminate) {
                    auto it = live_vms.find(ev.vm.vm_id);
                    if (it != live_vms.end()) {
                        if (it->second.definition.host_id) {
                            auto& hvec = host_to_vms[*it->second.definition.host_id];
                            hvec.erase(std::remove(hvec.begin(), hvec.end(), ev.vm.vm_id), hvec.end());
                        }
                        if (mig_track->inflight.count(ev.vm.vm_id) > 0
                            && sg_vm_is_migrating(it->second.sg_vm) == 0) {
                            // Migration finalize handshake in progress: the
                            // RX side already cleared is_migrating but the
                            // helper is still inside sg_vm_migrate() waiting
                            // for the stage-4 ACK. The plugin's shutdown
                            // callback would NOT kill it (is_migrating is
                            // false), so destroying the VM now frees it under
                            // the helper's feet. Defer the destroy to the
                            // helper; host pins stay until it releases them.
                            mig_track->pending_destroy.insert(ev.vm.vm_id);
                        } else {
                            // Mid-transfer (or not migrating at all):
                            // destroying the VM is safe; the live_migration
                            // plugin kills the TX/RX/helper actors. Drop our
                            // in-flight bookkeeping first so the helper
                            // cannot double-release.
                            mig_track->release_vm(ev.vm.vm_id);
                            it->second.sg_vm->destroy();
                        }
                        terminated_vms.insert(ev.vm.vm_id);
                        live_vms.erase(it);
                    }
                }
                ++event_cursor;
            }

            // 2. Build ClusterState snapshot.
            domain::ClusterState state;
            state.t_seconds = t;
            state.rng_seed  = spec.seed;
            state.wall_datetime = spec.start_datetime
                + std::chrono::seconds(static_cast<long long>(t));

            for (const auto& dc_spec : spec.datacenters) {
                domain::Datacenter dc;
                dc.dc_id            = dc_spec.dc_id;
                dc.name             = dc_spec.name;
                dc.latitude         = dc_spec.latitude;
                dc.longitude        = dc_spec.longitude;
                dc.pue              = dc_spec.pue;
                dc.carbon_series_id = dc_spec.carbon_series_id;

                for (const auto& h_spec : dc_spec.hosts) {
                    domain::Host h;
                    h.host_id           = h_spec.host_id;
                    h.dc_id             = h_spec.dc_id;
                    h.cpu_cores         = h_spec.cpu_cores;
                    h.cpu_capacity_mips = h_spec.cpu_capacity_mips;
                    h.ram_mb            = h_spec.ram_mb;
                    h.disk_gb           = h_spec.disk_gb;
                    h.net_bw_mbps       = h_spec.net_bw_mbps;
                    h.power_idle_w      = h_spec.power_idle_w;
                    h.power_peak_w      = h_spec.power_peak_w;
                    // Algorithms see ALL hosts as available; the backend
                    // handles power on/off transparently. This prevents
                    // artificial convergence where all algorithms only see
                    // the same small pool of active hosts.
                    h.active = true;
                    // CPU usage is DEMAND-based: SimGrid accounts VM load on
                    // the VM, not the host, so measured host load is always
                    // ~0 and would let placement overcommit CPU without
                    // bound (only RAM would bind).
                    auto h2v_it = host_to_vms.find(h.host_id);
                    if (h2v_it != host_to_vms.end()) {
                        for (const auto& vm_id : h2v_it->second) {
                            auto vm_it = live_vms.find(vm_id);
                            if (vm_it != live_vms.end()) {
                                h.vms.push_back(vm_id);
                                h.ram_used_mb += vm_it->second.definition.ram_mb;
                                h.cpu_used_mips += vm_it->second.definition.cpu_demand_mips;
                            }
                        }
                    }
                    dc.hosts.push_back(std::move(h));
                }

                // Carbon intensity lookup.
                auto cs_it = spec.carbon_series.find(dc.carbon_series_id);
                if (cs_it != spec.carbon_series.end()) {
                    state.carbon_now[dc.dc_id] = cs_it->second.at(state.wall_datetime);

                    if (spec.carbon_forecast_hours > 0) {
                        std::vector<double> forecast;
                        forecast.reserve(static_cast<std::size_t>(spec.carbon_forecast_hours));
                        for (int fh = 1; fh <= spec.carbon_forecast_hours; ++fh) {
                            auto future_ts = state.wall_datetime
                                + std::chrono::seconds(static_cast<long long>(fh) * 3600LL);
                            forecast.push_back(cs_it->second.at(future_ts));
                        }
                        state.carbon_forecast[dc.dc_id] = std::move(forecast);
                    }
                }
                state.forecast_hours = spec.carbon_forecast_hours;

                const double intensity = state.carbon_now.count(dc.dc_id)
                                             ? state.carbon_now.at(dc.dc_id)
                                             : 0.0;
                // Intensity used to integrate carbon over [t, t+dt).
                intensity_by_dc[dc.dc_id] = intensity;
#if ALGOSIM_HAVE_CARBON_PLUGIN
                for (const auto& h : dc.hosts) {
                    auto* sg_host = sg4::Host::by_name(h.host_id);
                    sg_host_set_carbon_intensity(sg_host, intensity);
                }
#endif

                state.datacenters.push_back(std::move(dc));
            }

            state.pending_vms = std::move(pending);
            for (const auto& [vm_id, rec] : live_vms) {
                state.running_vms.push_back(rec.definition);
            }

            // 3. Call placement algorithm.
            const auto t0_wall = std::chrono::steady_clock::now();
            auto place_decisions = placement.place(state);
            long long algo_us = 0;

            // 4. Call migration algorithm.
            std::vector<domain::MigrationDecision> mig_decisions;
            if (migration != nullptr && t > 0.0
                && std::fmod(t, mig_iv) < dt) {
                mig_decisions = migration->migrate(state);
            }
            const auto t1_wall = std::chrono::steady_clock::now();
            algo_us = std::chrono::duration_cast<std::chrono::microseconds>(
                          t1_wall - t0_wall).count();

            // 5. Apply placement decisions: create VMs with workload actors.
            std::size_t placed_this_tick = 0;
            for (const auto& d : place_decisions) {
                const auto* vm_ptr = state.pending_vm(d.vm_id);
                if (vm_ptr == nullptr) {
                    continue;
                }
                ++placed_this_tick;
                auto* sg_host = sg4::Host::by_name(d.target_host_id);
                // Ensure the host is powered on before creating a VM on it.
                // Algorithms see all hosts as available; the backend handles
                // the physical power state transparently.
                if (!sg_host->is_on()) {
                    sg_host->turn_on();
                }
                auto* sg_vm   = sg_host->create_vm(d.vm_id, vm_ptr->cpu_cores);
                const auto ram_bytes =
                    static_cast<size_t>(vm_ptr->ram_mb) * 1024U * 1024U;
                sg_vm->set_ramsize(ram_bytes);
                // Wire dirty_rate_mbps (megabits/s) into the live_migration
                // plugin's pre-copy model; see the dirty-page header
                // comment for the mapping and its assumptions.
                const auto bw_it = host_net_bw_mbps.find(d.target_host_id);
                const double mig_speed_bytes_s =
                    bw_it != host_net_bw_mbps.end() ? bw_it->second * 1e6 : 0.0;
                if (mig_speed_bytes_s > 0.0) {
                    const double dirty_bytes_s =
                        vm_ptr->dirty_rate_mbps * (1e6 / 8.0);
                    sg_vm_set_migration_speed(sg_vm, mig_speed_bytes_s);
                    sg_vm_set_dirty_page_intensity(
                        sg_vm,
                        std::clamp(dirty_bytes_s / mig_speed_bytes_s, 0.0, 1.0));
                    sg_vm_set_working_set_memory(
                        sg_vm, static_cast<sg_size_t>(0.9 * static_cast<double>(ram_bytes)));
                }
                sg_vm->start();

                // Single long-running execute: one event per VM for the
                // entire simulation duration. Avoids O(duration * n_vms)
                // event pressure that a loop-based actor would create.
                double demand    = vm_ptr->cpu_demand_mips;
                double total_sim = spec.duration_seconds;
                sg_vm->add_actor("workload-" + d.vm_id, [demand, total_sim]() {
                    sg4::this_actor::execute(demand * 1e6 * total_sim * 2.0);
                });

                VmRecord rec;
                rec.definition      = *vm_ptr;
                rec.definition.host_id = d.target_host_id;
                rec.sg_vm           = sg_vm;
                live_vms[d.vm_id]   = rec;
                host_to_vms[d.target_host_id].push_back(d.vm_id);
                all_decisions.push_back({t, "placement", d, {}});
            }
            // Pending VMs without a placement decision are dropped by the
            // event loop; count them instead of losing them silently.
            total_unplaced_vms += state.pending_vms.size() - placed_this_tick;

            // 6. Apply migration decisions, asynchronously. sg_vm_migrate()
            //    blocks its caller for the whole transfer, so each migration
            //    runs in a detached helper actor and overlaps the simulation
            //    instead of freezing the orchestrator's timeline.
            for (const auto& m : mig_decisions) {
                auto it = live_vms.find(m.vm_id);
                if (it == live_vms.end()) {
                    continue;
                }
                // A VM can only run one live migration at a time; drop
                // decisions for VMs still in flight.
                if (mig_track->inflight.count(m.vm_id) > 0) {
                    continue;
                }
                // Update host_to_vms index.
                auto& src_vec = host_to_vms[m.source_host_id];
                src_vec.erase(std::remove(src_vec.begin(), src_vec.end(), m.vm_id), src_vec.end());
                host_to_vms[m.target_host_id].push_back(m.vm_id);

                auto* target = sg4::Host::by_name(m.target_host_id);
                if (!target->is_on()) {
                    target->turn_on();
                }
                mig_track->inflight.emplace(
                    m.vm_id, std::make_pair(m.source_host_id, m.target_host_id));
                ++mig_track->host_refs[m.source_host_id];
                ++mig_track->host_refs[m.target_host_id];

                auto*             sg_vm = it->second.sg_vm;
                const std::string vm_id = m.vm_id;
                // Helper actors live on the anchor host, which is never
                // powered off. On forceful kill (VM destroyed mid-flight)
                // the bookkeeping was already released by the killer.
                mig_actor_host->add_actor(
                    "mig-" + vm_id + "-" + std::to_string(total_migrations),
                    [mig_track, sg_vm, target, vm_id]() {
                        try {
                            sg_vm_migrate(sg_vm, target);
                        } catch (const simgrid::VmFailureException&) {
                            // Migration refused (host off, VM not running,
                            // ...): keep the simulation going; bookkeeping
                            // is released below either way.
                        }
                        // A Terminate that arrived inside the finalize
                        // window (see MigTracking::pending_destroy) was
                        // deferred to this point: sg_vm_migrate() has fully
                        // returned, so destroying the VM is now safe.
                        // Destroy before releasing the host pins so 6b can
                        // never power off the involved hosts in between.
                        if (mig_track->pending_destroy.erase(vm_id) > 0) {
                            sg_vm->destroy();
                        }
                        mig_track->release_vm(vm_id);
                    });

                it->second.definition.host_id = m.target_host_id;
                ++total_migrations;
                all_decisions.push_back({t, "migration", {}, m});
            }

            // 6b. Power-manage hosts: turn off empty hosts, turn on hosts
            //     that just received a VM. This is what makes BestFit
            //     outperform FirstFit: consolidation frees idle hosts.
            for (const auto& dc_spec : spec.datacenters) {
                for (const auto& h_spec : dc_spec.hosts) {
                    if (h_spec.host_id == anchor_host_id) {
                        continue;
                    }
                    // Never power off a host involved in an in-flight
                    // migration: turning off src/dst kills the transfer.
                    if (mig_track->host_refs.count(h_spec.host_id) > 0) {
                        continue;
                    }
                    auto* sg_host = sg4::Host::by_name(h_spec.host_id);
                    auto  h2v_it  = host_to_vms.find(h_spec.host_id);
                    bool  has_vms = h2v_it != host_to_vms.end() && !h2v_it->second.empty();
                    if (!has_vms && sg_host->is_on()) {
                        sg_host->turn_off();
                    } else if (has_vms && !sg_host->is_on()) {
                        sg_host->turn_on();
                    }
                }
            }

            // 7. Advance simulation to the next tick boundary. sleep_until
            //    (not sleep_for) keeps the orchestrator aligned with the
            //    logical schedule even if a step consumed simulated time.
            sg4::this_actor::sleep_until(t + dt);

            // 8. Collect metrics from SimGrid plugins.
            metrics::BackendMetrics raw;
            raw.migrations_total   = total_migrations;
            // Bytes actually transferred by migration data comms (MiB):
            // measured, not the static image-size estimate (see header).
            raw.migrations_bytes_mb =
                MigBytesProbe::instance().completed_bytes() / (1024.0 * 1024.0);
            raw.unplaced_vms_total  = total_unplaced_vms;

            for (const auto& dc_spec : spec.datacenters) {
                double  dc_kwh  = 0.0;
                double& dc_gco2 = gco2_cum_by_dc[dc_spec.dc_id];
                const double intensity = intensity_by_dc.count(dc_spec.dc_id)
                                             ? intensity_by_dc.at(dc_spec.dc_id)
                                             : 0.0;
                for (const auto& h_spec : dc_spec.hosts) {
                    auto* sg_host = sg4::Host::by_name(h_spec.host_id);
                    const double joules = sg_host_get_consumed_energy(sg_host);
                    dc_kwh += joules / 3.6e6;
                    // Carbon integrated from the exact energy delta of this
                    // tick times the intensity that was in force over
                    // [t, t+dt); see the header note on carbon accounting.
                    double& prev = prev_host_joules[h_spec.host_id];
                    dc_gco2 += (joules - prev) / 3.6e6 * intensity;
                    prev = joules;
                }
                raw.kwh_by_dc[dc_spec.dc_id]  = dc_kwh;
                raw.gco2_by_dc[dc_spec.dc_id] = dc_gco2;
            }

            // Re-snapshot for the collector (post-step state).
            domain::ClusterState post_state;
            post_state.t_seconds     = t + dt;
            post_state.wall_datetime = state.wall_datetime
                + std::chrono::seconds(static_cast<long long>(dt));
            for (const auto& dc_spec : spec.datacenters) {
                domain::Datacenter dc = dc_spec;
                for (auto& h : dc.hosts) {
                    h.active = true;
                    h.vms.clear();
                    h.ram_used_mb   = 0;
                    h.cpu_used_mips = 0.0;
                    auto h2v_post = host_to_vms.find(h.host_id);
                    if (h2v_post != host_to_vms.end()) {
                        for (const auto& vm_id : h2v_post->second) {
                            auto vm_it = live_vms.find(vm_id);
                            if (vm_it != live_vms.end()) {
                                h.vms.push_back(vm_id);
                                h.ram_used_mb += vm_it->second.definition.ram_mb;
                                h.cpu_used_mips += vm_it->second.definition.cpu_demand_mips;
                            }
                        }
                    }
                }
                post_state.datacenters.push_back(std::move(dc));
            }
            for (const auto& [vm_id, rec] : live_vms) {
                post_state.running_vms.push_back(rec.definition);
            }
            total_sla_violations += metrics::sla_overcommit_vm_ticks(post_state);
            raw.sla_violations_tick = total_sla_violations;
            collector.record(post_state, raw, algo_us);
        }

        // Clean up remaining VMs. Destroying a VM mid-migration also kills
        // its migration actors (live_migration plugin); release our
        // bookkeeping first, as in the Terminate path. VMs caught in the
        // migration finalize window are deferred to their helper actor
        // (same UAF hazard as in the Terminate path); the helper destroys
        // them as soon as the stage-4 ACK lands, so engine.run() returns at
        // most one ACK latency past the nominal duration.
        for (auto& [vm_id, rec] : live_vms) {
            if (mig_track->inflight.count(vm_id) > 0
                && sg_vm_is_migrating(rec.sg_vm) == 0) {
                mig_track->pending_destroy.insert(vm_id);
                continue;
            }
            mig_track->release_vm(vm_id);
            rec.sg_vm->destroy();
        }
    };

    // Register the orchestrator on the first host of the first datacenter.
    const auto& first_host_id = spec.datacenters.front().hosts.front().host_id;
    auto*       anchor_host   = sg4::Host::by_name(first_host_id);
    anchor_host->add_actor("orchestrator", orchestrator);

    engine.run();

    return RunResult{
        .time_series = collector.time_series(),
        .summary     = collector.summary(),
        .decisions   = std::move(all_decisions),
    };
}

}  // namespace algosim::backend

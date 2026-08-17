add_executable(algosim_tests
    unit/algorithms/first_fit_test.cpp
    unit/algorithms/best_fit_test.cpp
    unit/algorithms/worst_fit_test.cpp
    unit/algorithms/round_robin_test.cpp
    unit/algorithms/ffd_test.cpp
    unit/algorithms/bfd_test.cpp
    unit/algorithms/lowest_carbon_dc_test.cpp
    unit/algorithms/follow_renewables_test.cpp
    unit/algorithms/wsnb_test.cpp
    unit/algorithms/followme_s_test.cpp
    unit/algorithms/cnemesis_test.cpp
    unit/algorithms/tiebreak_test.cpp
    unit/algorithms/registry_test.cpp)

target_include_directories(algosim_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/algorithms)

target_link_libraries(algosim_tests PRIVATE
    algosim::domain
    algosim::algorithms
    Catch2::Catch2WithMain)

algosim_set_warnings(algosim_tests)

add_executable(algosim_scenario_tests
    unit/scenario/trace_workload_test.cpp
    unit/scenario/network_profile_test.cpp
    unit/scenario/carbon_anchor_test.cpp
    unit/scenario/platform_atomic_write_test.cpp
    unit/scenario/event_order_test.cpp)

target_link_libraries(algosim_scenario_tests PRIVATE
    algosim::scenario
    algosim::domain
    Catch2::Catch2WithMain)

algosim_set_warnings(algosim_scenario_tests)

add_executable(algosim_metrics_tests
    unit/metrics/sla_test.cpp)

target_link_libraries(algosim_metrics_tests PRIVATE
    algosim::metrics
    algosim::domain
    Catch2::Catch2WithMain)

algosim_set_warnings(algosim_metrics_tests)

add_executable(algosim_runner_tests
    unit/runner/results_writer_test.cpp)

target_link_libraries(algosim_runner_tests PRIVATE
    algosim::runner
    algosim::domain
    Catch2::Catch2WithMain)

algosim_set_warnings(algosim_runner_tests)

if(ALGOSIM_ENABLE_SIMGRID)
    add_executable(algosim_backend_tests
        integration/backend_demand_accounting_test.cpp)

    target_link_libraries(algosim_backend_tests PRIVATE
        algosim::backend
        algosim::algorithms
        algosim::scenario
        algosim::metrics
        algosim::domain
        Catch2::Catch2WithMain)

    algosim_set_warnings(algosim_backend_tests)

    # Separate executable: each SimGrid Engine must live in its own process.
    add_executable(algosim_energy_ceiling_tests
        integration/backend_energy_ceiling_test.cpp)

    target_link_libraries(algosim_energy_ceiling_tests PRIVATE
        algosim::backend
        algosim::algorithms
        algosim::scenario
        algosim::metrics
        algosim::domain
        Catch2::Catch2WithMain)

    algosim_set_warnings(algosim_energy_ceiling_tests)

    # Separate executable: each SimGrid Engine must live in its own process.
    add_executable(algosim_migration_terminate_tests
        integration/backend_migration_terminate_test.cpp)

    target_link_libraries(algosim_migration_terminate_tests PRIVATE
        algosim::backend
        algosim::algorithms
        algosim::scenario
        algosim::metrics
        algosim::domain
        Catch2::Catch2WithMain)

    algosim_set_warnings(algosim_migration_terminate_tests)

    # Separate executable: each SimGrid Engine must live in its own process.
    add_executable(algosim_dirty_pages_transfer_tests
        integration/backend_dirty_pages_transfer_test.cpp)

    target_link_libraries(algosim_dirty_pages_transfer_tests PRIVATE
        algosim::backend
        algosim::algorithms
        algosim::scenario
        algosim::metrics
        algosim::domain
        Catch2::Catch2WithMain)

    algosim_set_warnings(algosim_dirty_pages_transfer_tests)

    # Separate executable: each SimGrid Engine must live in its own process.
    add_executable(algosim_dirty_pages_nonconvergence_tests
        integration/backend_dirty_pages_nonconvergence_test.cpp)

    target_link_libraries(algosim_dirty_pages_nonconvergence_tests PRIVATE
        algosim::backend
        algosim::algorithms
        algosim::scenario
        algosim::metrics
        algosim::domain
        Catch2::Catch2WithMain)

    algosim_set_warnings(algosim_dirty_pages_nonconvergence_tests)
endif()

include(Catch)
catch_discover_tests(algosim_tests)
catch_discover_tests(algosim_scenario_tests)
catch_discover_tests(algosim_runner_tests)
catch_discover_tests(algosim_metrics_tests)
if(ALGOSIM_ENABLE_SIMGRID)
    catch_discover_tests(algosim_backend_tests)
    catch_discover_tests(algosim_energy_ceiling_tests)
    # TIMEOUT converts the hang manifestation of the finalize-window UAF
    # into a test failure instead of blocking the suite.
    catch_discover_tests(algosim_migration_terminate_tests
        PROPERTIES TIMEOUT 300)
    # TIMEOUT converts a non-terminating engine.run() (e.g. a migration
    # riding the live_migration plugin's 1e7 s internal budget) into a
    # test failure instead of blocking the suite.
    catch_discover_tests(algosim_dirty_pages_transfer_tests
        PROPERTIES TIMEOUT 300)
    catch_discover_tests(algosim_dirty_pages_nonconvergence_tests
        PROPERTIES TIMEOUT 300)
endif()

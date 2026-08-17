add_library(algosim_metrics metric_collector.cpp sla.cpp)
add_library(algosim::metrics ALIAS algosim_metrics)

target_include_directories(algosim_metrics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

target_link_libraries(algosim_metrics
    PUBLIC algosim::domain algosim::scenario)

target_compile_features(algosim_metrics PUBLIC cxx_std_20)
algosim_set_warnings(algosim_metrics)

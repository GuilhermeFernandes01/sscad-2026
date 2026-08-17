add_library(algosim_scenario
    carbon_loader.cpp
    platform_generator.cpp
    scenario_builder.cpp)
add_library(algosim::scenario ALIAS algosim_scenario)

target_include_directories(algosim_scenario PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

target_link_libraries(algosim_scenario
    PUBLIC  algosim::domain
    PRIVATE yaml-cpp::yaml-cpp)

target_compile_features(algosim_scenario PUBLIC cxx_std_20)
algosim_set_warnings(algosim_scenario)

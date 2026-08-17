add_library(algosim_algorithms
    first_fit.cpp
    best_fit.cpp
    worst_fit.cpp
    round_robin.cpp
    ffd.cpp
    bfd.cpp
    lowest_carbon_dc.cpp
    follow_renewables.cpp
    wsnb.cpp
    followme_s.cpp
    cnemesis.cpp
    registry.cpp)
add_library(algosim::algorithms ALIAS algosim_algorithms)

target_include_directories(algosim_algorithms PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

target_link_libraries(algosim_algorithms PUBLIC algosim::domain)
target_compile_features(algosim_algorithms PUBLIC cxx_std_20)

algosim_set_warnings(algosim_algorithms)

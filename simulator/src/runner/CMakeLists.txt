add_library(algosim_runner results_writer.cpp build_info.cpp)
add_library(algosim::runner ALIAS algosim_runner)

# Git provenance header, regenerated on every build so the compiled-in SHA
# never goes stale across commits.
set(ALGOSIM_GIT_INFO_HPP "${CMAKE_BINARY_DIR}/generated/algosim_git_info.hpp")
add_custom_target(algosim_git_info
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DOUT_FILE=${ALGOSIM_GIT_INFO_HPP}
        -P ${CMAKE_SOURCE_DIR}/cmake/GitInfo.cmake
    BYPRODUCTS ${ALGOSIM_GIT_INFO_HPP}
    COMMENT "Refreshing git provenance header")
add_dependencies(algosim_runner algosim_git_info)

target_include_directories(algosim_runner PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
target_include_directories(algosim_runner PRIVATE
    "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(algosim_runner
    PUBLIC  algosim::domain algosim::metrics algosim::scenario
    PRIVATE nlohmann_json::nlohmann_json)

target_compile_features(algosim_runner PUBLIC cxx_std_20)
algosim_set_warnings(algosim_runner)

# The CLI executable is only built when the SimGrid backend is available.
if(ALGOSIM_ENABLE_SIMGRID)
    add_executable(algosim main.cpp)
    target_compile_definitions(algosim PRIVATE ALGOSIM_WITH_SIMGRID=1)
    target_link_libraries(algosim PRIVATE
        algosim::runner
        algosim::algorithms
        algosim::scenario
        algosim::metrics
        algosim::backend)
    algosim_set_warnings(algosim)
endif()

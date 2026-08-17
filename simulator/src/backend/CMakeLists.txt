add_library(algosim_backend_simgrid simgrid_backend.cpp)
add_library(algosim::backend ALIAS algosim_backend_simgrid)

target_include_directories(algosim_backend_simgrid PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

target_link_libraries(algosim_backend_simgrid
    PUBLIC algosim::domain algosim::metrics algosim::scenario algosim::algorithms)

# SimGrid linkage: prefer the modern imported target, fall back to pkg-config.
if(TARGET SimGrid::SimGrid)
    target_link_libraries(algosim_backend_simgrid PUBLIC SimGrid::SimGrid)
elseif(TARGET PkgConfig::SimGrid)
    target_link_libraries(algosim_backend_simgrid PUBLIC PkgConfig::SimGrid)
elseif(SimGrid_INCLUDE_DIRS OR SimGrid_LIBRARIES)
    target_include_directories(algosim_backend_simgrid PUBLIC ${SimGrid_INCLUDE_DIRS})
    target_link_libraries(algosim_backend_simgrid PUBLIC ${SimGrid_LIBRARIES})
else()
    message(FATAL_ERROR "ALGOSIM_ENABLE_SIMGRID=ON but no SimGrid target or vars found")
endif()

target_compile_features(algosim_backend_simgrid PUBLIC cxx_std_20)
algosim_set_warnings(algosim_backend_simgrid)

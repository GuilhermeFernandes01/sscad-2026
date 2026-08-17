# domain is header-only; we expose it as an INTERFACE library so consumers
# get the include path via target_link_libraries.

add_library(algosim_domain INTERFACE)
add_library(algosim::domain ALIAS algosim_domain)

target_include_directories(algosim_domain INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

target_compile_features(algosim_domain INTERFACE cxx_std_20)

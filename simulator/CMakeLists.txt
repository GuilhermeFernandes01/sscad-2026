cmake_minimum_required(VERSION 3.20)

project(algosim
    VERSION 0.1.0
    DESCRIPTION "Comparative framework for VM migration algorithms under CO2 objective"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# ---- Options ---------------------------------------------------------------

option(ALGOSIM_ENABLE_SIMGRID "Build the SimGrid backend and CLI executable" ON)
option(ALGOSIM_BUILD_TESTS    "Build unit and integration tests"             ON)
option(ALGOSIM_WARNINGS_AS_ERRORS "Treat warnings as errors"                 OFF)

# ---- Global defaults -------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CompilerWarnings)

# ---- Dependencies ----------------------------------------------------------

include(FetchContent)

# yaml-cpp for scenario configs
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp not found system-wide; fetching via FetchContent")
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
    )
    set(YAML_CPP_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS  OFF CACHE BOOL "" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# nlohmann_json for output serialisation
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found system-wide; fetching via FetchContent")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# SimGrid: optional, only needed for the backend + CLI
if(ALGOSIM_ENABLE_SIMGRID)
    find_package(SimGrid 4.1 QUIET)
    if(NOT SimGrid_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SimGrid IMPORTED_TARGET simgrid)
        endif()
    endif()
    if(NOT SimGrid_FOUND)
        message(WARNING "SimGrid not found; disabling ALGOSIM_ENABLE_SIMGRID. "
                        "Set CMAKE_PREFIX_PATH=<prefix> to point to your SimGrid install.")
        set(ALGOSIM_ENABLE_SIMGRID OFF CACHE BOOL "" FORCE)
    endif()
endif()

# Catch2 for tests
if(ALGOSIM_BUILD_TESTS)
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        message(STATUS "Catch2 v3 not found; fetching via FetchContent")
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.5.3
        )
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    endif()
    include(CTest)
    enable_testing()
endif()

# ---- Subdirectories --------------------------------------------------------

add_subdirectory(src/domain)
add_subdirectory(src/algorithms)
add_subdirectory(src/scenario)
add_subdirectory(src/metrics)

if(ALGOSIM_ENABLE_SIMGRID)
    add_subdirectory(src/backend)
endif()

add_subdirectory(src/runner)

if(ALGOSIM_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ---- Summary ---------------------------------------------------------------

message(STATUS "")
message(STATUS "algosim ${PROJECT_VERSION} configuration:")
message(STATUS "  C++ standard:        ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "  SimGrid backend:     ${ALGOSIM_ENABLE_SIMGRID}")
message(STATUS "  Build tests:         ${ALGOSIM_BUILD_TESTS}")
message(STATUS "  Warnings as errors:  ${ALGOSIM_WARNINGS_AS_ERRORS}")
message(STATUS "")

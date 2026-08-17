# GitInfo.cmake: regenerates the git provenance header at build time.
# Invoked as: cmake -DSRC_DIR=<repo> -DOUT_FILE=<hpp> -P GitInfo.cmake
# Only rewrites OUT_FILE when the content changes, so incremental builds
# stay incremental.

execute_process(
    COMMAND git -C "${SRC_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE ALGOSIM_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR ALGOSIM_GIT_SHA STREQUAL "")
    set(ALGOSIM_GIT_SHA "unknown")
endif()

execute_process(
    COMMAND git -C "${SRC_DIR}" status --porcelain --untracked-files=no
    OUTPUT_VARIABLE _dirty_out
    ERROR_QUIET)
if(_dirty_out STREQUAL "")
    set(ALGOSIM_GIT_DIRTY 0)
else()
    set(ALGOSIM_GIT_DIRTY 1)
endif()

set(_content "#pragma once
#define ALGOSIM_GIT_SHA \"${ALGOSIM_GIT_SHA}\"
#define ALGOSIM_GIT_DIRTY ${ALGOSIM_GIT_DIRTY}
")

set(_old "")
if(EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" _old)
endif()
if(NOT _old STREQUAL _content)
    file(WRITE "${OUT_FILE}" "${_content}")
endif()

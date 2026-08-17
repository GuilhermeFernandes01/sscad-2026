# CompilerWarnings.cmake: applies a strict, opinionated set of warnings to any
# target via algosim_set_warnings(<target>).

function(algosim_set_warnings target)
    set(gcc_like_flags
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough)

    set(msvc_flags /W4 /permissive-)

    if(ALGOSIM_WARNINGS_AS_ERRORS)
        list(APPEND gcc_like_flags -Werror)
        list(APPEND msvc_flags /WX)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE ${gcc_like_flags})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE ${msvc_flags})
    endif()
endfunction()

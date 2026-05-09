cmake_minimum_required(VERSION 3.14)
project(firmware_ut C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

# ── paths ────────────────────────────────────────────────
set(FW  ${CMAKE_SOURCE_DIR}/../firmware)   # point at /firmware
set(UT  ${CMAKE_SOURCE_DIR})               # /ut itself

# ── coverage flags ───────────────────────────────────────
add_compile_options(--coverage -fprofile-arcs -ftest-coverage -g -O0)
add_link_options(--coverage)

# ── one interface target that carries all firmware headers ─
# every test executable links this and gets all -I paths for free
add_library(fw_headers INTERFACE)
target_include_directories(fw_headers INTERFACE
    ${FW}                  # fil.h, l2p.h, lbm.h ...
    ${FW}/include          # types.h, config.h if in a subdir
    ${UT}/mocks            # mock headers if any
)

# ── fetch GTest automatically ────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)
enable_testing()

# ── macro: one call adds a complete test target ───────────
#
#   add_fw_test(
#       NAME     test_fil
#       SOURCES  test_fil.cpp ${FW}/fil.c
#       MOCKS    mock_fcsim.c mock_l2p.c
#   )
#
macro(add_fw_test)
    cmake_parse_arguments(T "" "NAME" "SOURCES;MOCKS" ${ARGN})

    # prefix mock paths with /ut/mocks/
    set(_mock_srcs "")
    foreach(m ${T_MOCKS})
        list(APPEND _mock_srcs ${UT}/mocks/${m})
    endforeach()

    add_executable(${T_NAME} ${T_SOURCES} ${_mock_srcs})

    target_link_libraries(${T_NAME}
        fw_headers          # all -I paths
        gtest_main
        pthread
    )

    gtest_discover_tests(${T_NAME})
endmacro()

# ── register test targets ────────────────────────────────
add_fw_test(
    NAME    test_fil
    SOURCES test_fil.cpp ${FW}/fil.c
    MOCKS   mock_fcsim.c mock_l2p.c mock_lbm.c
)

add_fw_test(
    NAME    test_l2p
    SOURCES test_l2p.cpp ${FW}/l2p.c
    MOCKS   mock_lbm.c
)

# add more modules here — one line each
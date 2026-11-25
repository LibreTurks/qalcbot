# qalcbot — a Discord calculator bot powered by libqalculate.
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)

project(qalcbot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# libqalculate provides the evaluation engine.
find_package(PkgConfig REQUIRED)
pkg_check_modules(QALC REQUIRED IMPORTED_TARGET libqalculate)

# D++ for the Discord API. Prefer a system package; fall back to a pinned
# FetchContent checkout when no dpp package is installed.
set(QALCBOT_EXTRA_DEPS "")
find_package(dpp QUIET)
if(NOT dpp_FOUND)
    include(FetchContent)
    # Build the vendored dpp statically so the runtime image only needs
    # the system libraries (libqalculate, libzstd) and not libdpp.so.
    # This has no effect when a system dpp package is found above.
    # D++ v10.1.2 defines no dpp::dpp alias in its own CMakeLists (only the
    # installed dpp-config.cmake does); add_library below creates it so the
    # same link name works for both the system package and the vendored
    # builds. Building the vendored dpp's test suite is unnecessary.
    set(DPP_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    FetchContent_Declare(
        dpp
        GIT_REPOSITORY https://github.com/brainboxdotcc/DPP.git
        GIT_TAG        v10.1.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(dpp)
    add_library(dpp::dpp ALIAS dpp)
    # dpp's static build forces OPENSSL_USE_STATIC_LIBS, and Debian trixie's
    # static libcrypto.a needs libzstd; link it when available.
    if(PkgConfig_FOUND)
        pkg_check_modules(ZSTD QUIET IMPORTED_TARGET libzstd)
        if(TARGET PkgConfig::ZSTD)
            list(APPEND QALCBOT_EXTRA_DEPS PkgConfig::ZSTD)
        endif()
    endif()
endif()

add_executable(qalcbot
    src/main.cpp
    src/qalculate_engine.cc
    src/message_filter.cc
)

target_link_libraries(qalcbot PRIVATE
    dpp::dpp
    PkgConfig::QALC
    Threads::Threads
    ${QALCBOT_EXTRA_DEPS}
)

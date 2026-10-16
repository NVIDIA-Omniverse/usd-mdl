# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(usdMdl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Linux/macOS rpath strategy for the installed shared libraries:
#   * Append external link-time directories (e.g. OpenUSD's lib/) to
#     INSTALL_RPATH automatically, so installed binaries find their
#     external deps without LD_LIBRARY_PATH gymnastics.
#   * Per-target INSTALL_RPATH entries below use $ORIGIN-relative paths
#     so the install tree is relocatable for our own libs that move
#     between lib/, plugin/usd/, and lib/python/UsdMdl/.
# No-op on Windows (DLL search uses PATH there).
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if(APPLE)
    set(_origin_marker "@loader_path")
else()
    set(_origin_marker "$ORIGIN")
endif()

# Match OpenUSD cmake/defaults/msvcdefaults.cmake (VS2019+): default /Zc:inline strips
# symbols used for ARCH_CONSTRUCTOR and static registry init; USD uses /Zc:inline-.
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1920)
    add_compile_options(/Zc:inline-)
endif()

# OpenUSD's pxr/base/tf/hashset.h includes the deprecated libstdc++
# <ext/hash_set>, which emits a #warning via <backward/backward_warning.h>.
# Silence it for GCC/Clang; harmless on MSVC.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wno-deprecated)
endif()

# OpenUSD's bundled TBB headers (tbb/task.h) emit a deprecation #pragma message.
# This is the documented oneTBB switch to silence those notes.
add_compile_definitions(TBB_SUPPRESS_DEPRECATED_MESSAGES=1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(pxr REQUIRED)
find_package(mdl REQUIRED CONFIG)
find_package(Python3 COMPONENTS Development Interpreter REQUIRED)
find_package(Threads REQUIRED)

include(CTest)
enable_testing()

include(PxrUsdPlugin)
include(MdlPluginTesting)

add_subdirectory(src)

# ---------------------------------------------------------------------------
# Package config for find_package(usdMdl)
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/usdMdlConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/usdMdlConfig.cmake"
    INSTALL_DESTINATION lib/cmake/usdMdl
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/usdMdlConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/usdMdlConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/usdMdlConfigVersion.cmake"
    DESTINATION lib/cmake/usdMdl
)

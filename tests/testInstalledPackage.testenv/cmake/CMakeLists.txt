# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(usdMdlInstalledPackageSmoke LANGUAGES CXX)

find_package(usdMdl REQUIRED CONFIG)

add_executable(usdMdlInstalledApi consumer.cpp)
target_link_libraries(usdMdlInstalledApi PRIVATE usdMdl::usdMdl)
target_compile_features(usdMdlInstalledApi PRIVATE cxx_std_17)

foreach(_resource IN ITEMS UsdPreviewSurface.usda UsdPreviewSurface.provenance.json OpenUSD-LICENSE.txt OpenUSD-NOTICE.txt)
    if(NOT EXISTS "${usdMdl_PLUGIN_DIR}/usdMdl/resources/${_resource}")
        message(FATAL_ERROR "installed shader resource is missing: ${_resource}")
    endif()
endforeach()

add_executable(installedConsumer main.cpp)
target_compile_features(installedConsumer PRIVATE cxx_std_17)
target_link_libraries(installedConsumer PRIVATE usdMdl::usdMdl)

enable_testing()
add_test(NAME testInstalledCppConsumer COMMAND installedConsumer
    "${CMAKE_CURRENT_SOURCE_DIR}/../mdl/installed_smoke.mdl" value out)
add_test(NAME testInstalledPublicApi COMMAND usdMdlInstalledApi)

# Validate the installation located through the remaining public plugin path.
# Application MDL content belongs to the caller, outside this package.
if(NOT TARGET usdMdl::usdMdl)
    message(FATAL_ERROR "installed package did not define usdMdl::usdMdl")
endif()
if(NOT EXISTS "${usdMdl_PLUGIN_DIR}/plugInfo.json")
    message(FATAL_ERROR "installed USD plugin manifest is missing")
endif()
if(DEFINED usdMdl_MDL_DIR OR DEFINED usdMdl_MDL_STDLIB_DIR OR DEFINED usdMdl_MDL_PATHS)
    message(FATAL_ERROR "installed package unexpectedly exports MDL content paths")
endif()
get_filename_component(_usdMdl_install_prefix "${usdMdl_PLUGIN_DIR}/../.." ABSOLUTE)
if(EXISTS "${_usdMdl_install_prefix}/mdl")
    message(FATAL_ERROR "installed package unexpectedly contains MDL content")
endif()

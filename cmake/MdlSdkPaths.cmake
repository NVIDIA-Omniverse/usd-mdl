# SPDX-License-Identifier: Apache-2.0
#
# MDL-SDK via find_package(mdl CONFIG). Add your MDL install to CMAKE_PREFIX_PATH
# (same as OpenUSD), e.g. -DCMAKE_PREFIX_PATH="C:/OpenUSD/install;C:/MDL-SDK/install"
#
# We use headers from the imported target but do not link mdl::mdl_sdk (runtime LoadLibrary).

function(mdl_sdk_compute_install_prefix _out)
    if(NOT TARGET mdl::mdl_sdk)
        message(FATAL_ERROR
            "Expected find_package(mdl CONFIG) before using MDL helpers. "
            "Add the MDL-SDK install prefix to CMAKE_PREFIX_PATH (directory containing share/mdl/mdl-config.cmake).")
    endif()
    # mdl_DIR is .../share/mdl
    get_filename_component(_prefix "${mdl_DIR}/../.." ABSOLUTE)
    file(TO_CMAKE_PATH "${_prefix}" _norm)
    string(REPLACE "\\" "/" _norm "${_norm}")
    set(${_out} "${_norm}" PARENT_SCOPE)
endfunction()

function(mdl_sdk_target_includes_from_mdl_package _target)
    if(NOT TARGET mdl::mdl_sdk)
        message(FATAL_ERROR "mdl::mdl_sdk is missing; run find_package(mdl CONFIG) before mdl_sdk_target_includes_from_mdl_package().")
    endif()
    get_target_property(_inc mdl::mdl_sdk INTERFACE_INCLUDE_DIRECTORIES)
    # IMPORTED targets may leave this unset; some CMake versions return NOTFOUND. MDL-SDK layout is <prefix>/include/mi/...
    if(NOT _inc OR _inc STREQUAL "INTERFACE_INCLUDE_DIRECTORIES-NOTFOUND")
        if(DEFINED MDL_SDK_INSTALL_PREFIX_STR AND MDL_SDK_INSTALL_PREFIX_STR)
            set(_inc "${MDL_SDK_INSTALL_PREFIX_STR}/include")
        else()
            message(FATAL_ERROR "mdl::mdl_sdk has no INTERFACE_INCLUDE_DIRECTORIES and MDL_SDK_INSTALL_PREFIX_STR is not set.")
        endif()
    endif()
    # Wrap in BUILD_INTERFACE so install(EXPORT ...) does not embed absolute
    # build-machine paths.  Installed consumers re-find mdl via find_dependency
    # and get the includes from their own mdl::mdl_sdk target.
    foreach(_dir IN LISTS _inc)
        target_include_directories(${_target} PUBLIC
            $<BUILD_INTERFACE:${_dir}>)
    endforeach()
endfunction()

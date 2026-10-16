#
# SPDX-License-Identifier: Apache-2.0
#
# Out-of-tree ctest registration helper for the usdMdl tests.
#
# Background
# ----------
# OpenUSD documents `pxr_register_test` + `cmake/macros/testWrapper.py` for
# plugins, but the wrapper is only present in the OpenUSD source tree (not in
# its install tree) and `Public.cmake` references it via `${PROJECT_SOURCE_DIR}`
# — a build-tree path that has no meaning to plugins built against an
# installed OpenUSD.  Out-of-tree plugins therefore roll their own helper;
# `Autodesk/maya-usd/cmake/test.cmake` does the same thing under the name
# `mayaUsd_add_test`.
#
# The companion script `cmake/mdlTestWrapper.py` only supports the features
# this file actually uses (per-test temp dir + fixture copy, env vars, PATH
# prefix, expected exit code).  Anything fancier (diff baselines, image diff,
# pre/post commands, output redirection) is intentionally absent — add a
# feature there and here only when a registered test needs it.

include_guard(GLOBAL)

set(MDL_PLUGIN_TESTING_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Top-level CMakeLists.txt runs find_package(Python3 ... Interpreter ...) before
# this file is included, so Python3_EXECUTABLE is always set here.
set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE STRING
    "Python for CTest / mdlTestWrapper.py" FORCE)

# -----------------------------------------------------------------------------
# Derive runtime locations needed by tests:
#   - DLL dirs for the test process PATH (USD + MDL-SDK on Windows)
#   - OpenUSD's Python package dir, so `import pxr` works without the user
#     having to set PYTHONPATH in their shell.
# Computed once at include time; find_package(pxr) and find_package(mdl CONFIG)
# must already have set pxr_DIR / mdl_DIR.
# -----------------------------------------------------------------------------
set(MDL_TEST_RUNTIME_PATHS "" CACHE INTERNAL "DLL dirs prepended to PATH for ctest")
set(MDL_TEST_PXR_PYTHON_DIR "" CACHE INTERNAL "OpenUSD lib/python dir for PYTHONPATH")

if(DEFINED pxr_DIR AND IS_DIRECTORY "${pxr_DIR}/lib/python")
    set(MDL_TEST_PXR_PYTHON_DIR "${pxr_DIR}/lib/python" CACHE INTERNAL "" FORCE)
endif()

if(WIN32)
    # Conda's Python DLL is beside python.exe, outside pxr_DIR. Native tests
    # link it through OpenUSD and need it even without an activated shell.
    get_filename_component(_python_runtime_dir "${Python3_EXECUTABLE}" DIRECTORY)
    list(APPEND MDL_TEST_RUNTIME_PATHS "${_python_runtime_dir}")
    # USD DLLs (and MaterialX bin/) live under the install root reported by pxr_DIR.
    if(DEFINED pxr_DIR)
        if(IS_DIRECTORY "${pxr_DIR}/lib")
            list(APPEND MDL_TEST_RUNTIME_PATHS "${pxr_DIR}/lib")
        endif()
        if(IS_DIRECTORY "${pxr_DIR}/bin")
            list(APPEND MDL_TEST_RUNTIME_PATHS "${pxr_DIR}/bin")
        endif()
    endif()
    # MDL-SDK DLLs: mdl_DIR is <prefix>/share/mdl, libmdl_sdk.dll lives in <prefix>/bin.
    if(DEFINED mdl_DIR)
        get_filename_component(_mdl_prefix "${mdl_DIR}/../.." ABSOLUTE)
        if(IS_DIRECTORY "${_mdl_prefix}/bin")
            list(APPEND MDL_TEST_RUNTIME_PATHS "${_mdl_prefix}/bin")
        endif()
    endif()
    set(MDL_TEST_RUNTIME_PATHS "${MDL_TEST_RUNTIME_PATHS}" CACHE INTERNAL "" FORCE)
endif()

# -----------------------------------------------------------------------------
# mdl_register_test(<name>
#     COMMAND <cmd>
#     TESTENV_DIR <path>          # full path to testenv fixture dir
#     [PYTHON]                    # prefix COMMAND with ${PYTHON_EXECUTABLE}
#     [EXPECTED_RETURN_CODE <n>]  # default 0
#     [PLUGIN_TARGET <target>]    # PXR_PLUGINPATH_NAME + PATH := $<TARGET_FILE_DIR:target>
#     [ENV <k=v> ...]             # extra environment variables for the child
#     [PRE_PATH <dir> ...]        # prepended to PATH ahead of the defaults
# )
# -----------------------------------------------------------------------------
function(mdl_register_test TEST_NAME)
    if(NOT BUILD_TESTING)
        return()
    endif()

    set(_options PYTHON)
    set(_one COMMAND EXPECTED_RETURN_CODE TESTENV_DIR PLUGIN_TARGET)
    set(_multi ENV PRE_PATH)
    cmake_parse_arguments(_bt "${_options}" "${_one}" "${_multi}" ${ARGN})

    if(NOT _bt_COMMAND)
        message(FATAL_ERROR "mdl_register_test(${TEST_NAME}): COMMAND is required")
    endif()
    if(NOT _bt_TESTENV_DIR)
        message(FATAL_ERROR "mdl_register_test(${TEST_NAME}): TESTENV_DIR is required")
    endif()

    set(_wrapper "${MDL_PLUGIN_TESTING_DIR}/mdlTestWrapper.py")
    if(NOT EXISTS "${_wrapper}")
        message(FATAL_ERROR "mdl_register_test: wrapper not found at '${_wrapper}'")
    endif()

    set(wrapperArgs "${_wrapper}" --verbose --testenv-dir=${_bt_TESTENV_DIR})

    if(DEFINED _bt_EXPECTED_RETURN_CODE)
        list(APPEND wrapperArgs --expected-return-code=${_bt_EXPECTED_RETURN_CODE})
    endif()

    # Keep opt-in discovery independent of the caller's shell. Per-case ENV
    # below can explicitly override this default.
    list(APPEND wrapperArgs --env-var=USD_MDL_LOAD_USD_PREVIEW_SURFACE=0)

    # Match OpenUSD convention: failed TF_VERIFY should fail the test.
    list(APPEND wrapperArgs --env-var=TF_FATAL_VERIFY=1)

    if(_bt_PLUGIN_TARGET)
        list(APPEND wrapperArgs
            --env-var=PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:${_bt_PLUGIN_TARGET}>)
        # Plugin DLL lives next to plugInfo.json; Python tests load it
        # indirectly via _usdMdl.pyd which links against usdMdl.dll,
        # so add the same directory to PATH.
        list(APPEND wrapperArgs --pre-path=$<TARGET_FILE_DIR:${_bt_PLUGIN_TARGET}>)
    endif()

    foreach(env IN LISTS _bt_ENV)
        list(APPEND wrapperArgs --env-var=${env})
    endforeach()

    # User-supplied PRE_PATH first, then our runtime defaults — earlier
    # --pre-path entries win because the wrapper builds PATH as
    # (prePaths + existing).
    foreach(path IN LISTS _bt_PRE_PATH)
        list(APPEND wrapperArgs --pre-path=${path})
    endforeach()
    foreach(path IN LISTS MDL_TEST_RUNTIME_PATHS)
        list(APPEND wrapperArgs --pre-path=${path})
    endforeach()

    # The plugin loads the SDK by name. Supply the test SDK location at
    # process launch, as a deployed application would, rather than baking it
    # into the plugin. POSIX loaders do not use PATH for shared libraries.
    if(NOT WIN32)
        get_filename_component(_mdl_test_prefix "${mdl_DIR}/../.." ABSOLUTE)
        if(APPLE)
            set(_loader_env DYLD_LIBRARY_PATH)
        else()
            set(_loader_env LD_LIBRARY_PATH)
        endif()
        list(APPEND wrapperArgs
            "--env-var=${_loader_env}=${_mdl_test_prefix}/lib:$ENV{${_loader_env}}")
    endif()

    # PYTHONPATH for every test (native + python):
    #   - the build-tree _usdMdl directory (first, so the freshly built
    #     extension always wins over a stale install or a host package),
    #   - usdMdl install lib/python (fallback, for `ctest` runs after
    #     `cmake --install`),
    #   - OpenUSD's lib/python (so `import pxr` works without shell PYTHONPATH),
    #   - inherited $ENV{PYTHONPATH}.
    #
    # The build-tree entry resolves to `$<TARGET_FILE_DIR:_usdMdl>`,
    # which `src/usdMdl/CMakeLists.txt`'s POST_BUILD step has populated
    # with a `UsdMdl/` package mirroring the install layout.  We use a
    # generator expression so the path picks up the active multi-config
    # variant on MSVC.
    set(_testPythonPath "$<TARGET_FILE_DIR:_usdMdl>")
    string(APPEND _testPythonPath ";${CMAKE_INSTALL_PREFIX}/lib/python")
    if(MDL_TEST_PXR_PYTHON_DIR)
        string(APPEND _testPythonPath ";${MDL_TEST_PXR_PYTHON_DIR}")
    endif()
    if(DEFINED ENV{PYTHONPATH})
        string(APPEND _testPythonPath ";$ENV{PYTHONPATH}")
    endif()
    if(WIN32)
        string(REGEX REPLACE "\\\\" "/" _testPythonPath "${_testPythonPath}")
    else()
        string(REPLACE ";" ":" _testPythonPath "${_testPythonPath}")
    endif()

    if(_bt_PYTHON)
        set(testCmd "\"${PYTHON_EXECUTABLE}\" ${_bt_COMMAND}")
    else()
        set(testCmd "${_bt_COMMAND}")
    endif()

    # PYTHONPATH is passed as a quoted literal directly to add_test() because
    # on Windows it embeds `;` as the directory separator, which collides with
    # CMake's list separator if we tried to APPEND it into wrapperArgs.
    add_test(
        NAME ${TEST_NAME}
        COMMAND "${PYTHON_EXECUTABLE}" ${wrapperArgs}
            "--env-var=PYTHONPATH=${_testPythonPath}"
            "${testCmd}"
    )
endfunction()

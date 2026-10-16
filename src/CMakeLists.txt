# SPDX-License-Identifier: Apache-2.0
#
# Single shared library `usdMdl` that hosts the MDL Sdr discovery +
# parser plugins along with all the MDL-to-USD conversion logic.
# Matches OpenUSD's per-plugin layout (one shared library, one MFB
# package name), e.g. usdShade.dll, sdrGlslfx.dll.

include(MdlSdkPaths)
mdl_sdk_compute_install_prefix(MDL_SDK_INSTALL_PREFIX_STR)

# Single shared library `usdMdl` that hosts the MDL SDK runtime layer
# (Context, ModuleLoader, EntityResolver, UriEncoding, TileExpansion,
# PathHelpers, EnvSettings -- under src/usdMdl/runtime/) PLUS the
# MDL-to-USD conversion logic PLUS the Sdr discovery/parser plugin
# entry points.  Matches OpenUSD's per-plugin layout (one .so/.dll
# per plugin, e.g. usdShade.so, sdrGlslfx.so).
add_library(usdMdl SHARED
    # Runtime layer (was src/mdlRuntime/; now src/usdMdl/runtime/)
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/PluginLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/Context.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/EnvSettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/ModuleLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/EntityResolver.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/RejectedResource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/PathHelpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/TileExpansion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime/UriEncoding.cpp"
    # MDL-to-USD conversion
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/initialize.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/canConnectTo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/mdlSdrIntegration.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/mdlModule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/function.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/annotation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/propertyConversion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/shaderDefinitions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/enumValue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/debugCodes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/tokens.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/converter/converter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/converter/converterUtils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/converter/struct.cpp"
    # Plugin entry points (Sdr discovery + parser)
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/mdlDiscovery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/mdlParser.cpp"
)
mdl_sdk_target_includes_from_mdl_package(usdMdl)
mdl_usd_plugin_target_defaults(usdMdl)

target_include_directories(usdMdl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/usdMdl>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(usdMdl
    PUBLIC arch tf work ar sdr usd usdShade
    PRIVATE plug
)

# usdMdl is SHARED: USDMDL_EXPORTS marks the producer side so
# USDMDL_API expands to ARCH_EXPORT in every translation unit that
# compiles into this DLL.  Runtime-layer headers under
# src/usdMdl/runtime/ explicitly include ../api.h for USDMDL_API and
# pick up the same producer/consumer behaviour.  Consumers
# (_usdMdl.pyd, test exes) get ARCH_IMPORT by default.
target_compile_definitions(usdMdl PRIVATE USDMDL_EXPORTS)
if(MSVC)
    target_compile_definitions(usdMdl PRIVATE NOMINMAX)
endif()

# Headers installed for external C++ consumers (UsdMdlAddModuleToRegistry,
# tokens, USDMDL_API).  All other headers in src/usdMdl/ stay in the
# source tree.
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/api.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/addModuleToRegistry.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/canConnectTo.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/shaderDefinitions.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/enumValue.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/initialize.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/tokens.h"
    DESTINATION include/usdMdl
)

# `_usdMdl` Python module lives next to the conversion source for
# proximity to the generated boost::python wrappers and to keep the
# wrap*.cpp / module.cpp files local to their wrapped C++ types.
add_subdirectory(usdMdl)

# Single plugInfo.json template, USD-canonical layout (DLL at
# plugin/usd/usdMdl.dll, plugInfo at plugin/usd/usdMdl/resources/
# plugInfo.json).  configure_file performs the @VAR@ substitutions at
# configure time but preserves $<...> generator expressions as literal
# text; file(GENERATE) then resolves those at build time.  Result:
# per-platform LibraryPath (usdMdl.dll / libusdMdl.so / .dylib) without
# needing separate plugInfo files.
#
# Plugin "Root" (relative to the directory containing plugInfo.json,
# i.e. plugin/usd/usdMdl/resources/) is "..", so the plugin root is
# plugin/usd/usdMdl/.  LibraryPath is then "../<dll>", resolving to
# plugin/usd/<dll> next to all other USD plugin DLLs.
#
# `file(GENERATE)` is configured per-$<CONFIG> so multi-config
# generators (MSVC, Xcode) get a distinct plugInfo.json per build
# type instead of the last-built one overwriting the others.  The
# install path below copies the active-config file.
set(PLUG_INFO_LIBRARY_PATH "../$<TARGET_FILE_NAME:usdMdl>")
set(PLUG_INFO_ROOT "..")
set(_pluginfo_template "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/plugInfo.json")
set(_pluginfo_configured "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json.in")
set(_pluginfo_generated "${CMAKE_CURRENT_BINARY_DIR}/plugInfo/$<CONFIG>/plugInfo.json")
configure_file("${_pluginfo_template}" "${_pluginfo_configured}" @ONLY)
file(GENERATE OUTPUT "${_pluginfo_generated}" INPUT "${_pluginfo_configured}")

# Aggregator plugInfo.json that points USD's Plug system at every
# `<sub>/resources/` subdir under the plugin directory.  Matches
# OpenUSD's `lib/usd/plugInfo.json` layout, and is the canonical way
# to make the per-plugin `<plugin>/resources/plugInfo.json` files
# discoverable when consumers set PXR_PLUGINPATH_NAME to the parent
# directory.  Plug's auto-walk only descends one level deep without
# this Includes directive.
set(_pluginfo_aggregator "${CMAKE_CURRENT_BINARY_DIR}/plugInfo/aggregator/plugInfo.json")
file(WRITE "${_pluginfo_aggregator}" "{\n    \"Includes\": [\"*/resources/\"]\n}\n")

include(UsdShaderDefinitions)
usdmdl_generate_shader_definitions(usdMdl)

# Build-tree staging mirrors the install layout (aggregator + USD-
# canonical <plugin>/resources/plugInfo.json) so tests (which set
# PXR_PLUGINPATH_NAME to $<TARGET_FILE_DIR:usdMdl>) pick up the
# plugins the same way installed consumers do.
add_custom_command(TARGET usdMdl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:usdMdl>/usdMdl/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_pluginfo_generated}"
        "$<TARGET_FILE_DIR:usdMdl>/usdMdl/resources/plugInfo.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_pluginfo_aggregator}"
        "$<TARGET_FILE_DIR:usdMdl>/plugInfo.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${USDMDL_SHADER_DEFINITIONS_OUTPUT}"
        "${USDMDL_SHADER_DEFINITIONS_PROVENANCE}"
        "${USDMDL_SHADER_DEFINITIONS_LICENSE_OUTPUT}"
        "${USDMDL_SHADER_DEFINITIONS_NOTICE_OUTPUT}"
        "$<TARGET_FILE_DIR:usdMdl>/usdMdl/resources"
    COMMENT "Stage usdMdl plugin manifests and shader-definition resources"
)

# Also refresh resources when only the USD definition, license or notice changes and
# usdMdl itself does not need relinking. The ordinary full build runs this target.
add_custom_target(usdMdlShaderResources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:usdMdl>/usdMdl/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${USDMDL_SHADER_DEFINITIONS_OUTPUT}"
        "${USDMDL_SHADER_DEFINITIONS_PROVENANCE}"
        "${USDMDL_SHADER_DEFINITIONS_LICENSE_OUTPUT}"
        "${USDMDL_SHADER_DEFINITIONS_NOTICE_OUTPUT}"
        "$<TARGET_FILE_DIR:usdMdl>/usdMdl/resources"
    DEPENDS usdMdl
    COMMENT "Refresh usdMdl shader-definition resources"
    VERBATIM
)

if(BUILD_TESTING)
    add_test(NAME testMdlTestWrapper
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/testMdlTestWrapper.py"
            --wrapper "${PROJECT_SOURCE_DIR}/cmake/mdlTestWrapper.py"
            --helper "${PROJECT_SOURCE_DIR}/cmake/MdlPluginTesting.cmake"
            --cmake "${CMAKE_COMMAND}")
endif()

mdl_register_test(testGenerateShaderDefinitions
    COMMAND "\"${PROJECT_SOURCE_DIR}/tests/testGenerateShaderDefinitions.py\" --generator \"${PROJECT_SOURCE_DIR}/tools/generateShaderDefinitions.py\" --source \"${USDMDL_USD_SHADER_DEFINITIONS_INPUT}\" --usd-version ${USDMDL_USD_VERSION} --usd-python \"${USDMDL_USD_PYTHON_PACKAGE}\" --usd-prefix \"${USDMDL_USD_SDK_PREFIX}\" --license \"${USDMDL_USD_SHADER_DEFINITIONS_SELECTED_LICENSE}\" --notice \"${USDMDL_USD_SHADER_DEFINITIONS_SELECTED_NOTICE}\""
    PYTHON
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testInstalledPackage.testenv"
    PLUGIN_TARGET usdMdl)

if(USDMDL_USD_PLUGIN_MANIFEST)
    mdl_register_test(testMdlPreviewSurfaceAuto
        COMMAND "testMdlPreviewSurfaceAuto.py"
        PYTHON
        EXPECTED_RETURN_CODE 0
        TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlPreviewSurfaceAuto.testenv"
        PLUGIN_TARGET usdMdl
        ENV "USDMDL_TEST_USD_PLUGIN_ROOT=${USDMDL_USD_PLUGIN_MANIFEST}")
endif()

# ---------------------------------------------------------------------------
# Test executables.  Each test exe co-locates with usdMdl.dll so Windows
# DLL search resolves it without PATH entries.
# ---------------------------------------------------------------------------

add_executable(testMdlRuntimeShutdown
    "${CMAKE_SOURCE_DIR}/tests/testMdlRuntimeShutdown.cpp")
target_link_libraries(testMdlRuntimeShutdown PRIVATE usdMdl tf sdr)
target_include_directories(testMdlRuntimeShutdown PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl")
target_compile_definitions(testMdlRuntimeShutdown PRIVATE
    "MDL_SDK_INSTALL_PREFIX=\"${MDL_SDK_INSTALL_PREFIX_STR}\"")
if(MSVC)
    target_compile_definitions(testMdlRuntimeShutdown PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlRuntimeShutdown PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

add_executable(testMdlConcurrentLoading
    "${CMAKE_SOURCE_DIR}/tests/testMdlConcurrentLoading.cpp")
target_link_libraries(testMdlConcurrentLoading PRIVATE usdMdl tf Threads::Threads)
target_include_directories(testMdlConcurrentLoading PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl")
if(MSVC)
    target_compile_definitions(testMdlConcurrentLoading PRIVATE NOMINMAX)
endif()
add_dependencies(testMdlConcurrentLoading usdMdl)
set_target_properties(testMdlConcurrentLoading PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)

mdl_register_test(testMdlConcurrentLoading
    COMMAND $<TARGET_FILE:testMdlConcurrentLoading>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    ENV
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl)

# Keep deliberately invalid and warning-producing modules outside healthy sweeps.
add_executable(testMdlDiagnostics "${CMAKE_SOURCE_DIR}/tests/testMdlDiagnostics.cpp")
target_link_libraries(testMdlDiagnostics PRIVATE usdMdl tf vt Threads::Threads)
target_compile_definitions(testMdlDiagnostics PRIVATE
    "MDL_SDK_INSTALL_PREFIX=\"${MDL_SDK_INSTALL_PREFIX_STR}\"")
if(MSVC)
    target_compile_definitions(testMdlDiagnostics PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlDiagnostics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
mdl_register_test(testMdlDiagnostics
    COMMAND $<TARGET_FILE:testMdlDiagnostics>
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlDiagnostics.testenv"
    PLUGIN_TARGET usdMdl
    ENV USD_MDL_DISCOVER_ON_STARTUP=0 TF_DEBUG=)
mdl_register_test(testMdlDiagnosticsDebug
    PYTHON
    COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlDiagnosticsDebug.py $<TARGET_FILE:testMdlDiagnostics>"
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlDiagnostics.testenv"
    PLUGIN_TARGET usdMdl
    ENV USD_MDL_DISCOVER_ON_STARTUP=0)
foreach(_mode IN ITEMS cold registered)
    mdl_register_test(testMdlDiagnosticsPython_${_mode}
        PYTHON
        COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlDiagnostics.py ${_mode}"
        TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlDiagnostics.testenv"
        PLUGIN_TARGET usdMdl)
endforeach()

# Phase 1 (no args): Context init/shutdown only.  Verifies the cleanest
# tear-down path without any MDL modules loaded.
mdl_register_test(testMdlRuntimeShutdownPhase1
    COMMAND $<TARGET_FILE:testMdlRuntimeShutdown>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    PLUGIN_TARGET usdMdl
)

# Phase 5 (-ppp): full path -- ModuleLoader -> MdlModule ctor ->
# GetSdrFromDiscoveryResult parse -> MdlSdrIntegration singleton
# GetDiscoveryResults + parse + explicit Shutdown(). The MDL path is
# passed as argv[1] (relative to the test's CWD, which testWrapper sets
# to a temp dir containing the copied testenv/mdl/).
# testMdlRuntimeShutdown does not call TerminateProcess, so this test
# passing is the validation that the SDK shutdown sequence is clean.
mdl_register_test(testMdlRuntimeShutdownPhase5
    COMMAND "$<TARGET_FILE:testMdlRuntimeShutdown> mdl -ppp"
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    PLUGIN_TARGET usdMdl
)

# Public lifecycle API: default self-owned INeuray plus host-owned instances
# supplied after startup, plus rejection of an unstarted host. Each mode runs in a
# fresh process because the MDL SDK does not support restart after shutdown.
add_executable(testMdlNeurayOwnership
    "${CMAKE_SOURCE_DIR}/tests/testMdlNeurayOwnership.cpp")
target_link_libraries(testMdlNeurayOwnership PRIVATE usdMdl tf sdr)
target_include_directories(testMdlNeurayOwnership PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl")
target_compile_definitions(testMdlNeurayOwnership PRIVATE
    "MDL_SDK_INSTALL_PREFIX=\"${MDL_SDK_INSTALL_PREFIX_STR}\"")
if(MSVC)
    target_compile_definitions(testMdlNeurayOwnership PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlNeurayOwnership PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

foreach(_mode IN ITEMS owned owned-empty owned-invalid external-unstarted external-started)
    string(REPLACE "-" "_" _mode_test_suffix "${_mode}")
    mdl_register_test(testMdlNeurayOwnership_${_mode_test_suffix}
        COMMAND "$<TARGET_FILE:testMdlNeurayOwnership> ${_mode} mdl"
        EXPECTED_RETURN_CODE 0
        TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
        PLUGIN_TARGET usdMdl
        ENV
            PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
            MDL_USER_PATH=<PXR_TEST_DIR>/mdl
    )
endforeach()

mdl_register_test(testMdlSdkRelocation
    PYTHON
    COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlSdkRelocation.py --executable $<TARGET_FILE:testMdlNeurayOwnership> --sdk-prefix ${MDL_SDK_INSTALL_PREFIX_STR}"
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    PLUGIN_TARGET usdMdl
    ENV
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
        USD_MDL_DISCOVER_ON_STARTUP=0
)

mdl_register_test(testEnvironmentHelpers
    PYTHON
    COMMAND "${CMAKE_SOURCE_DIR}/tests/testEnvironmentHelpers.py ${CMAKE_SOURCE_DIR}/cmake"
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testInstalledPackage.testenv")

add_executable(testMdlBuiltinsFirst "${CMAKE_SOURCE_DIR}/tests/testMdlBuiltinsFirst.cpp")
target_link_libraries(testMdlBuiltinsFirst PRIVATE usdMdl arch tf sdf sdr)
if(MSVC)
    target_compile_definitions(testMdlBuiltinsFirst PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlBuiltinsFirst PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
mdl_register_test(testMdlBuiltinsFirst
    COMMAND "$<TARGET_FILE:testMdlBuiltinsFirst>"
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlEmbeddedModules.testenv"
    PLUGIN_TARGET usdMdl)
mdl_register_test(testMdlWithoutBundledContent
    PYTHON
    COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlWithoutBundledContent.py"
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testInstalledPackage.testenv"
    PLUGIN_TARGET usdMdl)

add_executable(testMdlLazyDiscovery "${CMAKE_SOURCE_DIR}/tests/testMdlLazyDiscovery.cpp")
target_link_libraries(testMdlLazyDiscovery PRIVATE usdMdl arch tf sdf sdr)
if(MSVC)
    target_compile_definitions(testMdlLazyDiscovery PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlLazyDiscovery PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
foreach(_mode IN ITEMS initialize lookup)
    foreach(_setting IN ITEMS unset disabled)
        mdl_register_test(testMdlLazyDiscovery_${_mode}_${_setting}
            COMMAND "$<TARGET_FILE:testMdlLazyDiscovery> ${_mode} ${_setting}"
            TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
            PLUGIN_TARGET usdMdl
            ENV USD_MDL_SEARCH_PATHS=<PXR_TEST_DIR>/mdl)
    endforeach()
endforeach()

add_executable(testMdlCanConnectToCpp "${CMAKE_SOURCE_DIR}/tests/testMdlCanConnectTo.cpp")
target_link_libraries(testMdlCanConnectToCpp PRIVATE usdMdl)
if(MSVC)
    target_compile_definitions(testMdlCanConnectToCpp PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlCanConnectToCpp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
mdl_register_test(testMdlCanConnectToCpp
    COMMAND $<TARGET_FILE:testMdlCanConnectToCpp>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    PLUGIN_TARGET usdMdl)

add_executable(testMdlSdrPlugins
    "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.cpp")
target_link_libraries(testMdlSdrPlugins PRIVATE
    usdMdl  # only for the public token headers (tokens.h);
            # discovery still goes through Plug.
    arch
    tf
    sdf
    sdr
    plug
    usd
    usdShade
)
if(MSVC)
    target_compile_definitions(testMdlSdrPlugins PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlSdrPlugins PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

mdl_register_test(testMdlSdrPlugins
    COMMAND $<TARGET_FILE:testMdlSdrPlugins>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    ENV
        USD_MDL_DISCOVER_ON_STARTUP=1
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
)

add_executable(testMdlUsdShade
    "${CMAKE_SOURCE_DIR}/tests/testMdlUsdShade.cpp")
target_link_libraries(testMdlUsdShade PRIVATE
    arch
    ar
    tf
    sdf
    sdr
    plug
    usd
    usdShade
)
if(MSVC)
    target_compile_definitions(testMdlUsdShade PRIVATE NOMINMAX)
endif()
add_dependencies(testMdlUsdShade usdMdl)
set_target_properties(testMdlUsdShade PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

mdl_register_test(testMdlUsdShade
    COMMAND $<TARGET_FILE:testMdlUsdShade>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    ENV
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
)

# Executable form of the documented scene-inspection example. Building and
# running it in CTest keeps the public C++ API and the sample USD/MDL assets
# synchronized with supported OpenUSD versions. Both example tests validate
# printed defaults, annotations, and resource paths against authored content;
# the C++ test also checks SDK definition access for two different modules.
add_executable(inspectMdlMaterial
    "${CMAKE_SOURCE_DIR}/examples/inspect_mdl_material.cpp")
target_link_libraries(inspectMdlMaterial PRIVATE
    usdMdl
    usd
    usdShade
)
target_include_directories(inspectMdlMaterial PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
if(MSVC)
    target_compile_definitions(inspectMdlMaterial PRIVATE NOMINMAX)
endif()
set_target_properties(inspectMdlMaterial PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

mdl_register_test(testInspectMdlMaterialCpp
    PYTHON
    COMMAND "${CMAKE_SOURCE_DIR}/tests/testInspectMdlMaterial.py $<TARGET_FILE:inspectMdlMaterial>"
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/examples"
    PLUGIN_TARGET usdMdl
    ENV
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
)

add_executable(testMdlShaderDefinitionBinding
    "${CMAKE_SOURCE_DIR}/tests/testMdlShaderDefinitionBinding.cpp")
target_link_libraries(testMdlShaderDefinitionBinding PRIVATE usdMdl tf sdf sdr)
if(MSVC)
    target_compile_definitions(testMdlShaderDefinitionBinding PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlShaderDefinitionBinding PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
mdl_register_test(testMdlShaderDefinitionBinding
    COMMAND "$<TARGET_FILE:testMdlShaderDefinitionBinding> mdl preview_surface.usda"
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    PLUGIN_TARGET usdMdl
    ENV
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
        USD_MDL_DISCOVER_ON_STARTUP=0)

add_executable(testMdlDefaultExpressions
    "${CMAKE_SOURCE_DIR}/tests/testMdlDefaultExpressions.cpp")
target_link_libraries(testMdlDefaultExpressions PRIVATE usdMdl tf gf sdf sdr)
if(MSVC)
    target_compile_definitions(testMdlDefaultExpressions PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlDefaultExpressions PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
mdl_register_test(testMdlDefaultExpressions
    COMMAND $<TARGET_FILE:testMdlDefaultExpressions>
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    ENV
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
        USD_MDL_DISCOVER_ON_STARTUP=0)

add_executable(testMdlPathContracts "${CMAKE_SOURCE_DIR}/tests/testMdlPathContracts.cpp")
target_link_libraries(testMdlPathContracts PRIVATE usdMdl tf gf ar sdf sdr)
if(MSVC)
    target_compile_definitions(testMdlPathContracts PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlPathContracts PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
mdl_register_test(testMdlPathContracts
    COMMAND $<TARGET_FILE:testMdlPathContracts>
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    ENV
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
        USD_MDL_DISCOVER_ON_STARTUP=0)

# Isolated colliding PNGs reproduce #25 without polluting healthy discovery sweeps.
add_executable(testMdlRejectedResource "${CMAKE_SOURCE_DIR}/tests/testMdlRejectedResource.cpp")
target_link_libraries(testMdlRejectedResource PRIVATE usdMdl tf vt ar sdf sdr Threads::Threads)
if(MSVC)
    target_compile_definitions(testMdlRejectedResource PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlRejectedResource PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
foreach(_mode IN ITEMS legacy sdk plugin)
    mdl_register_test(testMdlRejectedResource_${_mode}
        COMMAND "$<TARGET_FILE:testMdlRejectedResource> ${_mode}"
        TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlRejectedResource.testenv"
        PLUGIN_TARGET usdMdl
        ENV USD_MDL_SEARCH_PATHS=<PXR_TEST_DIR>/mdl USD_MDL_DISCOVER_ON_STARTUP=0
            USD_MDL_ADD_USER_PATHS=0 USD_MDL_ADD_SYSTEM_PATHS=0 TF_DEBUG=)
endforeach()
foreach(_mode IN ITEMS cold registered)
    mdl_register_test(testMdlRejectedResourcePython_${_mode}
        PYTHON
        COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlRejectedResource.py ${_mode}"
        TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlRejectedResource.testenv"
        PLUGIN_TARGET usdMdl)
endforeach()

add_executable(testMdlModuleIdentity "${CMAKE_SOURCE_DIR}/tests/testMdlModuleIdentity.cpp")
target_link_libraries(testMdlModuleIdentity PRIVATE usdMdl tf gf ar sdf sdr)
if(MSVC)
    target_compile_definitions(testMdlModuleIdentity PRIVATE NOMINMAX)
endif()
set_target_properties(testMdlModuleIdentity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>)
mdl_register_test(testMdlModuleIdentity
    COMMAND $<TARGET_FILE:testMdlModuleIdentity>
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    ENV
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
        USD_MDL_DISCOVER_ON_STARTUP=0)

add_executable(testMdlResources
    "${CMAKE_SOURCE_DIR}/tests/testMdlResources.cpp")
target_link_libraries(testMdlResources PRIVATE
    tf
    plug
    usdMdl
)
target_include_directories(testMdlResources PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime"
)
if(MSVC)
    target_compile_definitions(testMdlResources PRIVATE NOMINMAX)
endif()
add_dependencies(testMdlResources usdMdl)
set_target_properties(testMdlResources PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

mdl_register_test(testMdlResources
    COMMAND $<TARGET_FILE:testMdlResources>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    ENV
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
)

add_executable(testMdlUriEncoding
    "${CMAKE_SOURCE_DIR}/tests/testMdlUriEncoding.cpp")
target_link_libraries(testMdlUriEncoding PRIVATE
    tf
    usdMdl
)
target_include_directories(testMdlUriEncoding PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime"
)
if(MSVC)
    target_compile_definitions(testMdlUriEncoding PRIVATE NOMINMAX)
endif()
add_dependencies(testMdlUriEncoding usdMdl)
set_target_properties(testMdlUriEncoding PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

mdl_register_test(testMdlUriEncoding
    COMMAND $<TARGET_FILE:testMdlUriEncoding>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
)

add_executable(testMdlPluginLoader
    "${CMAKE_SOURCE_DIR}/tests/testMdlPluginLoader.cpp")
target_link_libraries(testMdlPluginLoader PRIVATE
    tf
    usdMdl
)
target_include_directories(testMdlPluginLoader PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/usdMdl/runtime"
)
if(MSVC)
    target_compile_definitions(testMdlPluginLoader PRIVATE NOMINMAX)
endif()
add_dependencies(testMdlPluginLoader usdMdl)

# Validates Utils::ComputeMdlSubIdentifier for the canonical Omniverse
# Kit subIdentifier convention (parameter signature with same-module
# types unqualified, cross-module types kept fully qualified).  Sweeps
# explicitly registered results from sub_identifier.mdl and asserts the
# expected subIdentifier strings.
add_executable(testMdlSubIdentifier
    "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifier.cpp")
target_link_libraries(testMdlSubIdentifier PRIVATE
    arch
    tf
    plug
    sdf
    sdr
    usdMdl
)
if(MSVC)
    target_compile_definitions(testMdlSubIdentifier PRIVATE NOMINMAX)
endif()
add_dependencies(testMdlSubIdentifier usdMdl)
set_target_properties(testMdlSubIdentifier PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

mdl_register_test(testMdlSubIdentifier
    COMMAND $<TARGET_FILE:testMdlSubIdentifier>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
    ENV
        PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usdMdl>
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
)
set_target_properties(testMdlPluginLoader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:usdMdl>
)

mdl_register_test(testMdlPluginLoader
    COMMAND $<TARGET_FILE:testMdlPluginLoader>
    EXPECTED_RETURN_CODE 0
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv"
)

# Python Sdr tests for the MDL parser/discovery plugins; run from source tree.
set(_mdl_py_testenv "${CMAKE_SOURCE_DIR}/tests/testMdlSdrPlugins.testenv")
# Python tests pick up the top-level `UsdMdl` package from
# `$<TARGET_FILE_DIR:_usdMdl>` (staged at build time by the POST_BUILD
# step in `src/usdMdl/CMakeLists.txt`), with the installed
# `<prefix>/lib/python/` tree as a fallback.  `cmake --build` is
# sufficient before `ctest`; `cmake --install` is required only for
# end-user consumers of the install tree.
foreach(_t IN ITEMS testMdlDiscovery testMdlTypes testMdlCanConnectTo testMdlAnnotations testMdlExpressions testMdlLifecycle testMdlShaderDefinitions)
    set(_discovery_env "")
    if(_t STREQUAL "testMdlDiscovery")
        set(_discovery_env USD_MDL_DISCOVER_ON_STARTUP=1)
    endif()
    mdl_register_test(${_t}
        PYTHON
        COMMAND "${_mdl_py_testenv}/${_t}.py"
        TESTENV_DIR "${_mdl_py_testenv}"
        PLUGIN_TARGET usdMdl
        ENV
            ${_discovery_env}
            MDL_USER_PATH=<PXR_TEST_DIR>/mdl
            USDMDL_TEST_MDL_SDK_VERSION=${mdl_VERSION}
    )
endforeach()

# Exercise both explicit registration and cold Python lookup independently of
# the startup default. Unexpected Tf errors surface as Python exceptions.
foreach(_register IN ITEMS 0 1)
    mdl_register_test(testMdlExpressions_registered_${_register}
        PYTHON
        COMMAND "${_mdl_py_testenv}/testMdlExpressions.py"
        TESTENV_DIR "${_mdl_py_testenv}"
        PLUGIN_TARGET usdMdl
        ENV
            MDL_USER_PATH=<PXR_TEST_DIR>/mdl
            USD_MDL_DISCOVER_ON_STARTUP=0
            USDMDL_TEST_REGISTER_DEFAULTS=${_register})
endforeach()

foreach(_mode IN ITEMS cold registered eager)
    foreach(_order IN ITEMS forward reverse)
        mdl_register_test(testMdlArrayConstructors_${_mode}_${_order}
            PYTHON
            COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlArrayConstructors.py --mode ${_mode} --order ${_order}"
            TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlArrayConstructors.testenv"
            PLUGIN_TARGET usdMdl)
    endforeach()
endforeach()

# Fresh processes ensure an earlier SDK/Sdr lookup cannot mask dummy-file parsing.
foreach(_dummy IN ITEMS absent mdl-roots ar-context)
    set(_contents proxy)
    if(NOT _dummy STREQUAL "absent")
        list(APPEND _contents empty invalid)
    endif()
    foreach(_content IN LISTS _contents)
        foreach(_mode IN ITEMS cold registered eager)
            set(_test_name testMdlEmbeddedModules_${_dummy}_${_mode})
            if(NOT _content STREQUAL "proxy")
                string(APPEND _test_name _${_content})
            endif()
            mdl_register_test(${_test_name}
                PYTHON
                COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlEmbeddedModules.py --dummy ${_dummy} --mode ${_mode} --content ${_content}"
                TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlEmbeddedModules.testenv"
                PLUGIN_TARGET usdMdl)
            string(REPLACE "testMdlEmbeddedModules" "testMdlBuiltins" _builtin_test_name "${_test_name}")
            mdl_register_test(${_builtin_test_name}
                PYTHON
                COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlEmbeddedModules.py --module-set builtins --dummy ${_dummy} --mode ${_mode} --content ${_content}"
                TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlEmbeddedModules.testenv"
                PLUGIN_TARGET usdMdl)
        endforeach()
    endforeach()
endforeach()

# Expose each process configuration in CTest; each process reports individual
# public-lookup cases through unittest, including their authored sub-identifiers.
foreach(_mode IN ITEMS cold registered eager)
    foreach(_allow IN ITEMS unset 0 1)
        foreach(_order IN ITEMS forward reverse)
            mdl_register_test(testMdlSubIdentifiers_${_mode}_${_allow}_${_order}
                PYTHON
                COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifiers.py --mode ${_mode} --allow ${_allow} --order ${_order}"
                TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifiers.testenv"
                PLUGIN_TARGET usdMdl
            )
        endforeach()
    endforeach()
endforeach()
# Struct preference modifies only an enabled bare-name fallback. Run each
# combination in a fresh process so Sdr/Tf caches cannot mask setting changes.
foreach(_mode IN ITEMS cold registered eager)
    foreach(_prefer IN ITEMS 0 1)
        foreach(_allow IN ITEMS 0 1)
            mdl_register_test(testMdlSubIdentifiers_struct_${_mode}_${_prefer}_${_allow}
                PYTHON
                COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifiers.py --mode ${_mode} --prefer-struct ${_prefer} --allow ${_allow}"
                TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifiers.testenv"
                PLUGIN_TARGET usdMdl)
        endforeach()
    endforeach()
endforeach()

mdl_register_test(testMdlSubIdentifiers_relocation
    PYTHON
    COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifiers.py --relocate"
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifiers.testenv"
    PLUGIN_TARGET usdMdl
)

# Legacy material signatures are a separate opt-in; exercise both compatibility
# settings together and independently, with fresh Sdr caches for every run.
foreach(_mode IN ITEMS cold registered eager)
    foreach(_material IN ITEMS unset 0 1)
        foreach(_allow IN ITEMS 0 1)
            mdl_register_test(testMdlSubIdentifiers_legacy_${_mode}_${_material}_${_allow}
                PYTHON
                COMMAND "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifiers.py --mode ${_mode} --material-mismatch ${_material} --allow ${_allow}"
                TESTENV_DIR "${CMAKE_SOURCE_DIR}/tests/testMdlSubIdentifiers.testenv"
                PLUGIN_TARGET usdMdl
            )
        endforeach()
    endforeach()
endforeach()

mdl_register_test(testUsdMdlPythonBinding
    PYTHON
    COMMAND "${_mdl_py_testenv}/testUsdMdlPythonBinding.py"
    TESTENV_DIR "${_mdl_py_testenv}"
    PLUGIN_TARGET usdMdl
    ENV
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
)

mdl_register_test(testMdlOffSearchPath
    PYTHON
    COMMAND "${_mdl_py_testenv}/testMdlOffSearchPath.py"
    TESTENV_DIR "${_mdl_py_testenv}"
    PLUGIN_TARGET usdMdl
    ENV
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
)

mdl_register_test(testInspectMdlMaterialPython
    PYTHON
    COMMAND "${CMAKE_SOURCE_DIR}/tests/testInspectMdlMaterial.py ${CMAKE_SOURCE_DIR}/examples/inspect_mdl_material.py"
    TESTENV_DIR "${CMAKE_SOURCE_DIR}/examples"
    PLUGIN_TARGET usdMdl
    ENV
        MDL_USER_PATH=<PXR_TEST_DIR>/mdl
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
#
# Single exported target (matches OpenUSD's per-plugin layout where one
# shared library is one CMake target):
#
#   usdMdl::usdMdl - the plugin DLL.  Link this to call
#                    UsdMdlAddModuleToRegistry from C++; runtime
#                    consumers just set PXR_PLUGINPATH_NAME and let
#                    USD's Plug system load it.

install(TARGETS usdMdl
    EXPORT usdMdlTargets
    RUNTIME DESTINATION plugin/usd
    LIBRARY DESTINATION plugin/usd
    ARCHIVE DESTINATION plugin/usd
)

# CMAKE_INSTALL_RPATH_USE_LINK_PATH (set in the top-level CMakeLists)
# already appends external link-time dirs like OpenUSD's lib/ to the
# installed RPATH.  No additional $ORIGIN entries needed because the
# runtime layer is folded into usdMdl.dll itself.

if(WIN32)
    install(FILES $<TARGET_PDB_FILE:usdMdl>
        DESTINATION plugin/usd OPTIONAL)
endif()

install(FILES "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION .)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION .)

# Environment-setup helper scripts (POSIX shell + Windows cmd).  Each
# script locates its own install prefix relative to itself and sets the
# PXR_PLUGINPATH_NAME / PYTHONPATH variables a consumer needs. The
# application supplies USD_MDL_SEARCH_PATHS for its own content. Usage:
#
#     eval "$(<prefix>/bin/usd-mdl-env.sh)"   # POSIX shell
#     call <prefix>\bin\usd-mdl-env.bat       # Windows cmd.exe
configure_file("${CMAKE_SOURCE_DIR}/cmake/usd-mdl-env.sh.in"
               "${CMAKE_CURRENT_BINARY_DIR}/usd-mdl-env.sh"  @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/cmake/usd-mdl-env.bat.in"
               "${CMAKE_CURRENT_BINARY_DIR}/usd-mdl-env.bat" @ONLY)
install(PROGRAMS
    "${CMAKE_CURRENT_BINARY_DIR}/usd-mdl-env.sh"
    "${CMAKE_CURRENT_BINARY_DIR}/usd-mdl-env.bat"
    DESTINATION bin
)

# USD-canonical layout: DLL at plugin/usd/usdMdl.dll, per-plugin
# plugInfo at plugin/usd/usdMdl/resources/plugInfo.json, plus an
# aggregator at plugin/usd/plugInfo.json so consumers can set
# PXR_PLUGINPATH_NAME to <prefix>/plugin/usd and have Plug walk into
# every `*/resources/` subdir automatically (matches OpenUSD's
# lib/usd/plugInfo.json pattern).
install(FILES "${_pluginfo_generated}"
    "${USDMDL_SHADER_DEFINITIONS_OUTPUT}"
    "${USDMDL_SHADER_DEFINITIONS_PROVENANCE}"
    "${USDMDL_SHADER_DEFINITIONS_LICENSE_OUTPUT}"
    "${USDMDL_SHADER_DEFINITIONS_NOTICE_OUTPUT}"
    DESTINATION plugin/usd/usdMdl/resources)
install(FILES "${_pluginfo_aggregator}"
    DESTINATION plugin/usd)

install(EXPORT usdMdlTargets
    NAMESPACE usdMdl::
    DESTINATION lib/cmake/usdMdl
)

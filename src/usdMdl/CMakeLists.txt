# SPDX-License-Identifier: Apache-2.0
#
# This directory holds:
#   - the MDL-to-USD conversion sources (mdlSdrIntegration, mdlModule,
#     function, annotation, utils, debugCodes, tokens, converter/), now
#     compiled directly into the shared `usdMdl` plugin library defined
#     in ../CMakeLists.txt (no separate static archive); and
#   - the `_usdMdl` Python extension module (defined below).
#
# The single public C++ header set (api.h, addModuleToRegistry.h,
# initialize.h, shaderDefinitions.h, enumValue.h, tokens.h) is installed by ../CMakeLists.txt; the rest of the headers
# in this directory are internal to the build.

# ---------------------------------------------------------------------------
# Python extension module: top-level `UsdMdl` package (importable as
# `import UsdMdl`, NOT `from pxr import UsdMdl`).  This avoids the
# read-only-OpenUSD overlay-install requirement that the `pxr.X`
# convention forces.
# ---------------------------------------------------------------------------
add_library(_usdMdl SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/module.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/wrapMdlSdrIntegration.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/wrapTokens.cpp"
)
set_target_properties(_usdMdl PROPERTIES
    PREFIX ""
    OUTPUT_NAME "_usdMdl"
)
if(WIN32)
    set_target_properties(_usdMdl PROPERTIES SUFFIX ".pyd")
elseif(APPLE)
    set_target_properties(_usdMdl PROPERTIES SUFFIX ".so")
endif()
target_compile_definitions(_usdMdl PRIVATE
    MFB_PACKAGE_NAME=usdMdl
    MFB_ALT_PACKAGE_NAME=usdMdl
    MFB_PACKAGE_MODULE=UsdMdl
)
if(MSVC)
    target_compile_definitions(_usdMdl PRIVATE NOMINMAX)
endif()

# Linking against the shared usdMdl plugin gives us the runtime layer
# (src/usdMdl/runtime/) and the public-surface symbols that
# wrapMdlSdrIntegration.cpp consumes.  USDMDL_EXPORTS is defined only on
# the producer side of usdMdl, so USDMDL_API expands to dllimport here.
target_link_libraries(_usdMdl PRIVATE usdMdl tf boost python)
target_include_directories(_usdMdl PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_include_directories(_usdMdl SYSTEM PRIVATE ${Python3_INCLUDE_DIRS})

# Top-level Python package (NOT nested under pxr/) so the plugin can be
# installed into any prefix without needing overlay-install on top of
# OpenUSD.  Consumers add <prefix>/lib/python to PYTHONPATH and `import
# UsdMdl`.  Avoids the regular-package shadowing problem documented in
# docs/README.md.
install(TARGETS _usdMdl
    LIBRARY DESTINATION lib/python/UsdMdl
    RUNTIME DESTINATION lib/python/UsdMdl
)
install(FILES __init__.py DESTINATION lib/python/UsdMdl)

# _usdMdl at <prefix>/lib/python/UsdMdl/ needs to find usdMdl.dll at
# <prefix>/plugin/usd/ at runtime on Linux/macOS.  CMake otherwise
# strips the build-tree RPATH at install.  Windows is unaffected (DLL
# search uses PATH).  Three levels up + plugin/usd gets us there.
set_target_properties(_usdMdl PROPERTIES
    INSTALL_RPATH "${_origin_marker}/../../../plugin/usd"
)

# Stage a build-tree `UsdMdl/` Python package next to the built
# `_usdMdl.{pyd,so}` so `ctest` can run Python tests without requiring
# `cmake --install` first.  Layout mirrors the install tree
# (`UsdMdl/__init__.py`, `UsdMdl/_usdMdl.{pyd,so}`); Python only needs
# `$<TARGET_FILE_DIR:_usdMdl>` on `PYTHONPATH` to satisfy
# `import UsdMdl`.  `cmake/MdlPluginTesting.cmake` adds that path when
# the target exists.
add_custom_command(TARGET _usdMdl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:_usdMdl>/UsdMdl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:_usdMdl>"
        "$<TARGET_FILE_DIR:_usdMdl>/UsdMdl/$<TARGET_FILE_NAME:_usdMdl>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/__init__.py"
        "$<TARGET_FILE_DIR:_usdMdl>/UsdMdl/__init__.py"
    COMMENT "Stage build-tree UsdMdl/ Python package for ctest"
)

# Refresh pure Python changes even when the extension does not need relinking.
add_custom_target(usdMdlPythonPackage ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:_usdMdl>/UsdMdl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:_usdMdl>" "${CMAKE_CURRENT_SOURCE_DIR}/__init__.py"
        "$<TARGET_FILE_DIR:_usdMdl>/UsdMdl"
    DEPENDS _usdMdl
    COMMENT "Refresh build-tree UsdMdl Python package"
    VERBATIM
)

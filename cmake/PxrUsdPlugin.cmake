# SPDX-License-Identifier: Apache-2.0
#
# Compile definitions for USD-style plugin targets (see OpenUSD cmake/macros/Private.cmake).
# These macros are picked up by pxr/base/plug/initConfig.cpp at plugin init time.

function(mdl_usd_plugin_target_defaults _target)
    target_compile_definitions(${_target} PRIVATE
        MFB_PACKAGE_NAME=${_target}
        MFB_ALT_PACKAGE_NAME=${_target}
        PXR_BUILD_LOCATION=usd
        PXR_PLUGIN_BUILD_LOCATION=../plugin/usd
    )
endfunction()

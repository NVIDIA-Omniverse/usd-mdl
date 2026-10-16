//
// SPDX-License-Identifier: Apache-2.0
//

#include "debugCodes.h"

#include <pxr/base/tf/registryManager.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(USD_MDL, "Usd.Mdl.");
    TF_DEBUG_ENVIRONMENT_SYMBOL(USD_MDL_DIAGNOSTICS,
        "Informational and debug messages from MDL module loading.");
}

PXR_NAMESPACE_CLOSE_SCOPE

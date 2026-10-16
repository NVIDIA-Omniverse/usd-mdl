//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <pxr/usd/sdr/declare.h>
#include "api.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Return whether two usdMdl Sdr ports are locally compatible, in either order.
/// Requires one connectable input and one output with matching native MDL
/// types, shapes and named-type identities. Fixed array outputs may feed
/// deferred-size inputs. Null ports or missing MDL declarations return false.
/// Explicit varying outputs cannot feed uniform inputs. This does not validate
/// graph-wide deferred lengths, infer uniformity, or insert type conversions.
/// No SDK initialization, registry access or USD authoring is performed.
USDMDL_API
bool UsdMdlCanConnectTo(SdrShaderPropertyConstPtr property,
                        SdrShaderPropertyConstPtr other);

PXR_NAMESPACE_CLOSE_SCOPE

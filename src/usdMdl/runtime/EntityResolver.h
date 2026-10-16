//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "defines.h"
#include "../api.h"

#include <mi/mdl_sdk.h>

#include <string>
#include <vector>

MDL_RUNTIME_NAMESPACE_OPEN

/// Create an MDL entity resolver backed entirely by OpenUSD Ar.
///
/// URI-encoded modules are decoded to their canonical URL. Absolute MDL module
/// names (leading ::) and resource paths are searched under \p mdlSearchPaths.
/// Relative imports and resources are anchored to their owner file. Tiled and
/// frame-sequence resources are expanded through Ar. A miss returns nullptr;
/// the resolver never bypasses Ar by delegating to the SDK filesystem
/// resolver.
///
/// \param impexp    Import/export API used for frame / UV-tile marker
///                  substitution. The resolver retains its own reference;
///                  the caller keeps ownership of its input reference.
///                  Plain paths still work when it is null.
/// \param mdlSearchPaths Snapshot of `IMdl_configuration::get_mdl_path()`
///                  after application, user, and system paths are configured.
/// \return          A new resolver with refcount 1. Caller owns it and
///                  normally installs it via `set_entity_resolver()`.
USDMDL_API
mi::neuraylib::IMdl_entity_resolver*
CreateArEntityResolver(
    mi::neuraylib::IMdl_impexp_api* impexp,
    std::vector<std::string> mdlSearchPaths);

MDL_RUNTIME_NAMESPACE_CLOSE

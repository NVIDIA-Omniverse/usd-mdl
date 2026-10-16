//
// SPDX-License-Identifier: Apache-2.0
//
// Process-wide tunables exposed via OpenUSD's TfEnvSetting machinery so
// they appear in `pxrEnvSettings`/help and follow the standard discovery
// rules. Defined in EnvSettings.cpp and referenced only inside the
// usdMdl library; no `USDMDL_API` is
// needed on the extern globals or the helper because no caller crosses
// the DLL boundary today.

#pragma once

#include "defines.h"
#include "../api.h"

#include <pxr/pxr.h>
#include <pxr/base/tf/envSetting.h>

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// Opt in to eager MDL module discovery at SdrRegistry startup with `1`.
/// Default: false, so large libraries do not delay registry creation.
/// Asset lookup and explicit module registration remain available on demand.
extern TfEnvSetting<bool> USD_MDL_DISCOVER_ON_STARTUP;

/// Opt in to the shipped standard USD Preview Surface interfaces backed by MDL.
/// Default: false. Independent of the full native MDL catalog scan above.
/// Configure the implementation module's SDK search paths before Sdr startup.
extern TfEnvSetting<bool> USD_MDL_LOAD_USD_PREVIEW_SURFACE;

/// Compatibility for a bare name identifying multiple function overloads.
/// Default: false (reject). When true, choose the first SDK overload and warn.
/// The separate struct elemental preference can override that selection.
/// Explicit signatures must still match exactly. Set before the first lookup.
/// Temporary support for old legacy content; may be removed at any time.
extern TfEnvSetting<bool> USD_MDL_ALLOW_FIRST_OVERLOAD;

/// Prefer field inputs for a bare struct name only when first-overload fallback
/// is enabled too. Default: false. Explicit signatures retain exact matching.
/// Temporary legacy compatibility; may change or be removed at any time.
extern TfEnvSetting<bool> USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR;

/// Compatibility for a material requested with a stale or wrong signature.
/// Default: false (reject). When true, retry by simple material name and warn.
/// Function signatures stay strict. Set before the first lookup.
/// Temporary support for old legacy content; may be removed at any time.
extern TfEnvSetting<bool> USD_MDL_ALLOW_MATERIAL_SIGNATURE_MISMATCH;

/// Opt-in to `IMdl_configuration::add_mdl_user_paths()`.  When false,
/// the SDK's user paths are still picked up if the legacy
/// `MDL_USER_PATH` env var is set; this knob exists so callers can
/// force user paths in without having to mutate `MDL_USER_PATH`.
/// Default: false.
extern TfEnvSetting<bool> USD_MDL_ADD_USER_PATHS;

/// Opt-in to `IMdl_configuration::add_mdl_system_paths()`.  When false,
/// the SDK's system paths are still picked up if the legacy
/// `MDL_SYSTEM_PATH` env var is set.  Default: false.
extern TfEnvSetting<bool> USD_MDL_ADD_SYSTEM_PATHS;

/// Additional MDL search paths separated by the platform path
/// separator (`;` on Windows, `:` elsewhere).  Read once at
/// `Context::ensureInitialized()` and passed straight to
/// `IMdl_configuration::add_mdl_path/add_resource_path`.  Default: "".
extern TfEnvSetting<std::string> USD_MDL_SEARCH_PATHS;

PXR_NAMESPACE_CLOSE_SCOPE

MDL_RUNTIME_NAMESPACE_OPEN

/// Split `USD_MDL_SEARCH_PATHS` on the platform path separator and
/// return the non-empty entries (no normalization, no existence check;
/// the SDK is the source of truth for path validity).
std::vector<std::string> GetUsdMdlSearchPathsFromEnv();

MDL_RUNTIME_NAMESPACE_CLOSE

//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "api.h"

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/pxr.h>
#include <mi/mdl_sdk.h>

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace Utils
{

// USD needs a filename-shaped authoring identity for this SDK-only module.
inline constexpr const char* BuiltinModuleAsset = "mdl-builtin.mdl";
inline constexpr const char* BuiltinModuleName = "::%3Cbuiltins%3E";
bool IsBuiltinModuleAsset(const SdfAssetPath& assetPath);

// UI annotations do not affect whether an exported definition can be used.
bool IsEligibleDefinition(const mi::neuraylib::IFunction_definition* definition);

/// Serialize a VtValue into the string form Sdr stores in its metadata maps.
std::string VtValueToString(const VtValue& value, bool isCompoundElement = false);


/// Strips return types / signatures so #mi::neuraylib::IModule::get_function_overloads receives a
/// plain simple name (MDL-SDK warns if the string ends with \c ')' or includes a signature).
std::string
StripMdlNameForFunctionOverloadLookup(const char* mdlSimpleOrSubId);

/// A missing signature differs from an explicit zero-parameter signature.
struct MdlSubIdentifier
{
    std::string name;
    std::vector<std::string> parameterTypes;
    bool hasSignature = false;
};

/// Parse name or name(type,...); reject malformed syntax instead of falling
/// back to name-only lookup. Whitespace around the name/types is ignored.
bool ParseMdlSubIdentifier(const std::string& text, MdlSubIdentifier& result);

/// Match SDK parameter identities, allowing unqualified own-module types and
/// explicitly package-relative names. Never drop an external type's qualifier.
bool MatchesMdlSubIdentifier(
    const MdlSubIdentifier& request,
    const mi::neuraylib::IFunction_definition* definition);

/// Computes the same sub-identifier token used during discovery (per-function / overload).
///
/// Unique definitions use a simple name; overloads include parameter types.
/// Own-module named types are unqualified. External types retain their SDK
/// identity, with package-relative spellings only for encoded off-path siblings.
/// See docs/sub-identifiers.md for the authoring contract.
TfToken
ComputeMdlSubIdentifier(
    const mi::neuraylib::IFunction_definition* def,
    const mi::neuraylib::IModule* module);

} // namespace Utils

PXR_NAMESPACE_CLOSE_SCOPE

//
// SPDX-License-Identifier: Apache-2.0
//

#include "EnvSettings.h"

#include <pxr/base/arch/fileSystem.h>

#include <sstream>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(
    USD_MDL_DISCOVER_ON_STARTUP, false,
    "Discover MDL modules when SdrRegistry starts.");

TF_DEFINE_ENV_SETTING(
    USD_MDL_LOAD_USD_PREVIEW_SURFACE, false,
    "Load the shipped USD Preview Surface interfaces backed by MDL when "
    "SdrRegistry starts, independently of native MDL catalog discovery.");

TF_DEFINE_ENV_SETTING(
    USD_MDL_ALLOW_FIRST_OVERLOAD, false,
    "Allow an ambiguous bare sub-identifier to select the first SDK overload "
    "with a warning. This option does not relax explicit signatures. "
    "Temporary compatibility for old legacy content; may be removed at any time.");

TF_DEFINE_ENV_SETTING(
    USD_MDL_PREFER_STRUCT_ELEMENTAL_CONSTRUCTOR, false,
    "With USD_MDL_ALLOW_FIRST_OVERLOAD enabled, prefer the elemental constructor "
    "for an ambiguous bare struct name. Explicit signatures remain unchanged. "
    "Temporary compatibility for old legacy content; may change or be removed at any time.");

TF_DEFINE_ENV_SETTING(
    USD_MDL_ALLOW_MATERIAL_SIGNATURE_MISMATCH, false,
    "Allow a material with a mismatched explicit signature to resolve by simple "
    "name with a warning. Function signatures must still match. "
    "Temporary compatibility for old legacy content; may be removed at any time.");

TF_DEFINE_ENV_SETTING(
    USD_MDL_ADD_USER_PATHS, false,
    "Ask MDL SDK to add its user MDL paths "
    "(IMdl_configuration::add_mdl_user_paths).");

TF_DEFINE_ENV_SETTING(
    USD_MDL_ADD_SYSTEM_PATHS, false,
    "Ask MDL SDK to add its system MDL paths "
    "(IMdl_configuration::add_mdl_system_paths).");

TF_DEFINE_ENV_SETTING(
    USD_MDL_SEARCH_PATHS, "",
    "Filesystem MDL search paths separated by the platform path separator "
    "(';' on Windows, ':' elsewhere).");

PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE

MDL_RUNTIME_NAMESPACE_OPEN

std::vector<std::string>
GetUsdMdlSearchPathsFromEnv()
{
    std::vector<std::string> result;
    const std::string value = TfGetEnvSetting(USD_MDL_SEARCH_PATHS);
    if (value.empty()) {
        return result;
    }
    std::stringstream stream(value);
    std::string item;
    // ARCH_PATH_LIST_SEP is a string literal (";" on Windows, ":" elsewhere);
    // std::getline wants a single char.
    while (std::getline(stream, item, ARCH_PATH_LIST_SEP[0])) {
        if (!item.empty()) {
            result.push_back(std::move(item));
        }
    }
    return result;
}

MDL_RUNTIME_NAMESPACE_CLOSE

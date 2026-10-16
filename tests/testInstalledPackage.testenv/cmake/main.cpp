// SPDX-License-Identifier: Apache-2.0

#include <usdMdl/addModuleToRegistry.h>
#include <usdMdl/initialize.h>

#include <pxr/base/vt/value.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <iostream>

static bool CheckModule(const char* module, const char* input, const char* output)
{
    const auto nodes = pxr::UsdMdlAddModuleToRegistry(pxr::SdfAssetPath(module));
    if (nodes.size() != 1 || !nodes.front() || !nodes.front()->IsValid()) {
        std::cerr << "Expected one valid installed-package node\n";
        return false;
    }
    const auto* node = nodes.front();
    if (node->GetSourceType() != pxr::TfToken("mdl") ||
        node->GetShaderInputNames() != pxr::TfTokenVector{pxr::TfToken(input)} ||
        node->GetShaderOutputNames() != pxr::TfTokenVector{pxr::TfToken(output)}) {
        std::cerr << "Unexpected source type or ports\n";
        return false;
    }
    const auto* property = node->GetShaderInput(pxr::TfToken(input));
    if (!property || property->GetDefaultValue() != pxr::VtValue(0.5f)) {
        std::cerr << "Unexpected input default\n";
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "Usage: installedConsumer module.mdl input output\n";
        return 1;
    }
    if (!pxr::UsdMdlInitialize()) {
        std::cerr << "Installed runtime initialization failed\n";
        return 1;
    }
    // Release node references before normal runtime shutdown.
    const bool passed = CheckModule(argv[1], argv[2], argv[3]);
    pxr::UsdMdlShutdown();
    if (!passed) {
        return 1;
    }
    std::cout << "Installed usdMdl C++ consumer passed.\n";
    return 0;
}

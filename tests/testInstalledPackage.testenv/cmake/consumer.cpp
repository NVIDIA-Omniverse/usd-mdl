// SPDX-License-Identifier: Apache-2.0
#include <usdMdl/addModuleToRegistry.h>
#include <usdMdl/initialize.h>
#include <usdMdl/shaderDefinitions.h>
#include <usdMdl/enumValue.h>

PXR_NAMESPACE_USING_DIRECTIVE
int main()
{
    auto volatile registration = &UsdMdlAddShaderDefinitionsToRegistry;
    auto volatile enumBinding = &UsdMdlCreateEnumValue;
    return registration && enumBinding ? 0 : 1;
}

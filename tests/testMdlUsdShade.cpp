//
// SPDX-License-Identifier: Apache-2.0
//

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/shader.h>

#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "test_exit.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

std::string
JoinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) {
        return b;
    }
    char last = a.back();
    if (last == '/' || last == '\\') {
        return a + b;
    }
    return a + "/" + b;
}

} // namespace

int
main()
{
    std::cout << "\nRUN testMdlUsdShade\n" << std::endl;

    const std::string testDir = TfGetenv("PXR_TEST_DIR");
    if (testDir.empty()) {
        std::fprintf(stderr, "[testMdlUsdShade] PXR_TEST_DIR not set\n");
        mdl_test::FlushAndExit(1);
    }

    PlugPluginPtr plug =
        PlugRegistry::GetInstance().GetPluginWithName("usdMdl");
    TF_AXIOM(plug);
    TF_AXIOM(plug->Load());

    // Force the Sdr singleton to come up (and discover plugins) before we
    // resolve via UsdShadeShader::GetShaderNodeForSourceType.
    SdrRegistry::GetInstance();

    const std::string mdlFile = JoinPath(JoinPath(testDir, "mdl"), "color.mdl");
    const std::string mdlResolved = ArGetResolver().Resolve(mdlFile);
    if (mdlResolved.empty()) {
        std::fprintf(stderr,
                     "[testMdlUsdShade] could not resolve '%s'\n", mdlFile.c_str());
        mdl_test::FlushAndExit(1);
    }
    std::cout << "  color.mdl: " << mdlResolved << std::endl;

    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    TF_AXIOM(stage);

    UsdPrim world = stage->DefinePrim(SdfPath("/World"));
    TF_AXIOM(world);

    UsdPrim shaderPrim = stage->DefinePrim(SdfPath("/World/Mdl"), TfToken("Shader"));
    TF_AXIOM(shaderPrim);

    UsdShadeShader shader(shaderPrim);
    TF_AXIOM(shader);

    const TfToken mdlSourceType("mdl");

    const SdfAssetPath mdlAsset(mdlFile, mdlResolved);
    TF_AXIOM(shader.SetSourceAsset(mdlAsset, mdlSourceType));
    TF_AXIOM(shader.SetSourceAssetSubIdentifier(TfToken("test_color"), mdlSourceType));

    std::cout << "\nRESOLVE via UsdShadeShader::GetShaderNodeForSourceType(\"mdl\")\n"
              << std::endl;

    SdrShaderNodeConstPtr const node = shader.GetShaderNodeForSourceType(mdlSourceType);
    if (!node) {
        std::fprintf(stderr,
                     "[testMdlUsdShade] GetShaderNodeForSourceType returned null\n");
        mdl_test::FlushAndExit(1);
    }
    TF_AXIOM(node->IsValid());

    std::cout << "  identifier: " << node->GetIdentifier().GetString() << "\n";
    std::cout << "  name:       " << node->GetName() << "\n";
    std::cout << "  sourceType: " << node->GetSourceType().GetString() << "\n";

    TF_AXIOM(node->GetSourceType() == mdlSourceType);

    // test_color in mdl/color.mdl exports an input named 'param' (color3f) and a single output.
    SdrShaderPropertyConstPtr const paramIn = node->GetShaderInput(TfToken("param"));
    if (!paramIn) {
        std::fprintf(stderr,
                     "[testMdlUsdShade] expected input 'param' on test_color, not found\n");
        mdl_test::FlushAndExit(1);
    }
    std::cout << "  input 'param' type: " << paramIn->GetType().GetString() << "\n";

    if (node->GetShaderOutputNames().empty()) {
        std::fprintf(stderr,
                     "[testMdlUsdShade] expected at least one output on test_color\n");
        mdl_test::FlushAndExit(1);
    }

    const auto inputType = paramIn->GetTypeAsSdfType();
    TF_AXIOM(inputType.HasSdfType());
    TF_AXIOM(inputType.GetSdfType() == SdfValueTypeNames->Color3f);
    const VtValue expected(GfVec3f(0.1f, 0.2f, 0.3f));
    TF_AXIOM(paramIn->GetDefaultValue() == expected);
    TF_AXIOM(paramIn->GetDefaultValueAsSdfType() == expected);
    const UsdShadeInput input = shader.CreateInput(TfToken("param"), inputType.GetSdfType());
    TF_AXIOM(input.Set(paramIn->GetDefaultValueAsSdfType()));
    VtValue authored;
    TF_AXIOM(input.Get(&authored));
    TF_AXIOM(authored == expected);
    for (const TfToken& name : node->GetShaderOutputNames()) {
        const auto outputType = node->GetShaderOutput(name)->GetTypeAsSdfType();
        TF_AXIOM(outputType.HasSdfType());
        TF_AXIOM(outputType.GetSdfType() == SdfValueTypeNames->Color3f);
    }

    std::printf("\nOK: resolved SdrShaderNode through UsdStage -> UsdShade.Shader -> SdrRegistry\n");

    std::cout.flush();
    std::cerr.flush();

    // Same teardown approach as testMdlSdrPlugins: bypass DLL detach to avoid
    // re-entering MDL SDK code via Sdr singleton destruction.
    mdl_test::FlushAndExit(0);
}

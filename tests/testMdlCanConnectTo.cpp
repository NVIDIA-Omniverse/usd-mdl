//
// SPDX-License-Identifier: Apache-2.0
//

#include "canConnectTo.h"
#include "tokens.h"

#include <pxr/base/vt/value.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <cstdio>
#include <memory>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

std::unique_ptr<SdrShaderProperty> _Port(bool output, const char* nativeType,
                                       SdrTokenMap metadata = {})
{
    if (nativeType) metadata[UsdMdlMetadataTokens->Type] = nativeType;
    // Deliberately identical storage: only native declarations should decide.
    return std::make_unique<SdrShaderProperty>(
        TfToken(output ? "out" : "in"), SdrPropertyTypes->Float, VtValue(),
        output, 0, metadata, SdrTokenMap(), SdrOptionVec());
}

SdrTokenMap _Array(const char* element, const char* size, const char* symbol = "N")
{
    SdrTokenMap metadata = {
        {UsdMdlMetadataTokens->ArrayElementType, element},
        {UsdMdlMetadataTokens->ArrayIsImmediateSized, size ? "true" : "false"},
    };
    if (size) metadata[UsdMdlMetadataTokens->ArraySize] = size;
    else metadata[UsdMdlMetadataTokens->ArrayDeferredSizeSymbol] = symbol;
    return metadata;
}

bool _Check(const char* label, const SdrShaderProperty* a,
            const SdrShaderProperty* b, bool expected)
{
    const bool forward = UsdMdlCanConnectTo(a, b);
    const bool reverse = UsdMdlCanConnectTo(b, a);
    if (forward == expected && reverse == expected) return true;
    std::fprintf(stderr, "%s: expected %d, got %d / %d\n",
                 label, expected, forward, reverse);
    return false;
}

} // namespace

int main()
{
    int failures = 0;
    auto input = _Port(false, "float");
    auto output = _Port(true, "float");
    auto check = [&](const char* label, const SdrShaderProperty* a,
                     const SdrShaderProperty* b, bool expected) {
        if (!_Check(label, a, b, expected)) ++failures;
    };
    check("matching scalar", input.get(), output.get(), true);
    check("null input", nullptr, output.get(), false);
    check("two null ports", nullptr, nullptr, false);
    check("same input", input.get(), input.get(), false);
    check("two inputs", input.get(), _Port(false, "float").get(), false);
    check("two outputs", output.get(), _Port(true, "float").get(), false);
    check("nonconnectable", _Port(false, "float", {{SdrPropertyMetadata->Connectable, "0"}}).get(), output.get(), false);
    check("missing MDL metadata", _Port(false, nullptr).get(), output.get(), false);
    check("unknown native type", _Port(false, "made_up").get(), _Port(true, "made_up").get(), false);
    check("scalar family", input.get(), _Port(true, "int").get(), false);
    check("matrix shape", _Port(false, "float2x3").get(), _Port(true, "float3x2").get(), false);
    check("matrix match", _Port(false, "float2x3").get(), _Port(true, "float2x3").get(), true);
    check("vector width", _Port(false, "float2").get(), _Port(true, "float3").get(), false);
    check("color is not float3", _Port(false, "color").get(), _Port(true, "float3").get(), false);
    check("resource family", _Port(false, "texture_2d").get(), _Port(true, "texture_cube").get(), false);
    check("distribution family", _Port(false, "bsdf").get(), _Port(true, "edf").get(), false);
    check("ordinary aliases", _Port(false, "float", {{UsdMdlMetadataTokens->Symbol, "::AliasA"}}).get(),
          _Port(true, "float", {{UsdMdlMetadataTokens->Symbol, "::AliasB"}}).get(), true);
    check("renderType override", _Port(false, "float", {{SdrPropertyMetadata->RenderType, "int"}}).get(), output.get(), true);
    for (const char* type : {"enum", "struct"}) {
        auto named = _Port(false, type, {{UsdMdlMetadataTokens->Symbol, "::package::A"}});
        check(type, named.get(), _Port(true, type, {{UsdMdlMetadataTokens->Symbol, "::package::A"}}).get(), true);
        check("distinct named type", named.get(), _Port(true, type, {{UsdMdlMetadataTokens->Symbol, "::other::A"}}).get(), false);
        check("missing named symbol", named.get(), _Port(true, type).get(), false);
    }
    auto fixed = _Port(false, "[]", _Array("float2x3", "2"));
    check("fixed array match", fixed.get(), _Port(true, "[]", _Array("float2x3", "2")).get(), true);
    check("fixed array length", fixed.get(), _Port(true, "[]", _Array("float2x3", "3")).get(), false);
    check("flattened array shape", fixed.get(), _Port(true, "[]", _Array("float3x2", "2")).get(), false);
    check("array versus matrix", fixed.get(), _Port(true, "float2x3").get(), false);
    check("deferred to fixed", fixed.get(), _Port(true, "[]", _Array("float2x3", nullptr)).get(), false);
    auto deferred = _Port(false, "[]", _Array("float2x3", nullptr));
    check("fixed to deferred", deferred.get(), _Port(true, "[]", _Array("float2x3", "2")).get(), true);
    check("deferred names local", deferred.get(), _Port(true, "[]", _Array("float2x3", nullptr, "Other")).get(), true);
    auto zero = _Port(false, "[]", _Array("float", "0"));
    check("zero array", zero.get(), _Port(true, "[]", _Array("float", "0")).get(), true);
    check("zero versus scalar", zero.get(), output.get(), false);
    check("zero versus singleton", zero.get(), _Port(true, "[]", _Array("float", "1")).get(), false);
    auto namedArray = _Array("enum", "2");
    namedArray[UsdMdlMetadataTokens->Symbol] = "::package::A";
    auto arrayInput = _Port(false, "[]", namedArray);
    check("named array match", arrayInput.get(), _Port(true, "[]", namedArray).get(), true);
    namedArray[UsdMdlMetadataTokens->Symbol] = "::package::B";
    check("named array mismatch", arrayInput.get(), _Port(true, "[]", namedArray).get(), false);
    for (const char* size : {"", "-1", "+2", "2tail", " 2", "184467440737095516160"}) {
        check("invalid count", _Port(false, "[]", _Array("float", size)).get(),
              _Port(true, "[]", _Array("float", "2")).get(), false);
    }
    check("missing array element", _Port(false, "[]", _Array("", "2")).get(),
          _Port(true, "[]", _Array("float", "2")).get(), false);
    check("missing deferred symbol", _Port(false, "[]", _Array("float", nullptr, "")).get(),
          _Port(true, "[]", _Array("float", "2")).get(), false);
    auto uniform = _Port(false, "float", {{UsdMdlMetadataTokens->Modifier, "uniform"}});
    check("varying to uniform", uniform.get(), _Port(true, "float", {{UsdMdlMetadataTokens->Modifier, "varying"}}).get(), false);
    check("uniform match", uniform.get(), _Port(true, "float", {{UsdMdlMetadataTokens->Modifier, "uniform"}}).get(), true);
    check("unspecified uniformity", uniform.get(), output.get(), true);
    check("uniform to varying", _Port(false, "float", {{UsdMdlMetadataTokens->Modifier, "varying"}}).get(),
          _Port(true, "float", {{UsdMdlMetadataTokens->Modifier, "uniform"}}).get(), true);
    check("invalid modifier", input.get(), _Port(true, "float", {{UsdMdlMetadataTokens->Modifier, "invalid"}}).get(), false);
    return failures ? 1 : 0;
}

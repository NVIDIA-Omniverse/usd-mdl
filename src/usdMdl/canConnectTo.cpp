//
// SPDX-License-Identifier: Apache-2.0
//

#include "canConnectTo.h"
#include "tokens.h"

#include <pxr/usd/sdr/shaderProperty.h>

#include <algorithm>
#include <charconv>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

const std::string& _Get(const SdrTokenMap& metadata, const TfToken& key)
{
    static const std::string empty;
    const auto found = metadata.find(key);
    return found == metadata.end() ? empty : found->second;
}

struct MdlType {
    std::string element;
    std::string symbol;
    std::string modifier;
    bool array = false;
    bool immediate = false;
    size_t size = 0;
};

bool _ReadType(const SdrShaderProperty& property, MdlType& type)
{
    const SdrTokenMap& metadata = property.GetMetadata();
    const std::string& nativeType = _Get(metadata, UsdMdlMetadataTokens->Type);
    type.array = nativeType == UsdMdlTypes->Array.GetString();
    type.element = type.array
        ? _Get(metadata, UsdMdlMetadataTokens->ArrayElementType) : nativeType;
    const TfToken element(type.element);
    const auto& knownTypes = UsdMdlTypes->allTokens;
    if (element.IsEmpty() || element == UsdMdlTypes->Array
        || std::find(knownTypes.begin(), knownTypes.end(), element) == knownTypes.end()) {
        return false;
    }
    if (element == UsdMdlTypes->Enum || element == UsdMdlTypes->Struct) {
        type.symbol = _Get(metadata, UsdMdlMetadataTokens->Symbol);
        if (type.symbol.empty()) return false;
    }
    type.modifier = _Get(metadata, UsdMdlMetadataTokens->Modifier);
    if (!type.modifier.empty() && type.modifier != UsdMdlTypeModifiers->Uniform.GetString()
        && type.modifier != UsdMdlTypeModifiers->Varying.GetString()) {
        return false;
    }
    if (!type.array) return true;

    const std::string& immediate = _Get(metadata, UsdMdlMetadataTokens->ArrayIsImmediateSized);
    if (immediate == "false") {
        // Size symbols are local to calls, so their spellings cannot establish
        // compatibility between two unrelated nodes.
        return !_Get(metadata, UsdMdlMetadataTokens->ArrayDeferredSizeSymbol).empty();
    }
    if (immediate != "true") return false;
    type.immediate = true;
    const std::string& size = _Get(metadata, UsdMdlMetadataTokens->ArraySize);
    if (size.empty()) return false;
    const auto parsed = std::from_chars(size.data(), size.data() + size.size(), type.size);
    return parsed.ec == std::errc() && parsed.ptr == size.data() + size.size();
}

} // namespace

bool UsdMdlCanConnectTo(SdrShaderPropertyConstPtr property,
                        SdrShaderPropertyConstPtr other)
{
    if (!property || !other || property->IsOutput() == other->IsOutput()) return false;
    const SdrShaderProperty& input = property->IsOutput() ? *other : *property;
    const SdrShaderProperty& output = property->IsOutput() ? *property : *other;
    if (!input.IsConnectable()) return false;

    MdlType inputType, outputType;
    if (!_ReadType(input, inputType) || !_ReadType(output, outputType)
        || inputType.array != outputType.array
        || inputType.element != outputType.element
        || inputType.symbol != outputType.symbol) {
        return false;
    }
    if (inputType.modifier == UsdMdlTypeModifiers->Uniform.GetString()
        && outputType.modifier == UsdMdlTypeModifiers->Varying.GetString()) {
        return false;
    }
    // A deferred output has no known length to satisfy an immediate input.
    // For deferred inputs, graph-wide binding remains the caller's job.
    return !inputType.array || !inputType.immediate
        || (outputType.immediate && inputType.size == outputType.size);
}

PXR_NAMESPACE_CLOSE_SCOPE

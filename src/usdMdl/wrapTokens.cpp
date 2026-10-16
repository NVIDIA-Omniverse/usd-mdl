//
// SPDX-License-Identifier: Apache-2.0
//

#include "pxr/pxr.h"
#include "pxr/base/tf/pyStaticTokens.h"

#include "tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

void wrapTokens()
{
    // Python binding names track the C++ token-set names with one exception:
    // UsdMdlMetadataTokens is exposed as `UsdMdl.Metadata` (without the
    // `Tokens` suffix) to match how OpenUSD's Sdr exposes metadata key sets
    // (e.g. `Sdr.PropertyMetadata`).
    TF_PY_WRAP_PUBLIC_TOKENS("Tokens", UsdMdlTokens, USD_MDL_TOKENS);
    TF_PY_WRAP_PUBLIC_TOKENS("Types", UsdMdlTypes, USD_MDL_TYPES);
    TF_PY_WRAP_PUBLIC_TOKENS("Metadata", UsdMdlMetadataTokens, USD_MDL_METADATA_TOKENS);
    TF_PY_WRAP_PUBLIC_TOKENS("NodeMetadata", UsdMdlNodeMetadataTokens, USD_MDL_NODE_METADATA_TOKENS);
    TF_PY_WRAP_PUBLIC_TOKENS("ExpressionKinds", UsdMdlExpressionKinds, USD_MDL_EXPRESSION_KINDS);
    TF_PY_WRAP_PUBLIC_TOKENS("StructTypes", UsdMdlStructTypes, USD_MDL_STRUCT_TYPES);
    TF_PY_WRAP_PUBLIC_TOKENS("TypeModifiers", UsdMdlTypeModifiers, USD_MDL_TYPE_MODIFIERS);
    TF_PY_WRAP_PUBLIC_TOKENS("FunctionDefinitionTypes", UsdMdlFunctionDefinitionTypes, USD_MDL_FUNCTION_DEFINITION_TYPES);
}

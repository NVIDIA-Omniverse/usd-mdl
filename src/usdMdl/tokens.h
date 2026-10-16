//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef MDL_USD_MDL_TOKENS_H
#define MDL_USD_MDL_TOKENS_H

#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>

#include "api.h"

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
#define USD_MDL_TOKENS\
    ((DefaultOutputPortName, "out"))\
    ((Mdl, "mdl"))

TF_DECLARE_PUBLIC_TOKENS(UsdMdlTokens, USDMDL_API, USD_MDL_TOKENS);


#define USD_MDL_TYPES\
    ((Array, "[]"))\
    ((Bool, "bool"))\
    ((Bool2, "bool2"))\
    ((Bool3, "bool3"))\
    ((Bool4, "bool4"))\
    ((Bsdf, "bsdf"))\
    ((Bsdf_Measurement, "bsdf_measurement"))\
    ((Color, "color"))\
    ((Double, "double"))\
    ((Double2, "double2"))\
    ((Double2x2, "double2x2"))\
    ((Double2x3, "double2x3"))\
    ((Double2x4, "double2x4"))\
    ((Double3, "double3"))\
    ((Double3x2, "double3x2"))\
    ((Double3x3, "double3x3"))\
    ((Double3x4, "double3x4"))\
    ((Double4, "double4"))\
    ((Double4x2, "double4x2"))\
    ((Double4x3, "double4x3"))\
    ((Double4x4, "double4x4"))\
    ((Edf, "edf"))\
    ((Enum, "enum"))\
    ((Float, "float"))\
    ((Float2, "float2"))\
    ((Float2x2, "float2x2"))\
    ((Float2x3, "float2x3"))\
    ((Float2x4, "float2x4"))\
    ((Float3, "float3"))\
    ((Float3x2, "float3x2"))\
    ((Float3x3, "float3x3"))\
    ((Float3x4, "float3x4"))\
    ((Float4, "float4"))\
    ((Float4x2, "float4x2"))\
    ((Float4x3, "float4x3"))\
    ((Float4x4, "float4x4"))\
    ((Hair_Bsdf, "hair_bsdf"))\
    ((Int, "int"))\
    ((Int2, "int2"))\
    ((Int3, "int3"))\
    ((Int4, "int4"))\
    ((Light_Profile, "light_profile"))\
    ((String, "string"))\
    ((Struct, "struct"))\
    ((Texture2d, "texture_2d"))\
    ((Texture3d, "texture_3d"))\
    ((TextureBsdfData, "texture_bsdf_data"))\
    ((TextureCube, "texture_cube"))\
    ((TexturePtex, "texture_ptex"))\
    ((Vdf, "vdf"))


TF_DECLARE_PUBLIC_TOKENS(UsdMdlTypes, USDMDL_API, USD_MDL_TYPES);


#define USD_MDL_METADATA_TOKENS\
    ((Annotation, "mdl.annotation"))\
    ((AnnotationModule, "mdl.annotationModule"))\
    ((AnnotationType, "mdl.annotationType"))\
    ((AnnotationValue, "mdl.annotationValue"))\
    ((ArrayDeferredSizeSymbol, "mdl.arrayDeferredSizeSymbol"))\
    ((ArrayIsImmediateSized, "mdl.arrayIsImmediateSized"))\
    ((ArraySize, "mdl.arraySize"))\
    ((ArrayElementType, "mdl.arrayElementType"))\
    ((ExpressionKind, "mdl.expressionKind"))\
    ((ExpressionValue, "mdl.expressionValue"))\
    ((HasDefault, "mdl.hasDefault"))\
    ((Modifier, "mdl.modifier"))\
    ((Semantic, "mdl.semantic"))\
    ((StructType, "mdl.structType"))\
    ((Symbol, "mdl.symbol"))\
    ((TextureGamma, "mdl.textureGamma"))\
    ((TextureSelector, "mdl.textureSelector"))\
    ((Thumbnail, "mdl.thumbnail"))\
    ((Type, "mdl.type"))

TF_DECLARE_PUBLIC_TOKENS(UsdMdlMetadataTokens, USDMDL_API, USD_MDL_METADATA_TOKENS);


// Sdr node-level metadata keys, used as keys in
// `SdrShaderNode::GetMetadata()`.  Distinct from
// `UsdMdlMetadataTokens` (which holds Sdr *property*-level keys; the
// `mdl.` dotted form follows Sdr property-metadata convention).
// Node-level keys use the `mdl:` prefix for plugin-specific information.
// See docs/python-api.md for the value and lifetime contracts.
#define USD_MDL_NODE_METADATA_TOKENS\
    ((SubIdentifier, "mdl:subIdentifier"))\
    ((ModuleUsdIdentifier, "mdl:moduleUsdIdentifier"))\
    ((MdlNameWithSignature, "mdl:mdlNameWithSignature"))\
    ((MdlDbModuleName, "mdl:mdl_db_module_name"))\
    ((MdlDbDefinitionName, "mdl:mdl_db_definition_name"))\
    ((IsEmbeddedModule, "mdl:is_embedded_module"))\
    ((DefinitionType, "mdl:definitionType"))\
    ((Hidden, "mdl:hidden"))

TF_DECLARE_PUBLIC_TOKENS(UsdMdlNodeMetadataTokens, USDMDL_API, USD_MDL_NODE_METADATA_TOKENS);


#define USD_MDL_EXPRESSION_KINDS\
    (Constant)\
    (Call)\
    (Parameter)

TF_DECLARE_PUBLIC_TOKENS(UsdMdlExpressionKinds, USDMDL_API, USD_MDL_EXPRESSION_KINDS);


#define USD_MDL_STRUCT_TYPES\
    ((Material, "material"))\
    ((Material_Emission, "material_emission"))\
    ((Material_Geometry, "material_geometry"))\
    ((Material_Surface, "material_surface"))\
    ((Material_Volume, "material_volume"))\
    ((User, "user"))

TF_DECLARE_PUBLIC_TOKENS(UsdMdlStructTypes, USDMDL_API, USD_MDL_STRUCT_TYPES);

#define USD_MDL_TYPE_MODIFIERS\
    ((Uniform, "uniform"))\
    ((Varying, "varying"))

TF_DECLARE_PUBLIC_TOKENS(UsdMdlTypeModifiers, USDMDL_API, USD_MDL_TYPE_MODIFIERS);

#define USD_MDL_FUNCTION_DEFINITION_TYPES\
    ((Function, "function"))\
    ((Material, "material"))

TF_DECLARE_PUBLIC_TOKENS(UsdMdlFunctionDefinitionTypes, USDMDL_API, USD_MDL_FUNCTION_DEFINITION_TYPES);

// clang-format on

PXR_NAMESPACE_CLOSE_SCOPE

#endif

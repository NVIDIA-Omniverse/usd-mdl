//
// SPDX-License-Identifier: Apache-2.0
//

#include "struct.h"

#include "../annotation.h"
#include "../errorMessages.h"
#include <mi/mdl_sdk.h>

using namespace mi::neuraylib;

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
static mi::Sint32 _GetNodeOutputPortDefault(const IAnnotation_block* const annotation)
{
    // return the value of the node_port_mode annotation if it
    // exists, else -1

    static constexpr char kNodeOutputPortDefault[] = "::anno::node_output_port_default(::anno::node_port_mode)";

    mi::Sint32 val = -1;

    Annotation_wrapper wrapper(annotation);
    wrapper.get_annotation_param_value_by_name<mi::Sint32>(kNodeOutputPortDefault, 0, val);
    return val;
}
} // namespace

Converter::OutputsVec StructConverter::GetOutputs(const IType* const iType,
                                                  const IAnnotation_block* const returnAnnotation,
                                                  const std::vector<std::string> &outputNames,
                                                  const MdlModule* mdlModule,
                                                  const DebugContext& debugCtx)
{
    std::unique_ptr<StructConverter> structConverter(new StructConverter(iType, mdlModule, debugCtx));
    // make_unique never returns nullptr; bailing out on allocation
    // failure is left to the standard library (will throw bad_alloc).

    if (!structConverter->IsValid() || !structConverter->_iTypeStruct
        || outputNames.empty()) {
        return {};
    }

    const mi::base::Handle<const IType_struct>& iTypeStruct(structConverter->_iTypeStruct);

    // check the annotation on the port
    mi::Sint32 nodeOutputPortDefault = _GetNodeOutputPortDefault(returnAnnotation);

    // if the annotation on the port does not exist then check the annotation
    // on the struct type
    if (nodeOutputPortDefault == -1)
    {
        const mi::base::Handle<const IAnnotation_block> typeAnnotation(iTypeStruct->get_annotations());

        nodeOutputPortDefault = _GetNodeOutputPortDefault(typeAnnotation.get());
    }

    // node_port_mode is defined in MDL as:
    //    enum node_port_mode {
    //        node_port_value_only,
    //        node_port_fields_only,
    //        node_port_value_and_fields};

    OutputsVec outputs;

    // add fields if requested
    if (nodeOutputPortDefault >= 1)
    {
        for (mi::Size i = 0, fieldCount = iTypeStruct->get_size(); i < fieldCount; ++i)
        {
            const std::string fieldName = TfSafeString(iTypeStruct->get_field_name(i));

            const DebugContext fieldDebugCtx{&debugCtx, "field", fieldName.c_str()};

            const mi::base::Handle<const IType> structFieldType(iTypeStruct->get_field_type(i));
            if (!structFieldType)
            {
                ERROR_GET_ITEM(fieldDebugCtx, IType, IType_struct);
                return Converter::OutputsVec();
            }

            std::unique_ptr<Converter> converter(
                Converter::GetConverter(structFieldType.get(), mdlModule, fieldDebugCtx));

            if (!converter)
            {
                return Converter::OutputsVec();
            }

            outputs.emplace_back(std::move(converter), fieldName, true);
        }
    }

    // add the struct itself if requested
    if (nodeOutputPortDefault != 1)
    {
        // Create a converter for each additional output.
        for (size_t i = 1, j = outputNames.size(); i < j; ++i)
        {
            outputs.emplace_back(std::make_unique<StructConverter>(*structConverter), outputNames[i]);
        }

        outputs.emplace_back(std::move(structConverter), outputNames[0]);

    }

    return outputs;
}

PXR_NAMESPACE_CLOSE_SCOPE

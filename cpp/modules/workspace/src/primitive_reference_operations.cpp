#include <zima/workspace/primitive_reference_operations.hpp>
#include <zima/workspace/feature_reference_input.hpp>
namespace zima::workspace {
bool set_primitive_reference(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    const std::string& container,std::size_t index,document::ConstructionReference source,bool derive_orientation) {
    const auto* state=live.open_part(id);
    if(!state)throw PlacementEditError("unsupported_document","Primitive operations require an open Part.");
    const auto* existing=state->session.document().find_container(container);
    if(!existing)throw PlacementEditError("container_not_found","The requested container does not exist.");
    if(!primitive_definition(existing->feature_kind))throw PlacementEditError("wrong_feature","This container is not a supported primitive.");
    auto value=prepare_part_feature_reference(live,id,container,index,std::move(source),derive_orientation);
    return commit_primitive(live,kernel,id,std::move(value),PrimitiveEditMode::Replace);
}
}

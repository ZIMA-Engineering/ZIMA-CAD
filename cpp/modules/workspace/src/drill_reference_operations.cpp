#include <zima/workspace/drill_reference_operations.hpp>
#include <zima/workspace/hole_operations.hpp>
#include <zima/workspace/opening_operations.hpp>
namespace zima::workspace {
bool set_drill_placement_reference(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    const std::string& container,std::size_t index,document::ConstructionReference source,bool derive_orientation) {
    const auto* state=live.open_part(id);
    if(!state)throw PlacementEditError("unsupported_document","Hole and Opening references require an open Part.");
    const auto* existing=state->session.document().find_container(container);
    if(!existing)throw PlacementEditError("container_not_found","The requested container does not exist.");
    const auto kind=existing->feature_kind;
    if(kind!=document::FeatureKind::Hole&&kind!=document::FeatureKind::Thread)
        throw PlacementEditError("wrong_feature","The reference command does not match the Hole or Opening type.");
    auto value=prepare_part_feature_reference(live,id,container,index,std::move(source),derive_orientation);
    if(kind==document::FeatureKind::Hole)return commit_hole(live,kernel,id,std::move(value),HoleEditMode::Replace);
    return commit_opening(live,kernel,id,std::move(value),OpeningEditMode::Replace);
}
}

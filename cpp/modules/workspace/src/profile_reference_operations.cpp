#include <zima/workspace/profile_reference_operations.hpp>
#include <zima/workspace/feature_reference_input.hpp>
namespace zima::workspace {
bool set_part_profile_reference(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    const std::string& container,std::size_t index,document::ConstructionReference source,bool derive_orientation) {
    const auto* state=live.open_part(id);
    if(!state)throw ProfileOperationError("unsupported_document","Profile operations require an open Part.");
    const auto* existing=state->session.document().find_container(container);
    if(!existing)throw ProfileOperationError("container_not_found","The requested container does not exist.");
    if(existing->feature_kind!=document::FeatureKind::Extrusion&&existing->feature_kind!=document::FeatureKind::Revolution)
        throw ProfileOperationError("wrong_feature","The requested profile type does not match the container.");
    auto value=prepare_part_feature_reference(live,id,container,index,std::move(source),derive_orientation);
    normalize_owned_profile_front_references(value.placement.references);
    if(value==*existing)return false;
    commit_profile(live,kernel,id,std::move(value),ProfileEditMode::Replace);
    return true;
}
}

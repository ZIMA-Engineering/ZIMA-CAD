#include <zima/document/opening_component_state.hpp>
#include <zima/workspace/opening_component_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <algorithm>
namespace zima::workspace {
std::vector<OpeningComponent> opening_components(const document::HistoryContainer& value) {
    using Kind=document::FeatureKind;
    if(value.feature_kind==Kind::Thread) {
        std::vector<OpeningComponent> rows{{"bore",false}};
        if(value.thread.enabled)rows.push_back({"thread",true});
        if(value.thread.chamfer_enabled)rows.push_back({"chamfer",true});
        if(value.hole.drill_point_enabled&&value.thread.end_condition_forward==document::EndCondition::Length)rows.push_back({"tip",true});
        return rows;
    }
    if(value.feature_kind!=Kind::Hole)throw OpeningOperationError("wrong_feature","This container is not an opening.");
    std::vector<OpeningComponent> rows{{"bore-plane",false},{"bore-sketch",false},{"bore",false}};
    if(value.hole.entrance_chamfer>0)for(const char* role:{"chamfer-plane","chamfer-sketch","chamfer"})rows.push_back({role,false});
    if(value.hole.drill_point_enabled||value.hole.exit_chamfer_enabled)
        for(const char* role:{"tip-plane","tip-sketch","tip"})rows.push_back({role,false});
    if(value.hole.thread_enabled)rows.push_back({"thread",true});
    return rows;
}
bool remove_opening_component(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    const std::string& container,const std::string& role) {
    auto* state=live.open_part(id);if(!state)throw OpeningOperationError("unsupported_document","Opening operations require an open Part.");
    const auto& before=state->session.document();const auto* existing=before.find_container(container);
    if(!existing)throw OpeningOperationError("container_not_found","The requested container does not exist.");
    static_cast<void>(opening_components(*existing));
    const auto* body=before.body_owner_for_object(container);
    if(body&&body->derived_copy)throw OpeningOperationError("read_only_body","A derived Body cannot be edited directly.");
    if(body&&body->scope.id!=before.body_history.active_body_id())throw OpeningOperationError("inactive_body","Activate the owning Body before editing its opening.");
    const bool native=existing->feature_kind==document::FeatureKind::Hole;
    if(role!="thread"&&!(!native&&(role=="chamfer"||role=="tip")))
        throw OpeningOperationError("component_not_removable","This opening component cannot be removed separately.");
    auto value=*existing;
    if(!document::disable_opening_component(value,role))return false;
    if(native) {
        auto next=before;*next.find_container(container)=std::move(value);
        commit_part_document(live,id,std::move(next),state->session.calculated_boundaries());return true;
    }
    return commit_opening(live,kernel,id,std::move(value),OpeningEditMode::Replace);
}
}

#include <zima/workspace/reference_sources.hpp>

namespace zima::workspace {
std::string ReferenceFrame::path(const std::string& local) const {
    if(instance_prefix.empty())return local;
    if(local.empty())return instance_prefix;
    return instance_prefix+local;
}
void visit_original_references(const Workspace& live,const std::string& id,const ReferenceVisitor& visit) {
    if(const auto* state=live.open_part(id)) {
        const auto& document=state->session.document();const auto& calculated=state->session.calculated_boundaries();
        const ReferenceFrame frame;
        if(!calculated.empty() && !visit(calculated.back().mesh.original_references,frame))return;
        if(!visit(document.origin_viewer_mesh().original_references,frame))return;
        if(!visit(document.body_origin_reference_geometry(),frame))return;
        if(!visit(document.history_origin_reference_geometry_before({}),frame))return;
        static_cast<void>(visit(document.construction_viewer_mesh().original_references,frame));
        return;
    }
    if(const auto* state=live.open_assembly(id)) {
        const auto& document=state->session.document();const ReferenceFrame root;
        if(!visit(document.origin_viewer_mesh().original_references,root))return;
        if(!visit(document.construction_viewer_mesh().original_references,root))return;
        const auto suppressed=document.effectively_suppressed_occurrences();
        for(const auto& component:document.components) {
            const auto path=assembly::InstancePath{}.child(component.occurrence_id);
            ReferenceFrame frame;frame.instance_prefix=path.encoded();frame.visible=component.visible;
            frame.suppressed=suppressed.contains(component.occurrence_id);
            frame.point=[&live,&id,path](auto p){return live.occurrence_point_to_scene(id,path,p);};
            frame.direction=[&live,&id,path](auto p){return live.occurrence_direction_to_scene(id,path,p);};
            frame.surface_point=[&live,&id,prefix=frame.instance_prefix](const std::string& local,auto p){
                return live.occurrence_point_to_scene(id,assembly::InstancePath::decode(prefix+local),p);
            };
            frame.surface_direction=[&live,&id,prefix=frame.instance_prefix](const std::string& local,auto p){
                return live.occurrence_direction_to_scene(id,assembly::InstancePath::decode(prefix+local),p);
            };
            if(!visit(component.calculated_source->mesh.original_references,frame))return;
            if(component.source_kind==assembly::ComponentSourceKind::Part) {
                document::PartDocument origin;origin.document_id=component.source_document_id;
                if(!visit(origin.origin_viewer_mesh().original_references,frame))return;
            }
        }
        return;
    }
    throw ReferenceQueryError("unsupported_document","Reference queries require an open Part or Assembly.");
}
}

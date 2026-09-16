#include <zima/workspace/reference_sources.hpp>
#include <map>
#include <tuple>

namespace zima::workspace {
std::string ReferenceFrame::path(const std::string& local) const {
    if(instance_prefix.empty())return local;
    if(local.empty())return instance_prefix;
    return instance_prefix+local;
}
void append_original_reference_geometry(kernel::ViewerReferenceGeometry& target,
    const kernel::ViewerReferenceGeometry& source,const ReferenceFrame& frame,
    const OriginalReferenceFilter& accept) {
    const auto matches=[&](OriginalReferenceKind kind,const auto& reference) {
        return accept(kind,reference.owner_id,reference.semantic_key,frame.path(reference.instance_path));
    };
    for(const auto& edge:source.edges)if(matches(OriginalReferenceKind::Edge,edge.reference)) {
        auto copy=edge;copy.reference.instance_path=frame.path(edge.reference.instance_path);
        for(auto& point:copy.points)point=frame.point(point);
        if(copy.exact_spline)for(auto& point:copy.exact_spline->poles)point=frame.point(point);
        for(auto& side:copy.edge_treatment_side_directions)for(auto& direction:side)direction=frame.direction(direction);
        target.edges.push_back(std::move(copy));
    }
    for(const auto& point:source.points)if(matches(OriginalReferenceKind::Point,point.reference)) {
        auto copy=point;copy.reference.instance_path=frame.path(point.reference.instance_path);
        copy.position=frame.point(point.position);target.points.push_back(std::move(copy));
    }
    for(const auto& axis:source.axes)if(matches(OriginalReferenceKind::Axis,axis.reference)) {
        auto copy=axis;copy.reference.instance_path=frame.path(axis.reference.instance_path);
        copy.point=frame.point(axis.point);copy.direction=frame.direction(axis.direction);target.axes.push_back(std::move(copy));
    }
    std::map<std::uint32_t,std::uint32_t> vertices;
    std::map<std::tuple<std::string,std::string,std::string>,std::shared_ptr<const kernel::SurfaceGeometry>> surfaces;
    for(std::size_t triangle=0;triangle<source.triangle_references.size();++triangle) {
        const auto& face=source.triangle_references[triangle];if(!matches(OriginalReferenceKind::Face,face))continue;
        if(triangle*3+2>=source.triangles.size())throw ReferenceQueryError("invalid_reference_geometry","The persisted reference geometry is incomplete.");
        auto copy=face;copy.instance_path=frame.path(face.instance_path);
        if(face.surface) {
            auto [entry,inserted]=surfaces.try_emplace(std::make_tuple(face.owner_id,face.semantic_key,face.instance_path));
            if(inserted) {
                auto surface=*face.surface;surface.origin=frame.surface_point(face.instance_path,surface.origin);
                surface.axis=frame.surface_direction(face.instance_path,surface.axis);surface.radial=frame.surface_direction(face.instance_path,surface.radial);
                entry->second=std::make_shared<const kernel::SurfaceGeometry>(std::move(surface));
            }
            copy.surface=entry->second;
        }
        target.triangle_references.push_back(std::move(copy));
        for(std::size_t corner=0;corner<3;++corner) {
            const auto index=source.triangles[triangle*3+corner];
            if(index>=source.vertices.size())throw ReferenceQueryError("invalid_reference_geometry","The persisted reference geometry is incomplete.");
            auto [entry,inserted]=vertices.try_emplace(index,static_cast<std::uint32_t>(target.vertices.size()));
            if(inserted)target.vertices.push_back(frame.point(source.vertices[index]));
            target.triangles.push_back(entry->second);
        }
    }
}

void visit_original_references(const Workspace& live,const std::string& id,const ReferenceVisitor& visit) {
    if(const auto* state=live.open_part(id)) {
        const auto& document=state->session.document();const auto& calculated=state->session.calculated_boundaries();
        const ReferenceFrame frame;
        if(!calculated.empty() && !visit(calculated.back().mesh.original_references,frame))return;
        if(!visit(document.origin_viewer_mesh().original_references,frame))return;
        if(!visit(document.body_origin_reference_geometry(),frame))return;
        if(!visit(document.history_origin_reference_geometry_before({}),frame))return;
        if(!visit(document.sketch_placement_reference_geometry(),frame))return;
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

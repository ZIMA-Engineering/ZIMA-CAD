#include <zima/document/sheet_transition.hpp>
#include <transition_sketches.hpp>
#include <transition_sheet.hpp>
#include <algorithm>
namespace zima::document {
namespace {
using namespace kernel::sheet_material;
research::transition::Frame frame(kernel::Vec3 origin,kernel::Vec3 rotation) {
    return {origin,construction_direction_from_local_axis("x",rotation),construction_direction_from_local_axis("y",rotation),construction_direction_from_local_axis("z",rotation)};
}
}
ContainerOrigin sheet_transition_end_origin(const HistoryContainer& feature) {
    auto result=create_container_origin(feature.id+":transition-end");
    result.parent_id=feature.container_origin.id;
    return result;
}
HistoryContainer create_sheet_transition() {
    auto feature=PartDocument::create_sketch_container();feature.feature_kind=FeatureKind::SheetTransition;feature.name="Přechod plechu";
    feature.sheet_transition.end_origin_id=sheet_transition_end_origin(feature).id;
    auto round=sketcher::Sketch::create_default(),rectangle=sketcher::Sketch::create_default();
    round.name="Skica půlkruhu";rectangle.name="Skica zaobleného půlobdélníku";
    static_cast<void>(round.add_arc(0,0,80,0,-80,0));
    const auto sides=rectangle.add_rectangle(-100,0,100,80);
    std::ranges::find(rectangle.segments,sides[0],&sketcher::SketchSegment::id)->construction=true;
    const auto right=rectangle.add_corner_fillet(sides[1],sides[2],20).arc_id;
    const auto left=rectangle.add_corner_fillet(sides[2],sides[3],20).arc_id;
    static_cast<void>(rectangle.add_equal_radius_constraint(right,left));
    round.apply_dimension(round.create_arc_radius_dimension(round.arcs.front().id));
    rectangle.apply_dimension(rectangle.create_segment_dimension(sides[0]));
    const auto& vertical=*std::ranges::find(rectangle.segments,sides[1],&sketcher::SketchSegment::id);
    rectangle.apply_dimension(rectangle.create_point_dimension(vertical.first_point_id,vertical.second_point_id,sketcher::DimensionKind::DistanceY));
    std::ranges::find(rectangle.corner_radii,right,&sketcher::SketchCornerRadius::id)->dimension_visible=true;
    round.plane_reference_owner_id=feature.sheet_transition.end_origin_id;
    rectangle.plane_reference_owner_id=feature.container_origin.id;
    round.owner_container_id=rectangle.owner_container_id=feature.id;
    feature.sheet_transition.sketches={round.serialized(),rectangle.serialized()};reframe_sheet_transition(feature);return feature;
}
void reframe_sheet_transition(HistoryContainer& feature) {
    const auto& p=feature.placement;const auto root=frame({p.x,p.y,p.z},{p.rotation_x,p.rotation_y,p.rotation_z});
    const auto relative=frame(feature.sheet_transition.end_position,feature.sheet_transition.end_rotation);
    if(!root.valid()||!relative.valid()||feature.sheet_transition.end_origin_id!=sheet_transition_end_origin(feature).id)
        throw std::invalid_argument("Invalid transition sheet parameters");
    std::set<std::string> ids;
    for(unsigned i=0;i<2;++i) {
        auto sketch=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[i]);
        if(sketch.owner_container_id!=feature.id||!ids.insert(sketch.id).second)throw std::invalid_argument("Sheet transition editing must preserve feature identity.");
        const bool at_end=sketch.plane_reference_owner_id==feature.sheet_transition.end_origin_id;
        if(!at_end&&sketch.plane_reference_owner_id!=feature.container_origin.id)throw std::invalid_argument("Invalid transition sheet parameters");
        const auto at=at_end?research::transition::Frame{root.point(relative.origin),root.direction(relative.x),root.direction(relative.y),root.direction(relative.z)}:root;
        sketch.plane=sketcher::SketchPlane::XY;sketch.plane_offset=0;
        sketch.resolved_origin=at.origin;sketch.resolved_x_axis=at.x;sketch.resolved_y_axis=at.y;sketch.resolved_normal=at.z;
        feature.sheet_transition.sketches[i]=sketch.serialized();
    }
}
kernel::ViewerReferenceGeometry sheet_transition_end_references(const HistoryContainer& source) {
    auto feature=source;reframe_sheet_transition(feature);
    PartDocument carrier;carrier.document_id=feature.id+":transition-end";
    auto result=carrier.origin_viewer_mesh().original_references;
    const auto selected=std::ranges::find_if(feature.sheet_transition.sketches,[&](const auto& data){return sketcher::Sketch::from_serialized(data).plane_reference_owner_id==feature.sheet_transition.end_origin_id;});
    if(selected==feature.sheet_transition.sketches.end())throw std::invalid_argument("Invalid transition sheet parameters");
    const auto s=sketcher::Sketch::from_serialized(*selected);
    const auto vector=[&](kernel::Vec3 p){return add(add(mul(s.resolved_x_axis,p.x),mul(s.resolved_y_axis,p.y)),mul(s.resolved_normal,p.z));};
    const auto point=[&](kernel::Vec3 p){return add(s.resolved_origin,vector(p));};
    for(auto& p:result.vertices)p=point(p);
    for(auto& e:result.edges)for(auto& p:e.points)p=point(p);
    for(auto& p:result.points)p.position=point(p.position);
    for(auto& axis:result.axes){axis.point=point(axis.point);axis.direction=vector(axis.direction);}
    return result;
}
kernel::ViewerMesh sheet_transition_preview(const HistoryContainer& source) {
    auto feature=source;reframe_sheet_transition(feature);kernel::ViewerMesh result;
    std::array<sketcher::Sketch,2> sketches;
    for(unsigned i=0;i<2;++i) {
        sketches[i]=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[i]);
        const auto mesh=sketches[i].viewer_mesh();result.edges.insert(result.edges.end(),mesh.edges.begin(),mesh.edges.end());
        const auto& s=sketches[i];const auto origin=s.plane_reference_owner_id;
        for(const auto& [axis,direction]:std::array<std::pair<const char*,kernel::Vec3>,3>{{{"x",s.resolved_x_axis},{"y",s.resolved_y_axis},{"z",s.resolved_normal}}}) {
            kernel::ViewerEdge edge;edge.points={s.resolved_origin,add(s.resolved_origin,mul(direction,20))};edge.reference={origin,std::string("origin:axis:")+axis,{}};result.edges.push_back(std::move(edge));
        }
    }
    // Disposable ZIMA surface edges only; no kernel calculation during placement.
    try {
        auto input=research::transition::read_sketches(sketches[0],sketches[1]);input.model.corner_facets={feature.sheet_transition.facets[0],feature.sheet_transition.facets[1]};
        auto surface=research::transition::mesh(research::transition::calculate(input.model));
        result.edges.insert(result.edges.end(),surface.edges.begin(),surface.edges.end());
    }catch(const std::exception&){}
    return result;
}
kernel::HistoryOperation sheet_transition_operation(const PartDocument&,const HistoryContainer& feature) {
    auto framed=feature;reframe_sheet_transition(framed);const auto& parameters=framed.sheet_transition;
    if(feature.combine_mode!=CombineMode::Add)throw std::invalid_argument("Invalid transition sheet parameters");
    auto input=research::transition::read_sketches(sketcher::Sketch::from_serialized(parameters.sketches[0]),sketcher::Sketch::from_serialized(parameters.sketches[1]));input.model.corner_facets={parameters.facets[0],parameters.facets[1]};
    auto operation=research::transition::sheet_operation(research::transition::manufacture(input.model,
        {parameters.thickness,parameters.inside_radius,parameters.k_factor}),feature.id);
    // Embed source ancestry in all generated profile keys, before OCCT runs.
    // The transition is jointly driven by both profiles. Store the complete
    // authored curve ancestry, not just the two Sketch labels. Length prefixes
    // make identifiers with embedded separators unambiguous.
    std::set<std::string> parents;
    parents.insert(input.arc_parents.begin(),input.arc_parents.end());
    parents.insert(input.corner_parents.begin(),input.corner_parents.end());
    parents.insert(input.straight_parents.begin(),input.straight_parents.end());
    std::string ancestry=":parents:"+std::to_string(parents.size());
    for(const auto& parent:parents)ancestry+=":"+std::to_string(parent.size())+":"+parent;
    const auto remap=[&](std::string& id){if(!id.empty())id="transition:"+feature.feature_id+":"+id+ancestry;};
    auto& group=std::get<kernel::FeatureGroupRequest>(operation.primitive);
    for(auto& child:group.children)std::visit([&](auto& primitive){
        using T=std::decay_t<decltype(primitive)>;
        if constexpr(std::is_same_v<T,kernel::ExtrusionRequest>) {
            remap(primitive.profile_region_id);remap(primitive.outer_boundary_id);
            for(auto& id:primitive.outer_edge_source_ids)remap(id);for(auto& id:primitive.outer_vertex_source_ids)remap(id);
        } else if constexpr(std::is_same_v<T,kernel::Sweep3DRequest>) {
            for(auto& id:primitive.path_point_ids)remap(id);
            std::set<std::string> canonical;for(auto id:primitive.canonical_station_ids){remap(id);canonical.insert(id);}primitive.canonical_station_ids=std::move(canonical);
            for(auto& segment:primitive.path_segments)remap(segment.source_id);
            for(auto& section:primitive.sections){remap(section.profile_id);remap(section.point_id);remap(section.profile.region_id);remap(section.profile.outer_boundary_id);for(auto& id:section.profile.outer_edge_source_ids)remap(id);for(auto& id:section.profile.outer_vertex_source_ids)remap(id);}
        }
    },child);
    for(auto& region:operation.sheet_regions)remap(region.curved_source_id);
    operation.suppressed=feature.suppressed;return operation;
}
}

#include <zima/document/sheet_transition.hpp>
#include <transition_sketches.hpp>
#include <transition_sheet.hpp>
#include <zima/document/container_origin_display.hpp>
#include <algorithm>
namespace zima::document {
namespace {
using namespace kernel::sheet_material;
research::transition::Frame frame(kernel::Vec3 origin,kernel::Vec3 rotation) {
    return {origin,construction_direction_from_local_axis("x",rotation),construction_direction_from_local_axis("y",rotation),construction_direction_from_local_axis("z",rotation)};
}
std::string bend_key(const HistoryContainer& feature,std::size_t index) {
    const auto& p=feature.sheet_transition;
    return feature.feature_id+":facets:"+std::to_string(p.facets[0])+":"+std::to_string(p.facets[1])+":authored-bend:"+std::to_string(index);
}
research::transition::SheetOptions marking_options(const HistoryContainer& feature) {
    const auto& p=feature.sheet_transition;
    if(!(std::isfinite(p.end_notch_depth)&&p.end_notch_depth>0&&std::isfinite(p.bend_axis_end_length)&&p.bend_axis_end_length>0&&
         std::isfinite(p.rectangle_relief_depth)&&p.rectangle_relief_depth>0))throw std::invalid_argument("Invalid transition sheet parameters");
    research::transition::SheetOptions result{p.thickness,p.inside_radius,p.k_factor};
    if(p.end_notches)result.end_notch_depth=p.end_notch_depth;
    if(p.rectangle_reliefs) {
        result.rectangle_relief_depth=p.rectangle_relief_depth;
        const auto keys=sheet_transition_bend_keys(feature);
        for(const auto& selected:p.relieved_bends) {
            const auto found=std::ranges::find(keys,selected);
            if(found==keys.end())throw std::invalid_argument("Invalid transition sheet parameters");
            result.rectangle_relief_bends.insert(static_cast<std::size_t>(found-keys.begin()));
        }
    }
    return result;
}
kernel::ViewerMesh transition_material_wire(const research::transition::SheetResult& sheet,bool labels) {
    kernel::ViewerMesh result;
    const auto line=[&](kernel::Vec3 a,kernel::Vec3 b) {
        kernel::ViewerEdge edge;edge.points={a,b};result.edges.push_back(std::move(edge));
    };
    const auto skin_line=[&](kernel::Vec3 a,kernel::Vec3 b,const research::transition::SheetPanel& panel,bool inner) {
        const auto delta=sub(b,a);const double squared=dot(delta,delta);
        if(squared<1e-18)return;
        std::vector<std::pair<double,double>> covered;
        const auto cover=[&](kernel::Vec3 c,kernel::Vec3 d) {
            const auto ac=sub(c,a),ad=sub(d,a);
            const double u=dot(ac,delta)/squared,v=dot(ad,delta)/squared;
            const auto off_c=sub(ac,mul(delta,u)),off_d=sub(ad,mul(delta,v));
            if(dot(off_c,off_c)>1e-12||dot(off_d,off_d)>1e-12)return;
            const double low=std::max(0.,std::min(u,v)),high=std::min(1.,std::max(u,v));
            if(high>low)covered.emplace_back(low,high);
        };
        for(const auto& bend:sheet.bends)for(const auto* section:{&bend.sections.front(),&bend.sections.back()})
            cover((*section)[inner?3:0],(*section)[inner?2:1]);
        for(const auto& other:sheet.panels) {
            if(&other==&panel||dot(other.inward,panel.inward)<1-1e-9)continue;
            const auto shift=mul(other.inward,inner?sheet.thickness:0);
            for(std::size_t i=0;i<other.outer.size();++i)
                cover(add(other.outer[i],shift),add(other.outer[(i+1)%other.outer.size()],shift));
        }
        std::ranges::sort(covered);double start=0;
        for(const auto [low,high]:covered) {
            if(low>start+1e-9)line(add(a,mul(delta,start)),add(a,mul(delta,low)));
            start=std::max(start,high);
        }
        if(start<1-1e-9)line(add(a,mul(delta,start)),b);
    };
    for(const auto& panel:sheet.panels) {
        const auto thickness=mul(panel.inward,sheet.thickness);
        for(std::size_t i=0;i<panel.outer.size();++i) {
            const auto a=panel.outer[i],b=panel.outer[(i+1)%panel.outer.size()];
            skin_line(a,b,panel,false);skin_line(add(a,thickness),add(b,thickness),panel,true);line(a,add(a,thickness));
        }
    }
    for(const auto& bend:sheet.bends) {
        if(bend.sections.empty())continue;
        if(labels)result.constraint_markers.push_back({bend.sections[bend.sections.size()/2][1],std::to_string(&bend-sheet.bends.data()+1),{}, {}});
        for(std::size_t corner=0;corner<4;++corner) {
            kernel::ViewerEdge edge;
            for(const auto& section:bend.sections)edge.points.push_back(section[corner]);
            result.edges.push_back(std::move(edge));
            if(corner==1||corner==3)for(const auto* section:{&bend.sections.front(),&bend.sections.back()})
                line((*section)[corner],(*section)[(corner+1)%4]);
        }
    }
    return result;
}
}
ContainerOrigin sheet_transition_end_origin(const HistoryContainer& feature) {
    auto result=create_container_origin(feature.id+":transition-end");
    result.parent_id=feature.container_origin.id;
    return result;
}
bool rectangular_sheet_transition(const HistoryContainer& feature) {
    const auto sketch=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[0]);
    return sketch.arcs.empty()&&sketch.segments.size()==4;
}
bool has_sheet_transition(const PartDocument& document) {
    return std::ranges::any_of(document.history,[](const auto& feature) {
        return feature.feature_kind==FeatureKind::SheetTransition;
    });
}
void set_rectangular_transition_sides(HistoryContainer& feature,unsigned sides) {
    if(!rectangular_sheet_transition(feature)||(sides!=2&&sides!=3))
        throw std::invalid_argument("Transition requires two rectangular profiles with two or three adjacent sides.");
    static_cast<void>(research::transition::read_rectangular_sketches(
        sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[1]),
        sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[0])));
    auto pending=feature.sheet_transition.sketches;
    for(auto& data:pending) {
        auto sketch=sketcher::Sketch::from_serialized(data);
        double xmin=1e100;
        for(const auto& line:sketch.segments)for(const auto& id:{line.first_point_id,line.second_point_id})
            xmin=std::min(xmin,sketch.find_point(id)->x);
        for(auto& line:sketch.segments) {
            const auto* a=sketch.find_point(line.first_point_id);const auto* b=sketch.find_point(line.second_point_id);
            if(std::abs(a->x-xmin)<1e-7&&std::abs(b->x-xmin)<1e-7)line.construction=sides==2;
        }
        data=sketch.serialized();
    }
    feature.sheet_transition.sketches=std::move(pending);
}
HistoryContainer create_sheet_transition(bool rectangular) {
    auto feature=PartDocument::create_sketch_container();feature.feature_kind=FeatureKind::SheetTransition;feature.name="Přechod plechu";
    feature.sheet_transition.end_origin_id=sheet_transition_end_origin(feature).id;
    auto round=sketcher::Sketch::create_default(),rectangle=sketcher::Sketch::create_default();
    if(rectangular) {
        feature.name="Obdélníkový přechod plechu";
        for(unsigned end=0;end<2;++end) {
            auto& sketch=end?round:rectangle;const double width=end?140:200,depth=end?100:160;
            sketch.name=end?"Skica druhého obdélníku":"Skica prvního obdélníku";
            const auto sides=sketch.add_rectangle(-width/2,-depth/2,width/2,depth/2);
            std::ranges::find(sketch.segments,sides[0],&sketcher::SketchSegment::id)->construction=true;
            sketch.apply_dimension(sketch.create_segment_dimension(sides[0]));
            sketch.apply_dimension(sketch.create_segment_dimension(sides[1]));
            sketch.plane_reference_owner_id=end?feature.sheet_transition.end_origin_id:feature.container_origin.id;
            sketch.owner_container_id=feature.id;
        }
        feature.sheet_transition.sketches={round.serialized(),rectangle.serialized()};
        reframe_sheet_transition(feature);return feature;
    }
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
    const auto plane=sheet_transition_profile_plane(feature);
    const auto offset=static_cast<std::uint32_t>(result.vertices.size());
    result.vertices.insert(result.vertices.end(),plane.vertices.begin(),plane.vertices.end());
    for(auto index:plane.triangles)result.triangles.push_back(offset+index);
    result.triangle_references.insert(result.triangle_references.end(),plane.triangle_references.begin(),plane.triangle_references.end());
    return result;
}
kernel::ViewerReferenceGeometry sheet_transition_profile_plane(const HistoryContainer& source) {
    auto feature=source;reframe_sheet_transition(feature);
    const auto& data=feature.sheet_transition.sketches[0];
    const auto sketch=sketcher::Sketch::from_serialized(data);
    const auto center=sheet_transition_axis_points(feature)[1].position;
    kernel::ViewerReferenceGeometry result;
    for(const auto [x,y]:std::array<std::pair<double,double>,4>{{{-20,-20},{20,-20},{20,20},{-20,20}}})
        result.vertices.push_back(add(center,add(mul(sketch.resolved_x_axis,x),mul(sketch.resolved_y_axis,y))));
    result.triangles={0,1,2,0,2,3};
    result.triangle_references.assign(2,{feature.id,"plane:end",{}});
    return result;
}
std::array<kernel::ViewerPoint,2> sheet_transition_axis_points(const HistoryContainer& source,bool editing) {
    auto feature=source;reframe_sheet_transition(feature);
    const auto first=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[1]);
    const auto second=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[0]);
    kernel::Vec3 start,end;
    if(rectangular_sheet_transition(feature)) {
        const auto model=research::transition::read_rectangular_sketches(first,second).model;
        start=model.first_origin.origin;end=model.first_origin.point(model.second_relative.origin);
    }else {
        const auto model=research::transition::read_sketches(second,first).model;
        start=model.first_origin.point(model.second_relative.origin);end=model.first_origin.origin;
    }
    auto a=container_origin_marker(feature,editing),b=a;
    a.position=start;b.position=end;
    a.reference.semantic_key="axis:start";b.reference.semantic_key="axis:end";
    b.label.clear(); // The feature name labels the pair only once.
    return {a,b};
}
std::vector<std::string> sheet_transition_bend_keys(const HistoryContainer& source) {
    auto feature=source;reframe_sheet_transition(feature);const auto& p=feature.sheet_transition;
    const auto a=sketcher::Sketch::from_serialized(p.sketches[0]),b=sketcher::Sketch::from_serialized(p.sketches[1]);
    const research::transition::SheetOptions options{p.thickness,p.inside_radius,p.k_factor};
    const auto material=[&]{
        if(rectangular_sheet_transition(feature))return research::transition::manufacture(research::transition::read_rectangular_sketches(b,a).model,options);
        auto input=research::transition::read_sketches(a,b);input.model.corner_facets={p.facets[0],p.facets[1]};
        return research::transition::manufacture(input.model,options);
    }();
    std::vector<std::string> result;
    for(std::size_t i=0;i<material.bends.size();++i)result.push_back(bend_key(feature,material.bends[i].boundary_index));
    return result;
}
kernel::ViewerMesh sheet_transition_preview(const HistoryContainer& source) {
    auto feature=source;reframe_sheet_transition(feature);kernel::ViewerMesh result;
    std::array<sketcher::Sketch,2> sketches;
    for(unsigned i=0;i<2;++i) {
        sketches[i]=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[i]);
        auto mesh=sketches[i].viewer_mesh();
        std::erase_if(mesh.edges,[](const auto& edge){return edge.construction;});
        result.edges.insert(result.edges.end(),mesh.edges.begin(),mesh.edges.end());
        const auto& s=sketches[i];const auto origin=s.plane_reference_owner_id;
        for(const auto& [axis,direction]:std::array<std::pair<const char*,kernel::Vec3>,3>{{{"x",s.resolved_x_axis},{"y",s.resolved_y_axis},{"z",s.resolved_normal}}}) {
            kernel::ViewerEdge edge;edge.points={s.resolved_origin,add(s.resolved_origin,mul(direction,20))};edge.reference={origin,std::string("origin:axis:")+axis,{}};result.edges.push_back(std::move(edge));
        }
    }
    // Disposable ZIMA wire only; no kernel calculation during placement.
    try {
        kernel::ViewerMesh surface;
        const auto options=marking_options(feature);
        if(rectangular_sheet_transition(feature)) {
            const auto input=research::transition::read_rectangular_sketches(sketches[1],sketches[0]);
            surface=transition_material_wire(research::transition::manufacture(input.model,options),feature.sheet_transition.rectangle_reliefs);
        }else {
            auto input=research::transition::read_sketches(sketches[0],sketches[1]);input.model.corner_facets={feature.sheet_transition.facets[0],feature.sheet_transition.facets[1]};
            surface=transition_material_wire(research::transition::manufacture(input.model,options),feature.sheet_transition.rectangle_reliefs);
        }
        result.edges.insert(result.edges.end(),surface.edges.begin(),surface.edges.end());
        result.constraint_markers=std::move(surface.constraint_markers);
    }catch(const std::exception&){throw;}
    return result;
}
kernel::HistoryOperation sheet_transition_operation(const PartDocument&,const HistoryContainer& feature) {
    auto framed=feature;reframe_sheet_transition(framed);const auto& parameters=framed.sheet_transition;
    if(feature.combine_mode!=CombineMode::Add)throw std::invalid_argument("Invalid transition sheet parameters");
    const auto first=sketcher::Sketch::from_serialized(parameters.sketches[1]),second=sketcher::Sketch::from_serialized(parameters.sketches[0]);
    kernel::HistoryOperation operation;std::set<std::string> parents;kernel::Vec3 round_center,rectangle_center;
    const auto options=marking_options(framed);
    if(rectangular_sheet_transition(framed)) {
        const auto input=research::transition::read_rectangular_sketches(first,second);
        operation=research::transition::sheet_operation(research::transition::manufacture(input.model,options),feature.id);
        parents.insert(input.parents.begin(),input.parents.end());
        rectangle_center=input.model.first_origin.origin;round_center=input.model.first_origin.point(input.model.second_relative.origin);
    }else {
        auto input=research::transition::read_sketches(second,first);input.model.corner_facets={parameters.facets[0],parameters.facets[1]};
        operation=research::transition::sheet_operation(research::transition::manufacture(input.model,options),feature.id);
        parents.insert(input.arc_parents.begin(),input.arc_parents.end());
        parents.insert(input.corner_parents.begin(),input.corner_parents.end());
        parents.insert(input.straight_parents.begin(),input.straight_parents.end());
        round_center=input.model.first_origin.origin;rectangle_center=input.model.first_origin.point(input.model.second_relative.origin);
    }
    // Embed source ancestry in all generated profile keys, before OCCT runs.
    // The transition is jointly driven by both profiles. Store the complete
    // authored curve ancestry, not just the two Sketch labels. Length prefixes
    // make identifiers with embedded separators unambiguous.
    std::string ancestry=":parents:"+std::to_string(parents.size());
    for(const auto& parent:parents)ancestry+=":"+std::to_string(parent.size())+":"+parent;
    const auto remap=[&](std::string& id){if(!id.empty())id="transition:"+feature.feature_id+":"+id+ancestry;};
    auto& group=std::get<kernel::FeatureGroupRequest>(operation.primitive);
    if(parameters.short_bend_axes)group.bend_line_end_length=parameters.bend_axis_end_length;
    const auto end_plane=sheet_transition_profile_plane(framed);
    group.reference_planes.push_back({end_plane.triangle_references.front(),
        {end_plane.vertices[0],end_plane.vertices[1],end_plane.vertices[2],end_plane.vertices[3]}});
    for(auto point:sheet_transition_axis_points(framed)) {
        point.label.clear();point.always_visible=false;
        group.reference_points.push_back(std::move(point));
    }
    // Profile centres come from authored Sketch geometry, not result topology.
    // Use the existing primary-axis identity and persisted reference pipeline.
    const auto delta=sub(round_center,rectangle_center);
    const double axis_length=std::sqrt(dot(delta,delta));
    if(axis_length>1e-9)
        group.axes.push_back({mul(add(round_center,rectangle_center),.5),
            mul(delta,1.0/axis_length),axis_length+2.0,{feature.id,"axis:primary",{}}});
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

#include <zima/workspace/mech_keyway_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/modeling_confirmation.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/sheet_profile_operations.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/document/hole_profiles.hpp>
#include <zima/document/keyway_catalog.hpp>
#include <zima/document/profile_targets.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/loft.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <zima/kernel/wrapped_image.hpp>
#include <zima/sketcher/face_curve_reference.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace zima::workspace {
namespace {
using V=kernel::Vec3;
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V a,double k){return {a.x*k,a.y*k,a.z*k};}
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
std::optional<kernel::FaceReference> woodruff_floor_reference(
        const document::HistoryContainer& cut,const kernel::FaceReference& face,
        const kernel::ViewerReferenceGeometry& refs) {
    if(!face.surface||face.surface->kind!=kernel::SurfaceGeometry::Kind::Cylinder)return {};
    const kernel::FaceReference authored{cut.id,"generated:"+kernel::feature_side_parent_key(
        {cut.feature_id,kernel::FeatureSide::End,cut.id+":profile:cutter"}),{}};
    if(!kernel::wrapped_image_face_matches(face,authored))return {};
    // The Boolean can split the cutter floor at its seam. Anchor the exact
    // authored End-side surface only when one of its real descendants survives.
    const auto original=std::ranges::find(refs.triangle_references,authored);
    if(original==refs.triangle_references.end()||!original->surface)return {};
    return *original;
}
V rotation_for_axes(V x,V y,V z) {
    const double ry=std::asin(std::clamp(-x.z,-1.,1.));
    const double rx=std::abs(std::cos(ry))>1e-9?std::atan2(y.z,z.z):0.;
    const double rz=std::abs(std::cos(ry))>1e-9?std::atan2(x.y,x.x):std::atan2(-y.x,y.y);
    return {rx*180/std::numbers::pi,ry*180/std::numbers::pi,rz*180/std::numbers::pi};
}
bool hub_face_side(const document::HistoryContainer& cut,V point,V axis,const kernel::FaceReference& face) {
    if(cut.feature_kind==document::FeatureKind::Loft)return true;
    constexpr std::string_view prefix="generated:";
    if(!face.semantic_key.starts_with(prefix))return false;
    const auto side=kernel::feature_side_parent(std::string_view(face.semantic_key).substr(prefix.size()));
    const double station=dot(sub(point,V{cut.placement.x,cut.placement.y,cut.placement.z}),axis);
    const auto wanted=station< -1e-8||cut.feature.sides[0].operation==document::FeatureSideOperation::None?
        kernel::FeatureSide::Start:kernel::FeatureSide::End;
    return side&&side->side==wanted;
}

document::Placement keyway_profile_placement(const document::HistoryContainer& parent,
        const kernel::SurfaceGeometry& surface,const document::KeywayFrame& frame,double reference,
        const kernel::ViewerReferenceGeometry& refs,bool transverse) {
    auto placement=parent.placement;
    const double radius=surface.radius+reference*frame.slope;
    const auto origin=add(add(surface.origin,mul(frame.axis,reference)),mul(frame.radial,radius));
    placement.x=origin.x;placement.y=origin.y;placement.z=origin.z;
    placement.orientation_back=false;placement.orientation_quarter_turns=0;
    placement.rotation_offset_x=placement.rotation_offset_y=placement.rotation_offset_z=0;
    std::erase_if(placement.references,[](const auto& r){return r.orientation_only;});
    bool surface_position=false;
    for(auto& r:placement.references) {
        r.orientation_drives_rotation=false;r.orientation_role=document::placement_position_only_role;
        if(r.owner_id==parent.mech_keyway.support.owner_id&&r.semantic_key==parent.mech_keyway.support.semantic_key) {
            r.use_axis=false;surface_position=true;
        }
    }
    document::ConstructionReference contact;
    contact.owner_id=parent.mech_keyway.support.owner_id;
    contact.semantic_key=parent.mech_keyway.support.semantic_key;
    contact.instance_path=parent.mech_keyway.support.instance_path;
    contact.supports_offset=true;contact.orientation_role=document::placement_position_only_role;
    if(!surface_position)placement.references.insert(placement.references.begin(),contact);
    // Keep the parent's authored positional supports and measure their exact
    // offsets at the generated child profile's station.
    for(auto& r:placement.references)if(r.supports_offset) {
        const auto value=document::measure_placement_reference_value(r,placement.references,refs,origin);
        if(!value)throw std::invalid_argument("Keyway orientation reference is unavailable");
        r.offset=*value;
    }
    bool front=false;
    for(auto& r:placement.references)if(!r.use_angle&&r.semantic_key!=contact.semantic_key) {
        document::ExtrusionParameters::EndTarget target;
        target.reference={r.owner_id,r.semantic_key,r.instance_path};
        target.kind=document::profile_target_is_datum(target.reference)?document::EndTargetKind::Plane:document::EndTargetKind::Face;
        const auto resolved=document::resolve_profile_target(target,refs);
        if(!resolved||std::abs(dot(resolved->fallback_normal,frame.axis))<1.-1e-8)continue;
        r.orientation_drives_rotation=true;r.orientation_role="front";
        r.flip=dot(resolved->fallback_normal,frame.axis)<0;front=true;break;
    }
    if(transverse&&!front) {
        document::ExtrusionParameters::EndTarget target;
        target.reference=parent.mech_keyway.limits.front().reference;
        target.kind=document::profile_target_is_datum(target.reference)?document::EndTargetKind::Plane:document::EndTargetKind::Face;
        const auto resolved=document::resolve_profile_target(target,refs);
        if(resolved&&std::abs(dot(resolved->fallback_normal,frame.axis))>1.-1e-8) {
            document::ConstructionReference axis;
            axis.owner_id=target.reference.owner_id;axis.semantic_key=target.reference.semantic_key;
            axis.instance_path=target.reference.instance_path;axis.orientation_only=true;
            axis.orientation_drives_rotation=true;axis.orientation_role="front";
            axis.flip=dot(resolved->fallback_normal,frame.axis)<0;
            placement.references.push_back(std::move(axis));front=true;
        }
    }
    if(transverse) {
        // The end plane owns the transverse normal. A change of cone slope
        // must not tilt an ordinary decomposed hub profile away from its axis.
        for(auto& r:placement.references)if(r.orientation_role=="front")r.orientation_role="top";
        front=false;
        std::optional<document::ConstructionReference> roll;
        for(const auto& r:placement.references)if(r.use_angle) {
            document::ExtrusionParameters::EndTarget target;
            target.reference={r.owner_id,r.semantic_key,r.instance_path};
            target.kind=document::profile_target_is_datum(target.reference)?document::EndTargetKind::Plane:document::EndTargetKind::Face;
            const auto resolved=document::resolve_profile_target(target,refs);
            if(!resolved||std::abs(dot(resolved->fallback_normal,frame.axis))>1e-8)continue;
            // A cylindrical angle positions the contact point; it cannot also
            // drive FRONT. Retain that equation and use a separate ordinary
            // plane orientation reference for the transverse profile's roll.
            roll=r;roll->use_angle=false;roll->supports_offset=false;roll->offset=0;
            roll->orientation_only=true;roll->orientation_drives_rotation=true;
            roll->orientation_role="front";front=true;break;
        }
        if(roll)placement.references.push_back(std::move(*roll));
    } else {
        auto top=contact;top.orientation_only=true;top.orientation_drives_rotation=true;
        top.orientation_role="top";top.flip=surface.reversed;
        placement.references.push_back(std::move(top));
    }
    V base;
    if(!document::resolve_placement(placement,refs,&base))
        throw std::invalid_argument("Keyway orientation reference is unavailable");
    const auto axes=kernel::annotation_frame({},base).axes;
    const auto local=[&](V v){return V{dot(v,axes[0]),dot(v,axes[1]),dot(v,axes[2])};};
    const auto relative=transverse?rotation_for_axes(local(frame.tangent),local(frame.radial),local(frame.axis)):
        rotation_for_axes(local(mul(frame.tangent,-1)),local(frame.axis),local(frame.radial));
    placement.rotation_offset_x=relative.x;placement.rotation_offset_y=relative.y;
    if(front)placement.rotation_offset_z=relative.z;
    else {placement.absolute_rotation_z=relative.z;placement.rotation_offset_z=0;}
    if(!document::resolve_placement(placement,refs))
        throw std::invalid_argument("Keyway orientation reference is unavailable");
    return placement;
}

void rebase_keyway_profile(sketcher::Sketch& profile,const document::Placement& placement) {
    const auto frame=kernel::annotation_frame({}, {placement.rotation_x,placement.rotation_y,placement.rotation_z});
    const V origin{placement.x,placement.y,placement.z};
    if(dot(profile.resolved_normal,frame.axes[2])<1.-1e-8)
        throw std::invalid_argument("Align the keyway Origin with the support surface and its axis");
    const double rotation=std::atan2(dot(profile.resolved_x_axis,frame.axes[1]),dot(profile.resolved_x_axis,frame.axes[0]));
    for(auto& point:profile.points) {
        const auto delta=sub(profile.world_point(point.x,point.y),origin);
        point.x=dot(delta,frame.axes[0]);point.y=dot(delta,frame.axes[1]);
    }
    for(auto& arc:profile.arcs){arc.start_angle+=rotation;arc.end_angle+=rotation;}
    profile.resolved_origin=origin;profile.resolved_x_axis=frame.axes[0];
    profile.resolved_y_axis=frame.axes[1];profile.resolved_normal=frame.axes[2];
    profile.validate();
}
void numeric_frame(document::HistoryContainer& c,const sketcher::Sketch& s){
    const auto rotation=rotation_for_axes(s.resolved_x_axis,s.resolved_y_axis,s.resolved_normal);
    auto& p=c.placement;p={};p.x=s.resolved_origin.x;p.y=s.resolved_origin.y;p.z=s.resolved_origin.z;
    p.absolute_rotation_x=p.rotation_x=p.rotation_offset_x=rotation.x;
    p.absolute_rotation_y=p.rotation_y=p.rotation_offset_y=rotation.y;
    p.absolute_rotation_z=p.rotation_z=p.rotation_offset_z=rotation.z;
}
void configure_keyway_cut(document::HistoryContainer& cut,const MechKeywayDraft& draft,
        const kernel::ViewerReferenceGeometry* refs=nullptr) {
    const auto previous=cut.feature;
    const auto& definition=draft.cut;const auto& p=draft.parent.mech_keyway;
    cut.combine_mode=document::CombineMode::Subtract;cut.suppressed=draft.parent.suppressed;
    if(draft.cut_placement)cut.placement=*draft.cut_placement;
    else numeric_frame(cut,definition.profile);
    cut.feature={};cut.loft={};
    if(!definition.loft_sections.empty()) {
        cut.feature_kind=document::FeatureKind::Loft;
        const auto axes=kernel::annotation_frame({}, {cut.placement.rotation_x,cut.placement.rotation_y,cut.placement.rotation_z}).axes;
        const V origin{cut.placement.x,cut.placement.y,cut.placement.z};
        const auto local=[&](V v){return V{dot(v,axes[0]),dot(v,axes[1]),dot(v,axes[2])};};
        const auto local_refs=refs?document::general_surface_local_reference_geometry(cut,*refs):kernel::ViewerReferenceGeometry{};
        auto local_frame=draft.frame;
        local_frame.axis=local(local_frame.axis);local_frame.radial=local(local_frame.radial);local_frame.tangent=local(local_frame.tangent);
        for(auto profile:definition.loft_sections) {
            auto owned=document::PartDocument::create_feature_container(profile.id);
            owned.id=profile.owner_container_id;owned.feature_id=owned.id+":feature";
            owned.feature_parent_id=owned.id;owned.container_origin=document::create_container_origin(owned.id);
            owned.feature.type=document::FeatureType::Sketch;
            profile.plane_reference_owner_id=owned.container_origin.id;
            profile.resolved_origin=local(sub(profile.resolved_origin,origin));
            profile.resolved_x_axis=local(profile.resolved_x_axis);
            profile.resolved_y_axis=local(profile.resolved_y_axis);
            profile.resolved_normal=local(profile.resolved_normal);
            std::optional<document::Placement> referenced;
            if(refs) {
                const auto found=std::ranges::find(local_refs.triangle_references,p.support);
                if(found==local_refs.triangle_references.end()||!found->surface)
                    throw std::invalid_argument("Keyway support face is unavailable");
                const auto& surface=*found->surface;
                const double station=dot(sub(profile.resolved_origin,surface.origin),local_frame.axis);
                auto section_parent=draft.parent;
                std::erase_if(section_parent.placement.references,[&](const auto& r) {
                    if(r.orientation_only)return true;
                    if(r.use_angle||r.owner_id==p.support.owner_id&&r.semantic_key==p.support.semantic_key)return false;
                    document::ExtrusionParameters::EndTarget target;
                    target.reference={r.owner_id,r.semantic_key,r.instance_path};
                    target.kind=document::profile_target_is_datum(target.reference)?document::EndTargetKind::Plane:document::EndTargetKind::Face;
                    const auto resolved=document::resolve_profile_target(target,local_refs);
                    return resolved&&std::abs(dot(resolved->fallback_normal,local_frame.axis))>1.-1e-8;
                });
                const bool forward=dot(profile.resolved_origin,local_frame.axis)>0;
                const auto& limit=p.limits[(forward==(p.axial_direction>0))?0:1].reference;
                document::ConstructionReference end;
                end.owner_id=limit.owner_id;end.semantic_key=limit.semantic_key;end.instance_path=limit.instance_path;
                end.supports_offset=true;end.orientation_role=document::placement_position_only_role;
                section_parent.placement.references.push_back(std::move(end));
                referenced=keyway_profile_placement(section_parent,surface,local_frame,station,local_refs,true);
                rebase_keyway_profile(profile,*referenced);
            }
            numeric_frame(owned,profile);
            if(referenced)owned.placement=std::move(*referenced);
            cut.loft.profiles.push_back(owned.id);
            cut.loft.correspondence_starts[owned.id]=profile.id+":first-left";
            cut.loft.sketches.push_back({profile.serialized(),std::nullopt,std::move(owned),cut.id});
        }
        document::resolve_loft_sketches(cut,refs?*refs:kernel::ViewerReferenceGeometry{});
        document::validate_loft(cut);
        return;
    }
    cut.feature_kind=document::FeatureKind::Feature;
    cut.feature.sketch_id=definition.profile.id;
    for(std::size_t i=0;i<2;++i) {
        const double length=i?definition.reverse:definition.forward;
        auto& side=cut.feature.sides[i];
        side.operation=length>1e-9?document::FeatureSideOperation::Extrusion:document::FeatureSideOperation::None;
        side.length=length>1e-9?length:1.;
        if(!p.internal||!refs)continue;
        const std::size_t source=p.axial_direction>0?i:1-i;
        const auto& limit=p.limits[source];
        side.extrusion_extent=document::EndCondition::UpTo;
        document::ExtrusionParameters::EndTarget target;
        target.kind=document::profile_target_is_datum(limit.reference)?document::EndTargetKind::Plane:document::EndTargetKind::Face;
        target.reference=limit.reference;
        const auto resolved=document::resolve_profile_target(target,*refs);
        if(!resolved)throw std::invalid_argument("Keyway axial reference is unavailable");
        side.targets={*resolved};
        if(previous.sides[i].targets.size()==1&&previous.sides[i].targets.front().reference==limit.reference)
            side.up_to=previous.sides[i].up_to;
        side.up_to.length_correction=limit.correction+(source?p.first_overrun:p.last_overrun);
    }
}
void retaining_hole(document::HistoryContainer& hole,const MechKeywayDraft& draft,std::size_t index){
    const auto& p=draft.parent.mech_keyway;
    auto& h=hole.hole;h.type=document::HoleType::MetricThread;
    h.thread_enabled=true;h.thread_nominal_diameter=p.retaining_diameter;
    h.thread_pitch=p.retaining_pitch;h.thread_length=p.retaining_depth;
    h.diameter=p.retaining_diameter-p.retaining_pitch;
    h.bore_length=p.retaining_depth+2*p.retaining_pitch;h.drill_point_enabled=true;
    document::update_hole_profiles(h);
    auto frame=draft.cut.profile;
    frame.resolved_origin=draft.cut.retaining_centres.at(index);frame.resolved_x_axis=draft.frame.axis;
    frame.resolved_y_axis=mul(draft.frame.tangent,-1);frame.resolved_normal=mul(draft.frame.radial,-1);
    numeric_frame(hole,frame);hole.suppressed=draft.parent.suppressed;
}
const kernel::SurfaceGeometry& support(const kernel::FaceReference& r,const kernel::ViewerReferenceGeometry& refs){
    if(!r.valid())throw std::invalid_argument("Select the keyway support face");
    const auto it=std::ranges::find(refs.triangle_references,r);
    if(it==refs.triangle_references.end()||!it->surface||it->surface_result)
        throw std::invalid_argument("Keyway support face is unavailable");
    return *it->surface;
}
double station(const kernel::FaceReference& r,const kernel::SurfaceGeometry& surface,
        const kernel::ViewerReferenceGeometry& refs){
    document::ExtrusionParameters::EndTarget target;target.reference=r;
    target.kind=document::profile_target_is_datum(r)?document::EndTargetKind::Plane:document::EndTargetKind::Face;
    const auto resolved=document::resolve_profile_target(target,refs);
    if(!resolved)throw std::invalid_argument("Keyway axial reference is unavailable");
    const auto normal=resolved->fallback_normal;
    const double parallel=dot(normal,surface.axis);
    if(std::abs(parallel)<1.-1e-8||
        (resolved->reference.surface&&resolved->reference.surface->kind!=kernel::SurfaceGeometry::Kind::Plane))
        throw std::invalid_argument("Keyway axial reference must be perpendicular to the shaft axis");
    return dot(sub(resolved->fallback_origin,surface.origin),surface.axis);
}
document::HistoryContainer child(const document::PartDocument& doc,
        const document::HistoryContainer& parent,const char* role,document::FeatureKind kind){
    for(const auto& item:doc.history)if(item.mech_keyway_binding.parent_id==parent.id&&item.mech_keyway_binding.role==role){
        auto result=item;result.feature_kind=kind;return result;
    }
    auto result=kind==document::FeatureKind::Hole?document::PartDocument::create_hole_container():
        document::PartDocument::create_sketch_container();
    result.feature_kind=kind;result.mech_keyway_binding={parent.id,role};
    return result;
}
void retain_dimension_style(sketcher::Sketch& next,const document::PartDocument& before){
    const auto old=std::ranges::find(before.sketches,next.id,&sketcher::Sketch::id);
    if(old==before.sketches.end())return;
    for(auto& dimension:next.dimensions){
        const auto previous=std::ranges::find(old->dimensions,dimension.id,&sketcher::SketchDimension::id);
        if(previous==old->dimensions.end())continue;
        const auto value=dimension.value;
        const auto upper=dimension.upper_tolerance,lower=dimension.lower_tolerance;
        const auto* annotation=before.find_container(next.owner_container_id);
        const auto* parent=annotation?before.find_container(annotation->mech_keyway_binding.parent_id):nullptr;
        const bool default_deviations=parent&&previous->tolerance_mode=="deviations"&&
            previous->upper_tolerance==parent->mech_keyway.depth_upper_tolerance&&
            previous->lower_tolerance==parent->mech_keyway.depth_lower_tolerance;
        dimension=*previous;dimension.value=value;
        if(default_deviations){dimension.upper_tolerance=upper;dimension.lower_tolerance=lower;}
        dimension.driving=false;dimension.locked=false;
    }
}
void make_profile_editable(sketcher::Sketch& profile,const document::MechKeywayParameters& parameters){
    const auto constraint=[&](std::string id,const std::string& role){
        std::ranges::find(profile.constraints,id,&sketcher::SketchConstraint::id)->id=profile.id+":"+role;
    };
    const auto dimension=[&](sketcher::SketchDimension value,const char* role){
        value.id=profile.id+":"+role;value.driving=true;value.locked=false;
        profile.dimensions.push_back(std::move(value));
    };
    if(!profile.circles.empty()) {
        dimension(profile.create_circle_diameter_dimension(profile.circles.front().id),"disc-diameter");
    } else {
        const auto id=[&](const char* role){return profile.id+":"+role;};
        const auto* first_point=profile.find_point(id("first-left"));
        const auto* last_point=profile.find_point(id("last-left"));
        const bool along_y=std::abs(last_point->y-first_point->y)>std::abs(last_point->x-first_point->x);
        const double length_sense=(along_y?last_point->y-first_point->y:last_point->x-first_point->x)<0?-1.:1.;
        const auto axis=along_y?"sketch_axis:y":"sketch_axis:x";
        const auto parallel=along_y?sketcher::ConstraintKind::Vertical:sketcher::ConstraintKind::Horizontal;
        constraint(profile.add_segment_constraint(id("left-wall"),parallel),"horizontal-left");
        if(profile.arcs.size()==2) {
            constraint(profile.add_equal_radius_constraint(profile.arcs[0].id,profile.arcs[1].id),"equal-end-radius");
            constraint(profile.add_point_on_line_constraint(profile.arcs.front().center_point_id,axis),"centre-on-axis");
        } else {
            if(!profile.arcs.empty())
                constraint(profile.add_segment_constraint(id("right-wall"),parallel),"horizontal-right");
            if(parameters.first_end==document::KeywayEnd::Open)
                constraint(profile.add_symmetric_constraint(id("first-left"),id("first-right"),axis),"symmetric-first");
            if(parameters.last_end==document::KeywayEnd::Open)
                constraint(profile.add_symmetric_constraint(id("last-left"),id("last-right"),axis),"symmetric-last");
        }
        const auto arcs=profile.arcs;
        for(const auto& arc:arcs)for(const auto wall:{"left-wall","right-wall"})
            constraint(profile.add_tangent_constraint(arc.id,id(wall)),"tangent-"+arc.id+":"+wall);
        const auto positive_dimension=[&](std::string first,std::string last,bool y){
            const auto a=profile.find_point(first),b=profile.find_point(last);
            if((y?b->y-a->y:b->x-a->x)<0)std::swap(first,last);
            return profile.create_point_dimension(first,last,y?sketcher::DimensionKind::DistanceY:sketcher::DimensionKind::DistanceX);
        };
        auto width=positive_dimension(id("first-left"),id("first-right"),!along_y);
        width.suffix=parameters.width_fit;dimension(std::move(width),"width");
        const auto extremum=[&](const char* end,const char* corner,int quarter){
            const auto arc=std::ranges::find(profile.arcs,id(end),&sketcher::SketchArc::id);
            if(arc==profile.arcs.end())return id(corner);
            const auto arc_id=arc->id;
            const auto centre=*profile.find_point(arc->center_point_id);
            const auto point=id((std::string(end)+"-extent").c_str());
            const double shift=(quarter==2?-arc->radius:arc->radius)*length_sense;
            profile.points.push_back({point,centre.x+(along_y?0:shift),centre.y+(along_y?shift:0),false,true});
            constraint(profile.add_point_on_circle_constraint(point,arc_id),std::string(end)+"-extent-contact");
            constraint(profile.add_point_on_line_constraint(point,axis),std::string(end)+"-extent-axis");
            return point;
        };
        const auto first=extremum("first-end","first-left",2);
        const auto last=extremum("last-end","last-left",0);
        dimension(positive_dimension(first,last,along_y),"overall-length");
    }
    profile.validate();
}
}

std::vector<kernel::ViewerDimension> mech_keyway_dimensions(const MechKeywayDraft& draft){
    using namespace kernel;
    const auto& owner=draft.parent;const auto& p=owner.mech_keyway;const auto& f=draft.frame;
    const bool disc=p.type==document::KeywayType::Woodruff&&!p.internal;
    const Vec3 origin{owner.placement.x,owner.placement.y,owner.placement.z};
    const auto axis=dimension_scale(f.axis,p.axial_direction);
    const auto point=[&](double station,double radial,double tangent){return dimension_add(f.origin,
        dimension_add(dimension_scale(f.axis,station-p.measurement_station),dimension_add(dimension_scale(f.radial,radial),dimension_scale(f.tangent,tangent))));};
    std::vector<ViewerDimension> dimensions;
    const auto linear=[&](const char* key,Vec3 a,Vec3 b,double value,Vec3 offset,bool driving){
        ViewerDimension d{a,b,dimension_add(a,offset),dimension_add(b,offset),value,{owner.id,std::string("parameter:mech_")+key,{}}};
        const auto normal=dimension_cross(dimension_sub(b,a),offset);
        const auto magnitude=std::hypot(normal.x,normal.y,normal.z);
        d.plane_normal=magnitude>1e-12?dimension_scale(normal,1/magnitude):f.tangent;
        d.driving=driving;dimensions.push_back(std::move(d));
    };
    const double floor=f.radius+(p.internal?p.depth:-p.depth);
    linear("width",point(p.measurement_station,floor,-p.width/2),point(p.measurement_station,floor,p.width/2),p.width,dimension_scale(f.radial,8),false);
    dimensions.back().unit_suffix=p.width_fit;
    linear("length",point(draft.cut.nominal_start,f.radius,0),point(draft.cut.nominal_end,f.radius,0),
        std::abs(draft.cut.nominal_end-draft.cut.nominal_start),dimension_scale(f.tangent,12),!disc&&p.position!=document::KeywayPosition::TwoSided);
    linear("offset",origin,dimension_add(origin,dimension_scale(axis,p.axial_offset)),p.axial_offset,dimension_scale(f.radial,f.radius+16),true);
    if(p.position==document::KeywayPosition::TwoSided){
        const auto centre=dimension_add(origin,dimension_scale(axis,p.axial_offset));
        for(std::size_t i=0;i<2;++i){
            const double station=i?draft.cut.nominal_start:draft.cut.nominal_end;
            const auto end=point(station,0,0);
            const bool length=p.limits[i].termination==document::KeywayTermination::Length;
            linear(i?"reverse_length":"forward_length",centre,end,std::abs(dimension_dot(dimension_sub(end,centre),axis)),dimension_scale(f.radial,f.radius+8),length);
            if(!length){
                ViewerDimension correction;correction.reference={owner.id,i?"parameter:mech_reverse_correction":"parameter:mech_forward_correction",{}};
                correction.value=p.limits[i].correction;correction.label_only=true;
                correction.label_position=dimension_add(end,dimension_scale(f.radial,f.radius+20));dimensions.push_back(std::move(correction));
            }
        }
    }
    auto measurement=document::keyway_measurement_sketch(owner.id,p,f).viewer_mesh().dimensions;
    for(auto& d:measurement){d.reference={owner.id,"parameter:mech_measurement",{}};d.driving=false;dimensions.push_back(std::move(d));}
    return dimensions;
}

document::HistoryContainer create_mech_keyway(){
    auto c=document::PartDocument::create_sketch_container();
    c.feature_kind=document::FeatureKind::MechKeyway;c.combine_mode=document::CombineMode::Subtract;
    c.name="Drážka pro pero";return c;
}

void initialize_mech_keyway_placement(document::HistoryContainer& parent,
        const kernel::ViewerReferenceGeometry& refs,const kernel::FaceReference& axial_plane){
    const auto& surface=support(parent.mech_keyway.support,refs);
    document::ConstructionReference axis;
    axis.owner_id=parent.mech_keyway.support.owner_id;
    axis.semantic_key=parent.mech_keyway.support.semantic_key;
    axis.instance_path=parent.mech_keyway.support.instance_path;
    axis.use_axis=false;axis.supports_offset=true;
    axis.orientation_drives_rotation=true;axis.orientation_role="top";axis.flip=surface.reversed;
    auto placement=parent.placement;
    placement.x=surface.origin.x+surface.radius*surface.radial.x;
    placement.y=surface.origin.y+surface.radius*surface.radial.y;
    placement.z=surface.origin.z+surface.radius*surface.radial.z;
    placement.references={axis};
    if(axial_plane.valid()) {
        static_cast<void>(station(axial_plane,surface,refs));
        document::ConstructionReference end;
        end.owner_id=axial_plane.owner_id;end.semantic_key=axial_plane.semantic_key;
        end.instance_path=axial_plane.instance_path;end.supports_offset=true;
        end.orientation_role=surface.kind==kernel::SurfaceGeometry::Kind::Cylinder?"front":document::placement_position_only_role;
        end.orientation_drives_rotation=surface.kind==kernel::SurfaceGeometry::Kind::Cylinder;
        placement.references.push_back(std::move(end));
    }
    if(!document::resolve_placement(placement,refs))
        throw std::invalid_argument("Keyway orientation reference is unavailable");
    parent.placement=std::move(placement);
}

MechKeywayDraft resolve_mech_keyway_draft(document::HistoryContainer parent,
        const std::string& cut_id,const kernel::ViewerReferenceGeometry& refs){
    if(parent.feature_kind!=document::FeatureKind::MechKeyway)
        throw std::invalid_argument("Invalid MECH feature");
    const auto& surface=support(parent.mech_keyway.support,refs);
    auto& p=parent.mech_keyway;
    if(p.internal!=surface.reversed)throw std::invalid_argument("Keyway support changed its material side");
    if(p.internal&&(p.position!=document::KeywayPosition::TwoSided||
            p.first_end!=document::KeywayEnd::Open||p.last_end!=document::KeywayEnd::Open||
            std::ranges::any_of(p.limits,[](const auto& limit){return
                limit.termination!=document::KeywayTermination::UpTo||!limit.reference.valid()||limit.correction<0;})))
        throw std::invalid_argument("A hub keyway requires two Up To end references and outward corrections");
    auto placement=parent.placement;
    if(!document::resolve_placement(placement,refs))throw std::invalid_argument("Keyway orientation reference is unavailable");
    parent.placement=placement;
    const auto axes=kernel::annotation_frame({}, {placement.rotation_x,placement.rotation_y,placement.rotation_z});
    const auto delta=sub(V{placement.x,placement.y,placement.z},surface.origin);
    const double reference=dot(delta,surface.axis);
    const auto transverse=sub(delta,mul(surface.axis,reference));
    const double radial_distance=std::hypot(transverse.x,transverse.y,transverse.z);
    auto radial=axes.axes[0];
    if(radial_distance>1e-7) {
        const double radius=surface.radius+(surface.kind==kernel::SurfaceGeometry::Kind::Cone?reference*std::tan(surface.semi_angle):0.);
        if(std::abs(radial_distance-radius)>1e-7)
            throw std::invalid_argument("Place the keyway Origin on the support surface or its axis");
        radial=mul(transverse,1/radial_distance);
        const double slope=surface.kind==kernel::SurfaceGeometry::Kind::Cone?std::tan(surface.semi_angle):0.;
        const auto normal=mul(sub(radial,mul(surface.axis,slope)),1./std::hypot(1.,slope));
        // The surface owns TOP through the ordinary placement contract. Its
        // remaining roll does not redefine the support's exact axial direction.
        if(dot(axes.axes[2],normal)<1.-1e-8)
            throw std::invalid_argument("Align the keyway Origin with the support surface and its axis");
    }else if(std::abs(dot(axes.axes[2],surface.axis))<1.-1e-8)
        throw std::invalid_argument("Align the keyway Origin Z axis with the support axis");
    std::array<std::optional<double>,2> targets;
    if(p.position==document::KeywayPosition::TwoSided)
        for(std::size_t i=0;i<2;++i)if(p.limits[i].termination==document::KeywayTermination::UpTo)
            targets[i]=station(p.limits[i].reference,surface,refs);
    if(p.type==document::KeywayType::Woodruff&&!p.internal) {
        // The cutter centre is authored independently of its catalogue size.
        // Resolve the size there before validating the disc-derived extents;
        // a newly selected Woodruff type still carries the previous dimensions.
        p.measurement_station=reference+p.axial_direction*p.axial_offset;
    } else {
        const auto extents=document::keyway_axial_extents(p,reference,targets);
        p.measurement_station=(extents[0]+extents[1])/2;
        if(p.position==document::KeywayPosition::TwoSided)p.length=std::abs(extents[1]-extents[0]);
    }
    auto frame=document::keyway_frame(surface,radial,p.measurement_station);
    if(!p.table_id.empty())document::select_keyway_size(p,2*frame.radius);
    auto definition=document::keyway_cut_definition(cut_id,p,frame,reference,targets);
    std::optional<document::Placement> cut_placement;
    if(p.internal||p.type==document::KeywayType::Parallel) {
        const auto profile_station=reference+(p.internal?p.axial_direction*p.axial_offset:0.);
        cut_placement=keyway_profile_placement(parent,surface,frame,profile_station,refs,p.internal);
        rebase_keyway_profile(definition.profile,*cut_placement);
    }
    return {std::move(parent),std::move(frame),std::move(definition),std::move(cut_placement)};
}

std::vector<kernel::ViewerEdge> mech_keyway_preview_edges(const MechKeywayDraft& draft){
    document::PartDocument carrier;
    carrier.sketches.push_back(draft.cut.profile);
    document::HistoryContainer cut;
    cut.id=draft.cut.profile.owner_container_id;cut.feature_id=draft.parent.feature_id;
    configure_keyway_cut(cut,draft);
    auto edges=cut.feature_kind==document::FeatureKind::Loft?
        document::loft_definition_mesh(carrier,cut).edges:carrier.feature_preview_edges(cut);
    if(!draft.cut.loft_sections.empty()) {
        const auto& first=draft.cut.loft_sections.front();const auto& last=draft.cut.loft_sections.back();
        for(std::size_t i=0;i<first.points.size();++i) {
            kernel::ViewerEdge edge;
            edge.reference={cut.id,"loft:preview:"+first.points[i].id,{}};
            edge.points={first.world_point(first.points[i].x,first.points[i].y),last.world_point(last.points[i].x,last.points[i].y)};
            edges.push_back(std::move(edge));
        }
    }
    for(std::size_t i=0;i<draft.cut.retaining_centres.size();++i){
        auto hole=document::PartDocument::create_hole_container();
        hole.id=draft.parent.id+(i?":retaining-2":":retaining-1");
        retaining_hole(hole,draft,i);
        auto preview=carrier.primitive_preview_edges(hole);
        edges.insert(edges.end(),std::make_move_iterator(preview.begin()),std::make_move_iterator(preview.end()));
    }
    return edges;
}

document::PartDocument prepare_mech_keyway(const document::PartDocument& before,
        document::HistoryContainer parent,const kernel::ViewerReferenceGeometry& refs){
    const auto* existing=before.find_container(parent.id);
    parent.combine_mode=document::CombineMode::Subtract;
    const auto* owner=existing?before.body_owner_for_object(parent.id):before.body_history.find(before.body_history.active_body_id());
    if(!owner||owner->derived_copy||owner->suppressed||owner->scope.id!=before.body_history.active_body_id())
        throw std::invalid_argument("Activate the editable Body owning the keyway");
    auto cut=child(before,parent,"cut",document::FeatureKind::Feature);
    auto draft=resolve_mech_keyway_draft(std::move(parent),cut.id,refs);
    parent=draft.parent;
    const auto& p=parent.mech_keyway;
    const auto& frame=draft.frame;
    auto& definition=draft.cut;
    configure_keyway_cut(cut,draft,&refs);
    std::vector<document::HistoryContainer> children{cut};
    std::vector<sketcher::Sketch> sketches;
    if(cut.feature_kind==document::FeatureKind::Feature)sketches.push_back(definition.profile);
    if(p.bottom_radius>0) {
        auto fillet=child(before,parent,"bottom-fillet",document::FeatureKind::Fillet);
        fillet.combine_mode=document::CombineMode::Add;fillet.suppressed=parent.suppressed;
        // The ordinary OCCT route preserves the Woodruff floor's toroidal
        // rolling-ball surface, including its end transitions.
        fillet.edge_treatment.calculation_method=document::EdgeTreatmentParameters::CalculationMethod::Occt;
        fillet.edge_treatment.primary_size=p.bottom_radius;fillet.edge_treatment.routes.clear();
        std::vector<kernel::EdgeReference> edges;
        const auto rim=[&](const std::string& curve,kernel::FeatureSide side,const char* cap) {
            edges.push_back({cut.id,std::string(cap)+":"+kernel::feature_side_parent_key({cut.feature_id,side,curve}),{}});
        };
        if(p.internal) {
            for(const auto* point:{"last-left","last-right"}) {
                if(cut.feature_kind==document::FeatureKind::Loft) {
                    const auto& first=definition.loft_sections.front();
                    edges.push_back({cut.id,"loft:longitudinal:from:loft:point:from:"+
                        first.owner_container_id+":"+first.id+":"+point,{}});
                } else for(std::size_t i=0;i<2;++i)if(cut.feature.sides[i].operation!=document::FeatureSideOperation::None)
                    edges.push_back({cut.id,"generated:"+kernel::feature_side_parent_key({cut.feature_id,
                        i?kernel::FeatureSide::Start:kernel::FeatureSide::End,definition.profile.id+":"+point}),{}});
            }
        } else if(p.type==document::KeywayType::Woodruff) {
            rim(definition.profile.circles.front().id,kernel::FeatureSide::End,"end");
            rim(definition.profile.circles.front().id,kernel::FeatureSide::Start,"start");
        } else {
            const auto side=p.internal?kernel::FeatureSide::End:kernel::FeatureSide::Start;
            const auto cap=p.internal?"end":"start";
            for(const auto& segment:definition.profile.segments)
                if(segment.id.ends_with(":left-wall")||segment.id.ends_with(":right-wall"))rim(segment.id,side,cap);
            for(const auto& arc:definition.profile.arcs)rim(arc.id,side,cap);
        }
        // Separate exact references let the existing Fillet route planner
        // recover connected or clipped branches at the real input boundary.
        for(auto& edge:edges)fillet.edge_treatment.routes.push_back({std::move(edge)});
        children.push_back(std::move(fillet));
    }
    for(std::size_t i=0;i<definition.retaining_centres.size();++i){
        auto hole=child(before,parent,i?"retaining-2":"retaining-1",document::FeatureKind::Hole);
        retaining_hole(hole,draft,i);children.push_back(std::move(hole));
    }
    auto annotation=child(before,parent,"annotation",document::FeatureKind::Sketch);
    auto measured=document::keyway_measurement_sketch(annotation.id,p,frame);
    const auto* previous_cut=before.find_container(cut.id);
    if(existing&&previous_cut&&previous_cut->feature_kind==cut.feature_kind&&previous_cut->feature.sides==cut.feature.sides&&
        existing->mech_keyway.type==p.type&&existing->mech_keyway.internal==p.internal&&existing->mech_keyway.support==p.support) {
        const auto old=std::ranges::find(before.sketches,measured.id,&sketcher::Sketch::id);
        if(old!=before.sketches.end()) {
            measured.external_references=old->external_references;
            measured.constraints=old->constraints;
        }
    }
    retain_dimension_style(measured,before);numeric_frame(annotation,measured);
    annotation.suppressed=parent.suppressed;children.push_back(annotation);sketches.push_back(std::move(measured));
    auto next=before;auto graph=next.body_history;auto body=*owner;
    std::set<std::string> old_ids{parent.id};
    for(const auto& c:before.history)if(c.mech_keyway_binding.parent_id==parent.id)old_ids.insert(c.id);
    auto insertion=body.cursor;
    if(existing){const auto row=std::ranges::find(body.entries,parent.id,&document::PartHistoryEntry::id);
        if(row==body.entries.end())throw std::invalid_argument("Keyway is missing from Body history");
        insertion=static_cast<std::size_t>(row-body.entries.begin());}
    std::size_t removed{};
    for(std::size_t i=0;i<body.cursor;++i)if(old_ids.contains(body.entries[i].id))++removed;
    std::erase_if(body.entries,[&](const auto& e){return old_ids.contains(e.id);});body.cursor-=removed;
    std::vector<document::PartHistoryEntry> group{{document::PartHistoryKind::Feature,parent.id}};
    for(const auto& c:children)group.push_back({document::PartHistoryKind::Feature,c.id});
    body.entries.insert(body.entries.begin()+static_cast<std::ptrdiff_t>(insertion),group.begin(),group.end());
    if(insertion<=body.cursor)body.cursor+=group.size();graph.update_body(std::move(body));
    // Replacing a compound must retain the storage order of existing objects.
    // Removing and appending the whole group makes two unchanged compounds
    // reorder one another on every reference-regeneration pass.
    children.insert(children.begin(),std::move(parent));
    const auto replace_owned=[](auto& stored,auto& replacements,const auto& owned) {
        std::erase_if(stored,[&](auto& value) {
            if(!owned(value))return false;
            const auto replacement=std::ranges::find(replacements,value.id,[](const auto& item)->const auto&{return item.id;});
            if(replacement==replacements.end())return true;
            value=std::move(*replacement);replacements.erase(replacement);return false;
        });
        stored.insert(stored.end(),std::make_move_iterator(replacements.begin()),std::make_move_iterator(replacements.end()));
    };
    replace_owned(next.history,children,[&](const auto& c){return old_ids.contains(c.id);});
    replace_owned(next.sketches,sketches,[&](const auto& s){return old_ids.contains(s.owner_container_id);});
    next.set_body_history(std::move(graph));
    // Use the same command-local native placement resolution as generated
    // Sheet Profile children. Canonical frames and reference-valid state must
    // already match ordinary regeneration before comparing unchanged drafts.
    std::vector<std::string> draft_ids;
    for(const auto& entry:group)draft_ids.push_back(entry.id);
    resolve_sheet_feature_drafts(next,draft_ids,refs);
    next.reference_errors.erase(draft.parent.id);
    for(const auto& entry:group)next.reference_errors.erase(entry.id);
    next.validate_mech_keyways();return next;
}

bool refresh_mech_keyways(document::PartDocument& doc,const kernel::ViewerReferenceGeometry& refs,
        const document::HistoryReferenceViews& views){
    std::vector<std::string> ids;for(const auto& c:doc.history)
        if(c.feature_kind==document::FeatureKind::MechKeyway&&!c.suppressed)ids.push_back(c.id);
    bool changed=false;const document::HistoryReferenceResolver resolver(refs,views);
    for(const auto& id:ids){
        const auto* owner=doc.body_owner_for_object(id);if(!owner||owner->suppressed)continue;
        const auto row=std::ranges::find(owner->entries,id,&document::PartHistoryEntry::id);
        if(row==owner->entries.end()||static_cast<std::size_t>(row-owner->entries.begin())>=owner->cursor)continue;
        try{
            auto input=doc;auto graph=input.body_history;const auto active=graph.active_body_id();
            graph.activate(owner->scope.id);input.set_body_history(std::move(graph));
            auto local=refs;resolver.enter(id,local);
            document::history_reference_detail::append(local,doc.origin_viewer_mesh().original_references);
            document::history_reference_detail::append(local,doc.construction_viewer_mesh().original_references);
            local=doc.construction_reference_geometry_for(owner->scope.id,std::move(local));
            auto next=prepare_mech_keyway(input,*input.find_container(id),local);
            graph=next.body_history;graph.activate(active);next.set_body_history(std::move(graph));
            if(const auto error=doc.reference_errors.find(id);error!=doc.reference_errors.end()) {
                for(const auto& c:next.history)if(c.mech_keyway_binding.parent_id==id) {
                    const auto child_error=next.reference_errors.find(c.id);
                    if(child_error!=next.reference_errors.end()&&child_error->second==error->second)
                        next.reference_errors.erase(child_error);
                }
                next.reference_errors.erase(id);
            }
            const bool sketch_changes=next.sketches.size()!=doc.sketches.size()||
                std::ranges::any_of(next.sketches,[&](const auto& s){
                    const auto old=std::ranges::find(doc.sketches,s.id,&sketcher::Sketch::id);
                    return old==doc.sketches.end()||old->serialized()!=s.serialized();});
            if(next.history!=doc.history||sketch_changes||next.reference_errors!=doc.reference_errors||
                next.body_history.bodies()!=doc.body_history.bodies()){doc=std::move(next);changed=true;}
        }catch(const std::exception& e){
            // Keep all last-valid definitions and frames, but never recalculate
            // their material as if the missing support had been resolved.
            for(const auto& c:doc.history)if(c.id==id||c.mech_keyway_binding.parent_id==id){
                auto& message=doc.reference_errors[c.id];if(message!=e.what()){message=e.what();changed=true;}}
        }
    }
    return changed;
}

const document::HistoryContainer* mech_keyway_symbol_owner(
        const document::PartDocument& doc,const std::string& symbol_id){
    if(symbol_id.empty())return nullptr;
    for(const auto& owner:doc.history)if(owner.feature_kind==document::FeatureKind::MechKeyway&&
        (owner.mech_keyway.walls_symbol_id==symbol_id||owner.mech_keyway.floor_symbol_id==symbol_id))return &owner;
    return nullptr;
}

bool commit_mech_keyway(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
        document::HistoryContainer parent,std::optional<std::vector<symbols::Placement>> annotations){
    auto* state=live.open_part(id);if(!state)throw std::invalid_argument("MECH requires a Part");
    const auto& before=state->session.document();const auto* existing=before.find_container(parent.id);
    bool annotation_changes=false;
    if(annotations)for(const auto& value:*annotations){
        if(value.symbol.id!=parent.mech_keyway.walls_symbol_id&&value.symbol.id!=parent.mech_keyway.floor_symbol_id)
            throw std::invalid_argument("Keyway surface texture symbol is unavailable");
        value.validate();
        const auto old=std::ranges::find(before.symbol_annotations,value.symbol.id,[](const auto& item){return item.symbol.id;});
        annotation_changes|=old==before.symbol_annotations.end()||*old!=value;
    }
    if(existing&&parent==*existing&&!annotation_changes)return false;
    auto annotation_document=before;
    if(annotations&&annotation_changes)for(auto value:*annotations){
        auto& values=annotation_document.symbol_annotations;
        const auto old=std::ranges::find(values,value.symbol.id,[](const auto& item){return item.symbol.id;});
        if(old==values.end())values.push_back(std::move(value));else *old=std::move(value);
    }
    if(existing){
        auto geometry=parent;geometry.mech_keyway.show_roughness=existing->mech_keyway.show_roughness;
        geometry.mech_keyway.walls_symbol_id=existing->mech_keyway.walls_symbol_id;
        geometry.mech_keyway.floor_symbol_id=existing->mech_keyway.floor_symbol_id;
        if(geometry==*existing){
            if(parent==*existing&&annotation_document.symbol_annotations==before.symbol_annotations)return false;
            *annotation_document.find_container(parent.id)=parent;
            bind_mech_keyway_roughness(annotation_document,state->session.calculated_boundaries());
            commit_part_document_metadata(live,id,std::move(annotation_document));return true;
        }
    }
    // Saved intermediate rows can omit their duplicate reference packet.
    // The shared rollback query supplies the preceding owners' exact packet
    // from the current Body without calculating another solid.
    const auto rollback=existing?state->session.rollback_boundary(parent.id):std::nullopt;
    const auto* input=existing?(rollback&&rollback->input_body?&*rollback->input_body:nullptr):
        calculated_operation_input(state->session,{});
    if(!input)throw std::invalid_argument("Keyway requires calculated input material");
    auto refs=input->mesh.original_references;
    auto datums=before.origin_viewer_mesh().original_references;
    document::history_reference_detail::append(datums,before.construction_viewer_mesh().original_references);
    const auto* owner=existing?before.body_owner_for_object(parent.id):before.body_history.find(before.body_history.active_body_id());
    if(owner)datums=before.construction_reference_geometry_for(owner->scope.id,std::move(datums));
    document::history_reference_detail::append(refs,datums);
    document::PartDocument next;
    try { next=prepare_mech_keyway(annotation_document,parent,refs); }
    catch(const std::invalid_argument& error) {
        const auto* attempt=ModelingConfirmationAttempt::current();
        if(!attempt||!attempt->applies(before))throw;
        if(!owner||owner->derived_copy||owner->suppressed||owner->scope.id!=before.body_history.active_body_id())throw;
        document::validate_mech_keyway(parent.mech_keyway);
        next=annotation_document;
        parent.combine_mode=document::CombineMode::Subtract;
        if(existing)*next.find_container(parent.id)=parent;
        else {
            next.history.push_back(parent);
            next.insert_history_entry(document::PartHistoryKind::Feature,parent.id);
        }
        for(auto& item:next.history)if(item.id==parent.id||item.mech_keyway_binding.parent_id==parent.id) {
            item.suppressed=parent.suppressed;
            next.reference_errors[item.id]=error.what();
        }
        next.validate_mech_keyways();
    }
    PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    commit_part_document(live,id,std::move(next),std::move(calculated));return true;
}

void bind_mech_keyway_roughness(document::PartDocument& doc,const std::vector<kernel::BodyResult>& calculated){
    for(const auto& parent:doc.history){
        if(parent.feature_kind!=document::FeatureKind::MechKeyway)continue;
        const auto& p=parent.mech_keyway;
        if(p.walls_symbol_id.empty()&&p.floor_symbol_id.empty())continue;
        const auto annotation=std::ranges::find_if(doc.history,[&](const auto& item){return item.mech_keyway_binding.parent_id==parent.id&&item.mech_keyway_binding.role=="annotation";});
        const auto cut=std::ranges::find_if(doc.history,[&](const auto& item){return item.mech_keyway_binding.parent_id==parent.id&&item.mech_keyway_binding.role=="cut";});
        const auto* input=annotation==doc.history.end()?nullptr:calculated_operation_input(doc,calculated,annotation->id);
        for(int index=0;index<2;++index){
            const auto& id=index?p.floor_symbol_id:p.walls_symbol_id;
            auto symbol=std::ranges::find(doc.symbol_annotations,id,[](const auto& item){return item.symbol.id;});
            if(symbol==doc.symbol_annotations.end()){
                if(p.show_roughness)throw std::invalid_argument("Keyway surface texture symbol is unavailable");continue;
            }
            symbol->symbol.visible=p.show_roughness&&!parent.suppressed;
            if(!symbol->symbol.visible)continue;
            if(!input||cut==doc.history.end()||doc.reference_errors.contains(parent.id)){symbol->unresolved=true;continue;}
            // Existing Symbols keep their authored surface parameters and grip.
            // The final packet remains authoritative when an intermediate
            // boundary was intentionally omitted from the native display cache.
            if(symbol->reference&&!calculated.empty()&&
                    symbols::refresh_surface_attachment(*symbol,calculated.back().mesh.original_references))continue;
            const auto axes=kernel::annotation_frame({}, {parent.placement.rotation_x,parent.placement.rotation_y,parent.placement.rotation_z});
            auto source_refs=input->mesh.original_references;
            if(source_refs.triangle_references.empty()&&!calculated.empty()) {
                source_refs=calculated.back().mesh.original_references;
                if(const auto* body=doc.body_owner_for_object(parent.id))
                    source_refs=doc.construction_reference_geometry_for(body->scope.id,std::move(source_refs));
            }
            const auto& surface=support(p.support,source_refs);
            const auto delta=sub(V{parent.placement.x,parent.placement.y,parent.placement.z},surface.origin);
            const auto radial=sub(delta,mul(surface.axis,dot(delta,surface.axis)));
            const auto frame=document::keyway_frame(surface,std::hypot(radial.x,radial.y,radial.z)>1e-7?radial:axes.axes[0],p.measurement_station);
            const auto point=[&](double radial,double tangent){return kernel::dimension_add(frame.origin,
                kernel::dimension_add(mul(frame.radial,radial),mul(frame.tangent,tangent)));};
            const auto contact=index?point(frame.radius+(p.internal?p.depth:-p.depth),p.width*.3):
                point(frame.radius+(p.internal?p.depth:-p.depth)*.5,p.width*.5);
            const auto normal=index&&p.internal&&p.floor==document::KeywayFloor::ParallelToGenerator?
                sub(frame.radial,mul(frame.axis,frame.slope)):index?frame.radial:frame.tangent;
            const double normal_length=std::sqrt(dot(normal,normal));
            std::optional<kernel::FaceReference> target;
            for(const auto& binding:input->kernel_faces){
                const auto& face=binding.reference;
                if(face.owner_id!=cut->id||!face.surface)continue;
                if(p.internal&&!hub_face_side(*cut,contact,frame.axis,face))continue;
                if(!p.internal&&!index&&p.type!=document::KeywayType::Woodruff) {
                    // A hub wall spans both ordinary Feature sides. Attach
                    // the texture to the depth side above the support Sketch.
                    constexpr std::string_view prefix="generated:";
                    if(!face.semantic_key.starts_with(prefix))continue;
                    const auto side=kernel::feature_side_parent(std::string_view(face.semantic_key).substr(prefix.size()));
                    if(!side||side->side!=(p.internal?kernel::FeatureSide::End:kernel::FeatureSide::Start))continue;
                }
                const auto& surface=*face.surface;
                bool matches=false;
                auto reference=face;
                if(index&&p.type==document::KeywayType::Woodruff&&!p.internal){
                    if(const auto authored=woodruff_floor_reference(*cut,face,input->mesh.original_references)){
                        reference=*authored;matches=true;
                    }
                }else if(surface.kind==kernel::SurfaceGeometry::Kind::Plane)
                    matches=std::abs(dot(surface.axis,normal))>normal_length*(1-1e-8)&&
                        std::abs(dot(sub(contact,surface.origin),surface.axis))<1e-7;
                if(matches){if(target&&*target!=reference)throw std::invalid_argument("Keyway surface texture reference is ambiguous");target=reference;}
            }
            if(!target)throw std::invalid_argument("Keyway surface texture reference is unavailable");
            symbols::attach_to_surface(*symbol,*target,doc.document_id,contact);
            // Annotation placement is in Part coordinates, while operation
            // input geometry above is in the owning Body's local coordinates.
            if(!calculated.empty())static_cast<void>(symbols::refresh_surface_attachment(*symbol,calculated.back().mesh.original_references));
        }
    }
}

void bind_mech_keyway_annotations(const kernel::OcctKernel& kernel,document::PartDocument& doc,
        const std::vector<kernel::BodyResult>& calculated) {
    std::optional<Workspace> context;
    for(auto& sketch:doc.sketches) {
        const auto* annotation=doc.find_container(sketch.owner_container_id);
        if(!annotation||annotation->suppressed||annotation->mech_keyway_binding.role!="annotation"||
            !sketch.external_references.empty()||doc.reference_errors.contains(annotation->id))continue;
        const auto* parent=doc.find_container(annotation->mech_keyway_binding.parent_id);
        if(!parent||parent->feature_kind!=document::FeatureKind::MechKeyway)continue;
        if(!context){context.emplace();context->add_part(doc,calculated);}
        const auto& session=context->open_part(doc.document_id)->session;
        const auto* input=calculated_operation_input(session,annotation->id);
        if(!input||!input->calculation_errors.empty())continue;
        const auto cut=std::ranges::find_if(doc.history,[&](const auto& c){
            return c.mech_keyway_binding.parent_id==parent->id&&c.mech_keyway_binding.role=="cut";});
        if(cut==doc.history.end())throw std::invalid_argument("Keyway annotation has no cut source");
        const auto& p=parent->mech_keyway;
        const auto profile=std::ranges::find(doc.sketches,cut->feature.sketch_id,&sketcher::Sketch::id);
        if(!p.internal&&profile==doc.sketches.end())throw std::invalid_argument("Keyway annotation has no cut source");
        const auto* floor_point=sketch.find_point(sketch.id+":floor");
        const auto floor_world=sketch.world_point(floor_point->x,floor_point->y);
        auto floor_normal=p.internal?sketch.resolved_y_axis:profile->resolved_normal;
        if(p.internal&&p.floor==document::KeywayFloor::ParallelToGenerator) {
            const auto& source=support(p.support,input->mesh.original_references);
            if(source.kind==kernel::SurfaceGeometry::Kind::Cone)
                floor_normal=sub(floor_normal,mul(sketch.resolved_normal,std::tan(source.semi_angle)));
        }
        floor_normal=mul(floor_normal,1./std::sqrt(dot(floor_normal,floor_normal)));
        std::set<std::pair<std::string,std::string>> seen;
        std::optional<kernel::FaceReference> floor;
        for(const auto& binding:input->kernel_faces) {
            const auto& face=binding.reference;
            if(face.owner_id!=cut->id||!face.surface||!seen.emplace(face.owner_id,face.semantic_key).second)continue;
            if(p.internal&&!hub_face_side(*cut,floor_world,sketch.resolved_normal,face))continue;
            // Original reference packets also retain the inactive Feature
            // side. Only a face present in the calculated input can anchor
            // the manufacturing dimension.
            const auto& surface=*face.surface;
            bool matches=false;
            auto reference=face;
            if(p.type==document::KeywayType::Woodruff&&!p.internal) {
                if(const auto authored=woodruff_floor_reference(*cut,face,input->mesh.original_references)){
                    reference=*authored;matches=true;
                }
            } else if(surface.kind==kernel::SurfaceGeometry::Kind::Plane) {
                matches=std::abs(dot(surface.axis,floor_normal))>1-1e-8&&
                    std::abs(dot(sub(floor_world,surface.origin),surface.axis))<1e-8;
            }
            if(matches){
                if(floor&&*floor!=reference)throw std::invalid_argument("Keyway floor reference is ambiguous");
                floor=reference;
            }
        }
        if(!floor)throw std::invalid_argument("Keyway floor reference is unavailable");
        auto pending=sketch;
        for(const auto role:{p.type==document::KeywayType::Woodruff&&!p.internal?"surface":"opposite","floor"}) {
            const bool on_support=std::string_view(role)!="floor";
            auto face=on_support?p.support:*floor;
            const auto point_id=pending.id+":"+role;
            const auto* point=pending.find_point(point_id);
            std::vector<sketcher::SketchExternalReference> branches;
            const auto& geometry=face.surface?*face.surface:support(face,calculated.back().mesh.original_references);
            if((geometry.kind==kernel::SurfaceGeometry::Kind::Plane&&
                    std::abs(dot(geometry.axis,pending.resolved_normal))<1e-10)||
                ((geometry.kind==kernel::SurfaceGeometry::Kind::Cylinder||
                   (on_support&&geometry.kind==kernel::SurfaceGeometry::Kind::Cone))&&
                    std::abs(dot(geometry.axis,pending.resolved_normal))>1-1e-10)) {
                // Ordinary planar supports and axial cylinder circles already
                // have exact native reference representations. Their boundary
                // fragments are not the manufacturing dimension's definition.
                if(geometry.kind==kernel::SurfaceGeometry::Kind::Cone)
                    face.semantic_key=sketcher::analytic_section_key(face.semantic_key);
                branches.push_back(prepare_sketch_external_reference(*context,doc.document_id,pending,
                    sketcher::ExternalReferenceKind::Face,face.owner_id,face.semantic_key,face.instance_path));
            } else if(!on_support&&p.type==document::KeywayType::Woodruff&&!p.internal&&
                    geometry.kind==kernel::SurfaceGeometry::Kind::Cylinder&&
                    std::abs(dot(geometry.axis,pending.resolved_normal))<1e-10&&
                    std::abs(dot(sub(geometry.origin,pending.resolved_origin),pending.resolved_normal))<1e-8) {
                // This section contains the cutter axis. Its two exact
                // generators are the ordinary persisted face-curve supports;
                // their identity is independent of Boolean seam fragments.
                for(int side:{-1,1})branches.push_back(prepare_sketch_external_reference(
                    *context,doc.document_id,pending,sketcher::ExternalReferenceKind::Face,
                    face.owner_id,sketcher::face_curve_key(face.semantic_key,side),face.instance_path));
            } else branches=prepare_sketch_intersection_branches(*context,kernel,doc.document_id,pending,face,face);
            std::optional<sketcher::SketchExternalReference> selected;
            for(auto branch:branches) {
                if(branch.kind!=sketcher::ExternalReferenceKind::Edge&&branch.kind!=sketcher::ExternalReferenceKind::Face)continue;
                branch.id=pending.id+":reference:"+role;
                bool incident=false;
                if(const auto line=sketcher::external_reference_line(branch)) {
                    const auto& [a,d]=*line;
                    incident=std::abs((point->x-a[0])*d[1]-(point->y-a[1])*d[0])<=1e-7*std::hypot(d[0],d[1]);
                } else {
                    auto probe=pending;probe.add_external_reference(branch);
                    const auto projected=probe.project_point_to_curve(branch.id,point->x,point->y);
                    incident=projected&&std::hypot((*projected)[0]-point->x,(*projected)[1]-point->y)<=1e-7;
                }
                if(!incident)continue;
                if(selected)throw std::invalid_argument("Keyway annotation intersection is ambiguous");
                selected=std::move(branch);
            }
            if(!selected)throw std::invalid_argument("Keyway annotation intersection is unavailable");
            const auto reference_id=selected->id;
            const bool circular=sketcher::external_reference_circle(*selected).has_value();
            pending.add_external_reference(std::move(*selected));
            const auto contact=circular?pending.add_point_on_circle_constraint(point_id,reference_id):
                pending.add_point_on_line_constraint(point_id,reference_id);
            std::ranges::find(pending.constraints,contact,&sketcher::SketchConstraint::id)->id=pending.id+":contact:"+role;
            const auto axis=pending.add_point_on_line_constraint(point_id,"sketch_axis:y");
            std::ranges::find(pending.constraints,axis,&sketcher::SketchConstraint::id)->id=pending.id+":axis:"+role;
        }
        pending.validate();sketch=std::move(pending);
    }
}

bool explode_mech_keyway(Workspace& live,const std::string& id,const std::string& container){
    auto* state=live.open_part(id);if(!state)throw std::invalid_argument("MECH requires a Part");
    auto next=state->session.document();const auto* parent=next.find_container(container);
    const auto* owner=next.body_owner_for_object(container);
    if(!parent||parent->feature_kind!=document::FeatureKind::MechKeyway||!owner||
       owner->scope.id!=next.body_history.active_body_id())throw std::invalid_argument("Activate the Body owning this keyway");
    auto graph=next.body_history;auto body=*owner;
    const auto row=std::ranges::find(body.entries,container,&document::PartHistoryEntry::id);
    if(row==body.entries.end())throw std::invalid_argument("Keyway is missing from Body history");
    if(static_cast<std::size_t>(row-body.entries.begin())<body.cursor)--body.cursor;body.entries.erase(row);
    std::vector<std::string> operands;
    for(const auto& child:next.history)if(child.mech_keyway_binding.parent_id==container&&
            child.feature_kind!=document::FeatureKind::Sketch)operands.push_back(child.id);
    for(auto& dependent:next.history)if(dependent.feature_kind==document::FeatureKind::DerivedCopy){
        auto& sources=dependent.derived_copy.source_ids;
        if(const auto source=std::ranges::find(sources,container);source!=sources.end()){
            const auto index=source-sources.begin();sources.erase(source);
            sources.insert(sources.begin()+index,operands.begin(),operands.end());
        }
    }
    for(auto& child:next.history)if(child.mech_keyway_binding.parent_id==container&&child.mech_keyway_binding.role=="cut") {
        if(child.feature_kind==document::FeatureKind::Loft) {
            for(auto& owned:child.loft.sketches) {
                auto profile=sketcher::Sketch::from_serialized(owned.sketch_serialized);
                make_profile_editable(profile,parent->mech_keyway);owned.sketch_serialized=profile.serialized();
            }
        } else {
            auto profile=std::ranges::find(next.sketches,child.feature.sketch_id,&sketcher::Sketch::id);
            if(profile!=next.sketches.end())make_profile_editable(*profile,parent->mech_keyway);
        }
    }
    for(auto& c:next.history)if(c.mech_keyway_binding.parent_id==container)c.mech_keyway_binding={};
    std::erase_if(next.history,[&](const auto& c){return c.id==container;});
    graph.update_body(std::move(body));next.set_body_history(std::move(graph));
    commit_part_document_metadata(live,id,std::move(next));return true;
}
}

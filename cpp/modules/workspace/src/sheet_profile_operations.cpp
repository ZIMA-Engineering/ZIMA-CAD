#include <zima/workspace/sheet_profile_operations.hpp>

#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/metadata.hpp>
#include <zima/document/sheet_profile.hpp>
#include <zima/document/sketch_placement.hpp>
#include <zima/workspace/part_transactions.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>

namespace zima::workspace {
namespace {
using V=kernel::Vec3;

V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V value,double factor){return {value.x*factor,value.y*factor,value.z*factor};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V unit(V value) {
    const auto length=std::sqrt(dot(value,value));
    if(!std::isfinite(length)||length<1e-12)
        throw std::invalid_argument("Sheet Profile frame is degenerate.");
    return mul(value,1/length);
}
V direction(const sketcher::Sketch& sketch,const std::array<double,2>& value) {
    return unit(add(mul(sketch.x_axis(),value[0]),mul(sketch.y_axis(),value[1])));
}

void numeric_frame(document::HistoryContainer& feature,sketcher::Sketch& sketch,
        V origin,V x,V y,V z) {
    x=unit(x);y=unit(y);z=unit(z);
    const double ry=std::asin(std::clamp(-x.z,-1.,1.));
    const double rx=std::abs(std::cos(ry))>1e-9?std::atan2(y.z,z.z):0;
    const double rz=std::abs(std::cos(ry))>1e-9?std::atan2(x.y,x.x):
        std::atan2(-y.x,y.y);
    auto& p=feature.placement;p={};p.x=origin.x;p.y=origin.y;p.z=origin.z;
    p.absolute_rotation_x=p.rotation_x=p.rotation_offset_x=
        rx*180/std::numbers::pi;
    p.absolute_rotation_y=p.rotation_y=p.rotation_offset_y=
        ry*180/std::numbers::pi;
    p.absolute_rotation_z=p.rotation_z=p.rotation_offset_z=
        rz*180/std::numbers::pi;
    sketch.plane=sketcher::SketchPlane::XY;sketch.plane_auto=false;
    sketch.plane_reference_owner_id.clear();sketch.plane_offset=0;
    sketch.resolved_origin=origin;sketch.resolved_x_axis=x;
    sketch.resolved_y_axis=y;sketch.resolved_normal=z;
}

std::pair<double,double> width_interval(double width,
        document::ExtrusionDirection direction) {
    if(!std::isfinite(width)||width<.001||width>1000000)
        throw std::invalid_argument(
            "Sheet Profile width must be between 0.001 and 1000000 mm.");
    switch(direction) {
    case document::ExtrusionDirection::Forward:return {0,width};
    case document::ExtrusionDirection::Reverse:return {-width,0};
    case document::ExtrusionDirection::Symmetric:return {-width/2,width/2};
    }
    throw std::invalid_argument("Invalid Sheet Profile width direction.");
}

std::optional<std::pair<double,double>> master_width_interval(
        const document::PartDocument& document,
        const document::HistoryContainer& profile) {
    const auto* master=document.find_container(
        profile.sheet_profile.master_feature_id);
    if(!master||(master->feature_kind!=document::FeatureKind::Bend&&
                master->feature_kind!=document::FeatureKind::Flat))
        return std::nullopt;
    const auto sketch_id=master->feature_kind==document::FeatureKind::Bend?
        master->bend.sketch_id:master->flat.sketch_id;
    const auto sketch=std::ranges::find(document.sketches,sketch_id,
        &sketcher::Sketch::id);
    if(sketch==document.sketches.end())return std::nullopt;
    if(master->feature_kind==document::FeatureKind::Flat) {
        if(sketch->points.empty())return std::nullopt;
        const auto [minimum,maximum]=std::ranges::minmax_element(
            sketch->points,{},&sketcher::SketchPoint::x);
        if(maximum->x-minimum->x<.001)return std::nullopt;
        return std::pair{minimum->x,maximum->x};
    }
    const auto segment=std::ranges::find_if(sketch->segments,[](const auto& value) {
        return !value.construction;
    });
    if(segment==sketch->segments.end())return std::nullopt;
    const auto* first=sketch->find_point(segment->first_point_id);
    const auto* last=sketch->find_point(segment->second_point_id);
    if(!first||!last)return std::nullopt;
    const auto source=std::ranges::find(document.sketches,
        profile.sheet_profile.sketch_id,&sketcher::Sketch::id);
    if(source==document.sketches.end())return std::nullopt;
    const auto route=document::sheet_profile_route(*source,
        profile.sheet_profile.start_point_id);
    const auto span=std::ranges::find(route.spans,
        master->sheet_profile_binding.curve_id,
        &document::SheetProfileSpan::curve_id);
    if(span==route.spans.end()||
       span->kind!=document::SheetProfileSpanKind::Bend)return std::nullopt;
    const double map=span->turn_degrees>0?-1.:1.;
    std::array values{first->x*map,last->x*map};
    std::ranges::sort(values);
    if(values[1]-values[0]<.001)return std::nullopt;
    return std::pair{values[0],values[1]};
}

void set_bend_width(sketcher::Sketch& sketch,double first,double last) {
    if(last-first<.001)throw std::invalid_argument("Sheet Profile width is too small.");
    if(sketch.points.empty()&&sketch.segments.empty())
        document::initialize_bend_start_profile(sketch,last-first);
    const auto segment=std::ranges::find_if(sketch.segments,[](const auto& value) {
        return !value.construction;
    });
    if(segment==sketch.segments.end()||
       std::ranges::count_if(sketch.segments,[](const auto& value) {
           return !value.construction;
       })!=1)
        throw std::invalid_argument(
            "Generated Sheet Profile Bend start Sketch was modified.");
    auto* a=sketch.find_point(segment->first_point_id);
    auto* b=sketch.find_point(segment->second_point_id);
    if(!a||!b)throw std::invalid_argument("Sheet Profile Bend width endpoint is missing.");
    a->x=first;a->y=0;b->x=last;b->y=0;
    for(const auto& [suffix,value]:std::array<std::pair<const char*,double>,2>{{
            {":position:first",first},{":position:last",last}}}) {
        const auto dimension=std::ranges::find(sketch.dimensions,
            sketch.id+suffix,&sketcher::SketchDimension::id);
        if(dimension==sketch.dimensions.end())
            throw std::invalid_argument("Sheet Profile Bend width dimension is missing.");
        dimension->value=value;dimension->solution_side=std::signbit(value)?-1:1;
    }
    sketch.validate();
}

void set_flat_profile(sketcher::Sketch& sketch,double first,double last,
        double length,bool width_driver) {
    if(last-first<.001||length<.001)
        throw std::invalid_argument("Sheet Profile Flat dimensions are too small.");
    sketch.points.clear();sketch.segments.clear();sketch.circles.clear();
    sketch.arcs.clear();sketch.ellipses.clear();sketch.elliptical_arcs.clear();
    sketch.bsplines.clear();sketch.texts.clear();sketch.symbols.clear();
    sketch.external_references.clear();sketch.constraints.clear();
    sketch.dimensions.clear();sketch.dimension_layouts.clear();
    sketch.corner_radii.clear();sketch.offsets.clear();sketch.curve_trims.clear();
    const std::array<std::pair<double,double>,4> values{{
        {first,0},{last,0},{last,length},{first,length}}};
    for(std::size_t index=0;index<values.size();++index) {
        auto point=sketcher::Sketch::create_point(values[index].first,
            values[index].second);
        point.id=sketch.id+":profile:point:"+std::to_string(index);
        sketch.points.push_back(std::move(point));
    }
    for(std::size_t index=0;index<values.size();++index) {
        auto segment=sketcher::Sketch::create_segment(
            sketch.points[index].id,sketch.points[(index+1)%values.size()].id);
        segment.id=sketch.id+":profile:edge:"+std::to_string(index);
        sketch.segments.push_back(std::move(segment));
    }
    const auto stabilize=[&](std::string id,const std::string& role) {
        const auto constraint=std::ranges::find(sketch.constraints,id,
            &sketcher::SketchConstraint::id);
        if(constraint==sketch.constraints.end())
            throw std::logic_error("Generated Flat constraint is missing.");
        constraint->id=sketch.id+role;
    };
    stabilize(sketch.add_point_on_line_constraint(
        sketch.points[0].id,"sketch_axis:x"),
        ":profile:constraint:base:first");
    stabilize(sketch.add_point_on_line_constraint(
        sketch.points[1].id,"sketch_axis:x"),
        ":profile:constraint:base:last");
    stabilize(sketch.add_point_pair_constraint(sketch.points[1].id,
        sketch.points[2].id,sketcher::ConstraintKind::Vertical),
        ":profile:constraint:right");
    stabilize(sketch.add_point_pair_constraint(sketch.points[2].id,
        sketch.points[3].id,sketcher::ConstraintKind::Horizontal),
        ":profile:constraint:top");
    stabilize(sketch.add_point_pair_constraint(sketch.points[3].id,
        sketch.points[0].id,sketcher::ConstraintKind::Vertical),
        ":profile:constraint:left");
    for(const auto& [index,suffix]:
            std::array<std::pair<std::size_t,const char*>,2>{{
                {0,":position:first"},{1,":position:last"}}}) {
        auto dimension=sketch.create_point_dimension("sketch_origin",
            sketch.points[index].id,sketcher::DimensionKind::DistanceX);
        dimension.id=sketch.id+suffix;dimension.suppressed=!width_driver;
        dimension.placement=std::array{values[index].first,index?8.:-8.};
        sketch.dimensions.push_back(std::move(dimension));
    }
    auto height=sketch.create_point_dimension(sketch.points[1].id,
        sketch.points[2].id,sketcher::DimensionKind::DistanceY);
    height.id=sketch.id+":profile:length";height.locked=true;
    height.suppressed=true;sketch.dimensions.push_back(std::move(height));
    sketch.validate();
}

void hide_generated_bend_dimensions(document::HistoryContainer& feature,
        sketcher::Sketch& start,bool width_driver) {
    for(auto& dimension:start.dimensions)
        if(dimension.id==start.id+":position:first"||
           dimension.id==start.id+":position:last")
            dimension.suppressed=!width_driver;
    for(auto& serialized:feature.bend.auxiliary_sketches) {
        auto sketch=sketcher::Sketch::from_serialized(serialized);
        for(auto& dimension:sketch.dimensions)dimension.suppressed=!width_driver;
        serialized=sketch.serialized();
    }
}

std::optional<double> referenced_sheet_thickness(
        const document::HistoryContainer& profile,
        const kernel::ViewerReferenceGeometry& geometry) {
    for(const auto& reference:profile.placement.references) {
        const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& value) {
            return value.reference.owner_id==reference.owner_id&&
                value.reference.semantic_key==reference.semantic_key&&
                value.reference.instance_path==reference.instance_path;
        });
        if(edge==geometry.edges.end())continue;
        for(const auto& side:edge->edge_treatment_side_references)
            if(std::isfinite(side.sheet_thickness)&&side.sheet_thickness>=.001)
                return side.sheet_thickness;
    }
    return std::nullopt;
}

struct Child {
    document::HistoryContainer feature;
    sketcher::Sketch sketch;
};

Child child_for(const document::PartDocument& before,
        const document::HistoryContainer& profile,
        const document::SheetProfileSpan& span,
        const std::map<std::string,const document::HistoryContainer*>& old) {
    Child result;
    if(const auto found=old.find(span.curve_id);found!=old.end()) {
        result.feature=*found->second;
        const auto sketch_id=result.feature.feature_kind==document::FeatureKind::Bend?
            result.feature.bend.sketch_id:result.feature.flat.sketch_id;
        const auto sketch=std::ranges::find(before.sketches,sketch_id,
            &sketcher::Sketch::id);
        if(sketch==before.sketches.end()||sketch->owner_container_id!=result.feature.id)
            throw std::invalid_argument("Sheet Profile child Sketch is missing.");
        result.sketch=*sketch;
    } else {
        result.feature=document::PartDocument::create_sketch_container();
        result.sketch=sketcher::Sketch::create_default();
        result.sketch.owner_container_id=result.feature.id;
        result.feature.name=span.kind==document::SheetProfileSpanKind::Bend?
            "Ohyb":"Tabule";
    }
    result.feature.feature_kind=span.kind==document::SheetProfileSpanKind::Bend?
        document::FeatureKind::Bend:document::FeatureKind::Flat;
    result.feature.combine_mode=document::CombineMode::Add;
    result.feature.suppressed=profile.suppressed;
    result.feature.sheet_profile_binding={profile.id,span.curve_id,span.reversed};
    result.sketch.owner_container_id=result.feature.id;
    result.sketch.name=result.feature.name;
    return result;
}

std::vector<std::size_t> executable_order(std::size_t count,std::size_t bend) {
    std::vector<std::size_t> result;result.reserve(count);result.push_back(bend);
    for(auto index=bend;index>0;--index)result.push_back(index-1);
    for(auto index=bend+1;index<count;++index)result.push_back(index);
    return result;
}

std::array<double,2> rotate2(std::array<double,2> value,double angle) {
    const auto c=std::cos(angle),s=std::sin(angle);
    return {value[0]*c-value[1]*s,value[0]*s+value[1]*c};
}

void transform_route_tail(sketcher::Sketch& driver,
        const document::SheetProfileRoute& route,
        std::array<double,2> old_origin,
        std::array<double,2> new_origin,double rotation) {
    std::set<std::string> point_ids;
    if(!route.spans.empty())point_ids.insert(route.spans.front().end_point_id);
    for(std::size_t index=1;index<route.spans.size();++index) {
        const auto& span=route.spans[index];
        point_ids.insert(span.start_point_id);point_ids.insert(span.end_point_id);
        if(span.kind==document::SheetProfileSpanKind::Bend) {
            const auto arc=std::ranges::find(route.evaluated.arcs,span.curve_id,
                &sketcher::SketchArc::id);
            if(arc!=route.evaluated.arcs.end())
                point_ids.insert(arc->center_point_id);
        }
    }
    for(const auto& id:point_ids) {
        const auto* source=route.evaluated.find_point(id);
        auto* target=driver.find_point(id);
        if(!source||!target)throw std::invalid_argument(
            "Sheet Profile route point is missing during master edit.");
        const auto shifted=rotate2(
            {source->x-old_origin[0],source->y-old_origin[1]},rotation);
        target->x=new_origin[0]+shifted[0];
        target->y=new_origin[1]+shifted[1];
    }
}

void update_master_flat_route(sketcher::Sketch& driver,
        const document::SheetProfileRoute& route,
        const sketcher::Sketch& edited) {
    if(route.spans.empty()||route.spans.front().kind!=
            document::SheetProfileSpanKind::Flat)
        throw std::invalid_argument("Sheet Profile master is not a Flat span.");
    if(edited.points.size()!=4)
        throw std::invalid_argument(
            "A generated Sheet Profile master Flat must remain rectangular.");
    const auto [minimum,maximum]=std::ranges::minmax_element(
        edited.points,{},&sketcher::SketchPoint::y);
    const double length=maximum->y-minimum->y;
    if(!std::isfinite(length)||length<.001)
        throw std::invalid_argument(
            "Sheet Profile master Flat length is too small.");
    const auto& span=route.spans.front();
    const std::array new_end{
        span.start[0]+span.start_tangent[0]*length,
        span.start[1]+span.start_tangent[1]*length};
    transform_route_tail(driver,route,span.end,new_end,0);
    auto* end=driver.find_point(span.end_point_id);
    if(!end)throw std::invalid_argument(
        "Sheet Profile master Flat endpoint is missing.");
    end->x=new_end[0];end->y=new_end[1];
}

void update_master_bend_route(sketcher::Sketch& driver,
        const document::SheetProfileRoute& route,
        const document::HistoryContainer& parent,
        const document::HistoryContainer& edited,double thickness) {
    if(route.spans.empty()||route.spans.front().kind!=
            document::SheetProfileSpanKind::Bend)
        throw std::invalid_argument("Sheet Profile master is not a Bend span.");
    const auto& span=route.spans.front();
    const int turn=span.turn_degrees>0?1:-1;
    const bool material_toward_center=
        parent.sheet_profile.material_side==turn;
    const double radius=edited.bend.radius+
        (material_toward_center?thickness:0.);
    if(!std::isfinite(radius)||radius<.001||
       !std::isfinite(edited.bend.angle_degrees)||
       edited.bend.angle_degrees<=0||edited.bend.angle_degrees>180)
        throw std::invalid_argument(
            "Sheet Profile master Bend radius or angle is invalid.");
    const double sweep=static_cast<double>(turn)*
        edited.bend.angle_degrees*std::numbers::pi/180.;
    const std::array radial{
        static_cast<double>(turn)*span.start_tangent[1],
        static_cast<double>(-turn)*span.start_tangent[0]};
    const std::array center{
        span.start[0]-radial[0]*radius,
        span.start[1]-radial[1]*radius};
    const auto end_radial=rotate2(radial,sweep);
    const std::array new_end{
        center[0]+end_radial[0]*radius,
        center[1]+end_radial[1]*radius};
    const double old_sweep=span.turn_degrees*std::numbers::pi/180.;
    transform_route_tail(driver,route,span.end,new_end,sweep-old_sweep);
    auto arc=std::ranges::find(driver.arcs,span.curve_id,
        &sketcher::SketchArc::id);
    if(arc==driver.arcs.end())throw std::invalid_argument(
        "Sheet Profile master Bend arc is missing.");
    auto* center_point=driver.find_point(arc->center_point_id);
    if(!center_point)throw std::invalid_argument(
        "Sheet Profile master Bend center is missing.");
    center_point->x=center[0];center_point->y=center[1];arc->radius=radius;
    const auto* raw_start=driver.find_point(arc->start_point_id);
    const auto* raw_end=driver.find_point(arc->end_point_id);
    if(!raw_start||!raw_end)throw std::invalid_argument(
        "Sheet Profile master Bend endpoint is missing.");
    arc->start_angle=std::atan2(raw_start->y-center[1],
        raw_start->x-center[0]);
    arc->end_angle=std::atan2(raw_end->y-center[1],
        raw_end->x-center[0]);
    while(arc->end_angle<=arc->start_angle)
        arc->end_angle+=2*std::numbers::pi;
    if(arc->end_angle-arc->start_angle>std::numbers::pi+1e-8)
        throw std::invalid_argument(
            "Sheet Profile master Bend edit reversed its authored arc.");
}
}

PreparedSheetProfile prepare_sheet_profile(const document::PartDocument& before,
        document::HistoryContainer profile,sketcher::Sketch driver,
        kernel::ViewerReferenceGeometry references) {
    const auto* existing=before.find_container(profile.id);
    if(existing) {
        const auto old=std::ranges::find(before.sketches,
            existing->sheet_profile.sketch_id,&sketcher::Sketch::id);
        if(existing->feature_kind!=document::FeatureKind::SheetProfile||
           existing->feature_id!=profile.feature_id||
           existing->feature_parent_id!=profile.feature_parent_id||
           existing->container_origin!=profile.container_origin||
           old==before.sketches.end()||driver.id!=old->id)
            throw std::invalid_argument(
                "Sheet Profile editing must preserve its container and Sketch identities.");
    } else if(profile.id.empty()||profile.feature_id.empty()||
              before.find_container(driver.owner_container_id))
        throw std::invalid_argument("A new Sheet Profile requires new identities.");
    if(profile.feature_kind!=document::FeatureKind::SheetProfile||
       profile.sheet_profile.sketch_id!=driver.id||
       driver.owner_container_id!=profile.id)
        throw std::invalid_argument("Sheet Profile must own its one driving Sketch.");
    document::validate_native_metadata_text(profile.name);
    if(profile.name.empty())throw std::invalid_argument("Specify a Sheet Profile name.");
    document::normalize_container_front_references(profile.placement.references);
    if(profile.sheet_profile.material_side!=1&&profile.sheet_profile.material_side!=-1)
        throw std::invalid_argument("Invalid Sheet Profile material side.");

    // Resolve only the ordinary parent placement before reading the route.
    // This carrier has no kernel operation and cannot publish any scene.
    document::PartDocument carrier;carrier.document_id=before.document_id;
    carrier.history={profile};carrier.sketches={driver};
    append_reference_geometry(references,before.origin_viewer_mesh().original_references);
    append_reference_geometry(references,
        before.construction_viewer_mesh().original_references);
    const auto inherited=referenced_sheet_thickness(profile,references);
    carrier.resolve_constructions(std::move(references));
    profile=std::move(carrier.history.front());
    driver=std::move(carrier.sketches.front());
    auto route=document::sheet_profile_route(driver,
        profile.sheet_profile.start_point_id);
    profile.sheet_profile.start_point_id=route.start_point_id;
    const std::size_t master_index=0;

    auto interval=width_interval(profile.sheet_profile.initial_width,
        profile.sheet_profile.width_direction);
    if(existing)if(const auto authored=master_width_interval(before,*existing))
        interval=*authored;
    const auto [width_first,width_last]=interval;
    profile.sheet_profile.initial_width=width_last-width_first;
    const auto defaults=document::sheet_metal_defaults(before);
    profile.sheet_profile.sheet_attachment=
        !profile.sheet_profile.thickness_override&&inherited.has_value();
    const double thickness=profile.sheet_profile.thickness_override?
        profile.sheet_profile.thickness:
        inherited.value_or(defaults.thickness_mm.value_or(1.0));
    profile.sheet_profile.thickness=thickness;
    document::validate_sheet_metal_defaults({thickness,defaults.k_factor});

    std::map<std::string,const document::HistoryContainer*> old_children;
    std::set<std::string> old_ids;
    if(existing) {
        old_ids.insert(existing->id);
        for(const auto& feature:before.history)
            if(feature.sheet_profile_binding.parent_id==existing->id) {
                old_ids.insert(feature.id);
                if(!old_children.emplace(feature.sheet_profile_binding.curve_id,
                        &feature).second)
                    throw std::invalid_argument(
                        "Sheet Profile contains duplicate child bindings.");
            }
    }
    std::vector<Child> logical;logical.reserve(route.spans.size());
    const auto width_axis=unit(driver.normal());
    for(std::size_t index=0;index<route.spans.size();++index) {
        const auto& span=route.spans[index];
        auto child=child_for(before,profile,span,old_children);
        const auto tangent=direction(driver,span.start_tangent);
        const auto left=unit(cross(width_axis,tangent));
        const auto start=driver.world_point(span.start[0],span.start[1]);
        if(span.kind==document::SheetProfileSpanKind::Flat) {
            child.feature.flat.sketch_id=child.sketch.id;
            child.feature.flat.sheet_attachment=false;
            child.feature.flat.thickness_override=true;
            child.feature.flat.thickness=thickness;
            child.feature.flat.direction=profile.sheet_profile.material_side>0?
                document::ExtrusionDirection::Forward:
                document::ExtrusionDirection::Reverse;
            numeric_frame(child.feature,child.sketch,start,width_axis,tangent,left);
            set_flat_profile(child.sketch,width_first,width_last,span.length,
                index==master_index);
            static_cast<void>(document::flat_request(child.feature,child.sketch,defaults));
        } else {
            const int turn=span.turn_degrees>0?1:-1;
            const bool material_toward_center=
                profile.sheet_profile.material_side==turn;
            const double inner_radius=material_toward_center?
                span.radius-thickness:span.radius;
            if(inner_radius<-1e-8)
                throw std::invalid_argument(
                    "Sheet Profile thickness exceeds an outer Bend radius.");
            const auto material=mul(left,
                static_cast<double>(profile.sheet_profile.material_side));
            const auto outer_start=material_toward_center?start:
                add(start,mul(material,thickness));
            const auto inward=mul(left,static_cast<double>(turn));
            const auto along=mul(width_axis,static_cast<double>(-turn));
            child.feature.bend.sketch_id=child.sketch.id;
            child.feature.bend.sheet_attachment=false;
            child.feature.bend.thickness_override=true;
            child.feature.bend.thickness=thickness;
            child.feature.bend.radius_follows_thickness=false;
            child.feature.bend.radius=std::max(0.,inner_radius);
            child.feature.bend.angle_degrees=std::abs(span.turn_degrees);
            if(index==master_index) {
                child.feature.value_locks.erase("radius");
                child.feature.value_locks.erase("angle");
            } else {
                child.feature.value_locks.insert("radius");
                child.feature.value_locks.insert("angle");
            }
            numeric_frame(child.feature,child.sketch,outer_start,along,inward,tangent);
            std::array local_width{width_first*dot(width_axis,along),
                width_last*dot(width_axis,along)};
            std::ranges::sort(local_width);
            set_bend_width(child.sketch,local_width[0],local_width[1]);
            document::prepare_bend_sketches(child.feature,child.sketch,defaults);
            hide_generated_bend_dimensions(child.feature,child.sketch,
                index==master_index);
            static_cast<void>(document::bend_request(child.feature,
                child.sketch,defaults));
        }
        logical.push_back(std::move(child));
    }
    profile.sheet_profile.master_feature_id=logical[master_index].feature.id;

    const auto* owner=existing?before.body_owner_for_object(existing->id):
        before.body_history.find(before.body_history.active_body_id());
    if(!owner)throw std::invalid_argument(
        "Activate an editable Body before creating a Sheet Profile.");
    if(owner->derived_copy)throw std::invalid_argument(
        "A derived Body cannot be edited directly.");
    if(owner->scope.id!=before.body_history.active_body_id())
        throw std::invalid_argument("Activate the Body owning this Sheet Profile.");
    auto graph=before.body_history;auto body=*owner;
    std::size_t insertion=body.cursor;
    if(existing) {
        const auto parent=std::ranges::find(body.entries,existing->id,
            &document::PartHistoryEntry::id);
        if(parent==body.entries.end())
            throw std::invalid_argument("Sheet Profile is missing from Body history.");
        insertion=static_cast<std::size_t>(parent-body.entries.begin());
    }
    std::size_t removed_before{};
    for(std::size_t index=0;index<body.entries.size();++index)
        if(old_ids.contains(body.entries[index].id)&&index<body.cursor)
            ++removed_before;
    std::erase_if(body.entries,[&](const auto& entry) {
        return old_ids.contains(entry.id);
    });
    body.cursor-=std::min(body.cursor,removed_before);
    const auto order=executable_order(logical.size(),master_index);
    std::vector<document::PartHistoryEntry> group{{
        document::PartHistoryKind::Feature,profile.id}};
    for(const auto index:order)group.push_back({
        document::PartHistoryKind::Feature,logical[index].feature.id});
    body.entries.insert(body.entries.begin()+static_cast<std::ptrdiff_t>(insertion),
        group.begin(),group.end());
    if(insertion<=body.cursor)body.cursor+=group.size();
    graph.update_body(std::move(body));

    auto next=before;
    std::erase_if(next.history,[&](const auto& feature) {
        return old_ids.contains(feature.id);
    });
    std::erase_if(next.sketches,[&](const auto& sketch) {
        return old_ids.contains(sketch.owner_container_id);
    });
    next.history.push_back(std::move(profile));next.sketches.push_back(std::move(driver));
    for(const auto index:order) {
        next.history.push_back(logical[index].feature);
        next.sketches.push_back(logical[index].sketch);
    }
    next.set_body_history(std::move(graph));
    next.resolve_constructions();
    next.validate_sheet_profiles();
    PreparedSheetProfile result{std::move(next),{}};
    for(const auto& child:logical)result.logical_children.push_back(child.feature.id);
    return result;
}

PreparedSheetProfile prepare_sheet_profile_master_edit(
        const document::PartDocument& before,
        document::HistoryContainer edited,sketcher::Sketch edited_sketch,
        kernel::ViewerReferenceGeometry references) {
    const auto* existing=before.find_container(edited.id);
    if(!existing||existing->sheet_profile_binding.parent_id.empty()||
       existing->feature_kind!=edited.feature_kind||
       (edited.feature_kind!=document::FeatureKind::Flat&&
        edited.feature_kind!=document::FeatureKind::Bend))
        throw std::invalid_argument(
            "Only a generated Sheet Profile Flat or Bend can use master editing.");
    const auto* parent=before.find_container(
        existing->sheet_profile_binding.parent_id);
    if(!parent||parent->feature_kind!=document::FeatureKind::SheetProfile||
       parent->sheet_profile.master_feature_id!=existing->id)
        throw std::invalid_argument(
            "Edit this generated feature through its parent Profile Sketch.");
    const auto old_sketch=std::ranges::find(before.sketches,
        edited_sketch.id,&sketcher::Sketch::id);
    if(old_sketch==before.sketches.end()||
       old_sketch->owner_container_id!=existing->id||
       edited.feature_id!=existing->feature_id||
       edited.feature_parent_id!=existing->feature_parent_id||
       edited.container_origin!=existing->container_origin)
        throw std::invalid_argument(
            "Sheet Profile master edit must preserve native identities.");

    auto intermediate=before;
    // Placement and material direction belong to the parent route. The native
    // master editor contributes its classic width/radius/angle/thickness/K
    // values without detaching the child from that route.
    edited.placement=existing->placement;
    edited.sheet_profile_binding=existing->sheet_profile_binding;
    *intermediate.find_container(edited.id)=edited;
    *std::ranges::find(intermediate.sketches,edited_sketch.id,
        &sketcher::Sketch::id)=edited_sketch;
    auto* pending_parent=intermediate.find_container(parent->id);
    auto driver=std::ranges::find(intermediate.sketches,
        pending_parent->sheet_profile.sketch_id,&sketcher::Sketch::id);
    if(driver==intermediate.sketches.end())throw std::invalid_argument(
        "Sheet Profile driver Sketch is missing.");
    const auto route=document::sheet_profile_route(*driver,
        pending_parent->sheet_profile.start_point_id);
    const double old_thickness=existing->feature_kind==
            document::FeatureKind::Flat
        ?existing->flat.thickness:existing->bend.thickness;
    const double thickness=edited.feature_kind==document::FeatureKind::Flat
        ?edited.flat.thickness:edited.bend.thickness;
    if(!std::isfinite(thickness)||thickness<.001)
        throw std::invalid_argument(
            "Sheet Profile thickness must be at least 0.001 mm.");
    if(std::abs(thickness-old_thickness)>1e-10) {
        pending_parent->sheet_profile.thickness=thickness;
        pending_parent->sheet_profile.thickness_override=true;
    }
    if(edited.feature_kind==document::FeatureKind::Flat)
        update_master_flat_route(*driver,route,edited_sketch);
    else update_master_bend_route(*driver,route,*pending_parent,edited,
        pending_parent->sheet_profile.thickness_override?
            pending_parent->sheet_profile.thickness:old_thickness);
    driver->validate();
    const auto updated=document::sheet_profile_route(*driver,
        pending_parent->sheet_profile.start_point_id);
    if(updated.spans.empty()||updated.spans.front().curve_id!=
            existing->sheet_profile_binding.curve_id)
        throw std::invalid_argument(
            "Sheet Profile constraints prevented the master edit.");
    return prepare_sheet_profile(intermediate,*pending_parent,*driver,
        std::move(references));
}

bool commit_sheet_profile(Workspace& live,const kernel::OcctKernel& kernel,
        const std::string& id,document::HistoryContainer profile,
        sketcher::Sketch driver) {
    auto* state=live.open_part(id);
    if(!state)throw std::invalid_argument("Sheet Profile is available only in a Part.");
    const auto& before=state->session.document();
    auto references=construction_reference_source_geometry(
        state->session.calculated_boundaries());
    auto prepared=prepare_sheet_profile(before,std::move(profile),
        std::move(driver),std::move(references));
    PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,
        prepared.document,&state->session.calculated_boundaries(),policy);
    if(prepared.document.serialized()==before.serialized()) {
        state->session.update_calculated_boundaries(std::move(calculated));
        return false;
    }
    commit_part_document(live,id,std::move(prepared.document),
        std::move(calculated));
    return true;
}

bool commit_sheet_profile_master_edit(Workspace& live,
        const kernel::OcctKernel& kernel,const std::string& id,
        document::HistoryContainer edited,sketcher::Sketch edited_sketch) {
    auto* state=live.open_part(id);
    if(!state)throw std::invalid_argument(
        "Sheet Profile is available only in a Part.");
    const auto& before=state->session.document();
    auto references=construction_reference_source_geometry(
        state->session.calculated_boundaries());
    auto prepared=prepare_sheet_profile_master_edit(before,std::move(edited),
        std::move(edited_sketch),std::move(references));
    PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,
        prepared.document,&state->session.calculated_boundaries(),policy);
    if(prepared.document.serialized()==before.serialized()) {
        state->session.update_calculated_boundaries(std::move(calculated));
        return false;
    }
    commit_part_document(live,id,std::move(prepared.document),
        std::move(calculated));
    return true;
}

bool explode_sheet_profile(Workspace& live,const kernel::OcctKernel& kernel,
        const std::string& id,const std::string& profile_id) {
    auto* state=live.open_part(id);
    if(!state)throw std::invalid_argument("Sheet Profile is available only in a Part.");
    const auto& before=state->session.document();
    const auto* profile=before.find_container(profile_id);
    if(!profile||profile->feature_kind!=document::FeatureKind::SheetProfile)
        throw std::invalid_argument("Sheet Profile does not exist.");
    const auto* owner=before.body_owner_for_object(profile_id);
    if(!owner||owner->scope.id!=before.body_history.active_body_id())
        throw std::invalid_argument("Activate the Body owning this Sheet Profile.");
    auto next=before;
    for(auto& feature:next.history)
        if(feature.sheet_profile_binding.parent_id==profile_id)
            feature.sheet_profile_binding={};
    std::erase_if(next.history,[&](const auto& feature) {
        return feature.id==profile_id;
    });
    std::erase_if(next.sketches,[&](const auto& sketch) {
        return sketch.owner_container_id==profile_id;
    });
    auto graph=next.body_history;auto body=*graph.find(owner->scope.id);
    const auto row=std::ranges::find(body.entries,profile_id,
        &document::PartHistoryEntry::id);
    if(row==body.entries.end())
        throw std::invalid_argument("Sheet Profile is missing from Body history.");
    const auto index=static_cast<std::size_t>(row-body.entries.begin());
    body.entries.erase(row);if(index<body.cursor)--body.cursor;
    graph.update_body(std::move(body));next.set_body_history(std::move(graph));
    PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,next,
        &state->session.calculated_boundaries(),policy);
    commit_part_document(live,id,std::move(next),std::move(calculated));
    return true;
}

} // namespace zima::workspace

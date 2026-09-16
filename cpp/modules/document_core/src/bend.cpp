#include <zima/document/bend.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace zima::document {
namespace {
using V=kernel::Vec3;
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V scale(V a,double s){return {a.x*s,a.y*s,a.z*s};}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct BendFrame {
    V first,last,along,inward,normal,anchor;
    double width{},first_coordinate{};
    std::string segment,first_point,last_point;
};
BendFrame frame(const HistoryContainer& feature,const sketcher::Sketch& sketch) {
    if(feature.feature_kind!=FeatureKind::Bend||feature.combine_mode!=CombineMode::Add)
        throw std::invalid_argument("Bend can only add material in a Part.");
    if(sketch.id!=feature.bend.sketch_id||sketch.owner_container_id!=feature.id)
        throw std::invalid_argument("Bend requires its own Sketch.");
    sketch.validate();
    const auto unsupported=[](const auto& curves){return std::ranges::any_of(curves,[](const auto& c){return !c.construction;});};
    if(unsupported(sketch.circles)||unsupported(sketch.arcs)||unsupported(sketch.ellipses)||
        unsupported(sketch.elliptical_arcs)||unsupported(sketch.bsplines)||!sketch.texts.empty()||
        std::ranges::count_if(sketch.segments,[](const auto& s){return !s.construction;})!=1)
        throw std::invalid_argument("Bend requires exactly one non-construction straight segment.");
    const auto& segment=*std::ranges::find_if(sketch.segments,[](const auto& s){return !s.construction;});
    const auto* first=sketch.find_point(segment.first_point_id);const auto* last=sketch.find_point(segment.second_point_id);
    if(!first||!last)throw std::invalid_argument("Bend segment endpoint is missing.");
    BendFrame f;f.first=sketch.world_point(first->x,first->y);f.last=sketch.world_point(last->x,last->y);
    const auto direction=add(f.last,scale(f.first,-1));f.width=std::hypot(direction.x,direction.y,direction.z);
    if(!std::isfinite(f.width)||f.width<.001)throw std::invalid_argument("Bend width must be at least 0.001 mm.");
    f.along=scale(direction,1/f.width);f.normal=sketch.resolved_normal;f.inward=cross(f.normal,f.along);
    f.first_coordinate=dot(add(f.first,scale(sketch.resolved_origin,-1)),f.along);
    f.anchor=add(f.first,scale(f.along,-f.first_coordinate));
    f.segment=segment.id;f.first_point=segment.first_point_id;f.last_point=segment.second_point_id;return f;
}
std::string child(const HistoryContainer& feature,const std::string& role,const std::string& parent) {
    return "bend:"+feature.feature_id+":"+role+":from:"+std::to_string(parent.size())+":"+parent;
}
template<class Request> void profile(Request& request,const HistoryContainer& feature,const BendFrame& f,double thickness) {
    request.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{f.first,f.last,
        add(f.last,scale(f.inward,thickness)),add(f.first,scale(f.inward,thickness))}};
    // The same authored section and ancestry are used in both states. OCCT
    // only locates these identities, including the Start/End region children.
    request.profile_region_id=child(feature,"section",f.segment);
    request.outer_boundary_id=child(feature,"boundary",f.segment);
    request.outer_edge_source_ids={child(feature,"outer",f.segment),child(feature,"last",f.last_point),
        child(feature,"inner",f.segment),child(feature,"first",f.first_point)};
    request.outer_vertex_source_ids={child(feature,"outer",f.first_point),child(feature,"outer",f.last_point),
        child(feature,"inner",f.last_point),child(feature,"inner",f.first_point)};
}
sketcher::SketchDimension& difference(sketcher::Sketch& sketch,bool last) {
    const auto id=sketch.id+(last?":difference:last":":difference:first");
    const auto found=std::ranges::find(sketch.dimensions,id,&sketcher::SketchDimension::id);
    if(found==sketch.dimensions.end())throw std::invalid_argument("Bend endpoint difference dimension is missing.");
    return *found;
}
double extension(const sketcher::SketchDimension& dimension,bool last) {
    return dimension.value*dimension.solution_side*(last?1:-1);
}
V path_point(const BendFrame& f,const BendParameters& p,double fraction) {
    const double angle=p.angle_degrees*std::numbers::pi/180;
    if(p.unbend)return add(f.anchor,scale(f.normal,angle*(p.radius+p.k_factor*p.thickness)*fraction));
    return add(add(f.anchor,scale(f.inward,(p.radius+p.thickness)*(1-std::cos(angle*fraction)))),
        scale(f.normal,(p.radius+p.thickness)*std::sin(angle*fraction)));
}
}
BendParameters resolved_bend_parameters(const HistoryContainer& feature,const SheetMetalDefaults& defaults) {
    auto p=feature.bend;
    if(!p.thickness_override)p.thickness=defaults.thickness_mm.value_or(1.0);
    if(!p.k_factor_override)p.k_factor=defaults.k_factor;
    if(p.radius_follows_thickness)p.radius=p.thickness;
    validate_sheet_metal_defaults({p.thickness,p.k_factor});
    if(!std::isfinite(p.radius)||p.radius<0||p.radius>1000000)
        throw std::invalid_argument("Bend inner radius must be between 0 and 1000000 mm.");
    if(!std::isfinite(p.angle_degrees)||p.angle_degrees<0||p.angle_degrees>180)
        throw std::invalid_argument("Bend angle must be between 0 and 180 degrees.");
    // Validate stored overrides as well, even while their controls are disabled.
    validate_sheet_metal_defaults({feature.bend.thickness,feature.bend.k_factor});
    return p;
}
void prepare_bend_sketches(HistoryContainer& feature,const sketcher::Sketch& start,const SheetMetalDefaults& defaults) {
    const auto p=resolved_bend_parameters(feature,defaults);const auto f=frame(feature,start);
    auto& data=feature.bend.auxiliary_sketches;
    auto path=data[0].empty()?sketcher::Sketch::create_default():sketcher::Sketch::from_serialized(data[0]);
    path.owner_container_id=feature.id;path.name="Trajektorie ohybu";
    path.plane_reference_owner_id=feature.id+":bend:path";path.plane_offset=0;
    path.resolved_origin=f.anchor;path.resolved_x_axis=f.normal;path.resolved_y_axis=f.inward;path.resolved_normal=scale(f.along,-1);
    // A zero angle contributes no material. Keep the last non-degenerate path
    // and its IDs; the property angle restores it without inventing new curves.
    const double angle=p.angle_degrees>0?p.angle_degrees:90.;
    if(data[0].empty()) {
        const double a=angle*std::numbers::pi/180,r=p.radius+p.thickness;
        static_cast<void>(path.add_arc(0,r,0,0,r*std::sin(a),r*(1-std::cos(a))));
        const auto arc=path.arcs.front();path.find_point(arc.start_point_id)->fixed=true;
        static_cast<void>(path.add_point_on_line_constraint(arc.center_point_id,"sketch_axis:y"));
        auto radius=path.create_arc_radius_dimension(arc.id);radius.id=path.id+":radius";radius.placement=std::array{r*.5,r*.5};
        auto turn=path.create_three_point_angle_dimension(arc.start_point_id,arc.center_point_id,arc.end_point_id);
        turn.id=path.id+":angle";turn.placement=std::array{r*1.3,r*.8};
        path.dimensions={std::move(radius),std::move(turn)};
    }
    if(path.arcs.size()!=1||path.arcs.front().construction||
        std::ranges::any_of(path.segments,[](const auto& c){return !c.construction;})||
        !path.circles.empty()||!path.ellipses.empty()||!path.elliptical_arcs.empty()||!path.bsplines.empty()||!path.texts.empty())
        throw std::invalid_argument("Bend path requires one circular arc.");
    if(p.angle_degrees>0) {
        auto& arc=path.arcs.front();const double a=p.angle_degrees*std::numbers::pi/180,r=p.radius+p.thickness;
        arc.radius=r;arc.start_angle=-std::numbers::pi/2;arc.end_angle=arc.start_angle+a;
        auto* center=path.find_point(arc.center_point_id);auto* first=path.find_point(arc.start_point_id);auto* last=path.find_point(arc.end_point_id);
        if(!center||!first||!last)throw std::invalid_argument("Bend path endpoint is missing.");
        center->x=0;center->y=r;first->x=0;first->y=0;last->x=r*std::sin(a);last->y=r*(1-std::cos(a));
        for(auto& d:path.dimensions) {
            if(d.id==path.id+":radius"){d.value=r;d.locked=p.radius_follows_thickness||feature.value_locks.contains("radius");}
            if(d.id==path.id+":angle"){d.value=p.angle_degrees;d.locked=feature.value_locks.contains("angle");}
        }
    }
    path.validate();data[0]=path.serialized();
    auto end=data[1].empty()?sketcher::Sketch::create_default():sketcher::Sketch::from_serialized(data[1]);
    end.owner_container_id=feature.id;end.name="Koncový profil ohybu";
    end.plane_reference_owner_id=feature.id+":bend:end";end.plane_offset=0;
    const double a=p.unbend?0:p.angle_degrees*std::numbers::pi/180;
    end.resolved_origin=path_point(f,p,1);end.resolved_x_axis=f.along;
    end.resolved_y_axis=add(scale(f.inward,std::cos(a)),scale(f.normal,-std::sin(a)));
    end.resolved_normal=cross(end.resolved_x_axis,end.resolved_y_axis);
    if(data[1].empty()) {
        for(bool last:{false,true}) {
            const double x=f.first_coordinate+(last?f.width:0);
            auto reference=sketcher::Sketch::create_point(x,0),point=sketcher::Sketch::create_point(x,0);
            reference.fixed=true;reference.construction=true;
            reference.id=end.id+(last?":reference:last":":reference:first");
            point.id=end.id+(last?":endpoint:last":":endpoint:first");
            end.points.push_back(reference);end.points.push_back(point);
            static_cast<void>(end.add_point_on_line_constraint(point.id,"sketch_axis:x"));
            auto dimension=end.create_point_dimension(reference.id,point.id,sketcher::DimensionKind::DistanceX);
            dimension.id=end.id+(last?":difference:last":":difference:first");dimension.solution_side=last?1:-1;
            dimension.placement=std::array{x,last?8.:-8.};end.dimensions.push_back(std::move(dimension));
        }
        end.segments.push_back(sketcher::Sketch::create_segment(end.id+":endpoint:first",end.id+":endpoint:last"));
        end.segments.push_back(sketcher::Sketch::create_segment(end.id+":reference:first",end.id+":reference:last",true));
    }
    for(bool last:{false,true}) {
        const double x=f.first_coordinate+(last?f.width:0);const auto& d=difference(end,last);
        auto* reference=end.find_point(end.id+(last?":reference:last":":reference:first"));
        auto* point=end.find_point(end.id+(last?":endpoint:last":":endpoint:first"));
        if(!reference||!point)throw std::invalid_argument("Bend end profile endpoint is missing.");
        reference->x=x;reference->y=0;reference->fixed=true;reference->construction=true;
        point->x=x+d.value*d.solution_side;point->y=0;
    }
    if(f.width+extension(difference(end,false),false)+extension(difference(end,true),true)<.001)
        throw std::invalid_argument("Bend end width must be at least 0.001 mm.");
    end.validate();data[1]=end.serialized();
}
std::array<double,2> bend_profile_extensions(const HistoryContainer& feature) {
    auto end=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[1]);
    return {extension(difference(end,false),false),extension(difference(end,true),true)};
}
void set_bend_profile_extensions(HistoryContainer& feature,double first,double last) {
    auto end=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[1]);
    for(bool side:{false,true}) {
        const double v=side?last:first;if(!std::isfinite(v))throw std::invalid_argument("Bend extension must be finite.");
        auto& d=difference(end,side);d.value=std::abs(v);d.solution_side=(side?1:-1)*(v<0?-1:1);
    }
    feature.bend.auxiliary_sketches[1]=end.serialized();
}
void accept_bend_sketch(HistoryContainer& feature,const sketcher::Sketch& start,std::size_t stage,
    sketcher::Sketch edited,const SheetMetalDefaults& defaults) {
    if(stage>=2)throw std::invalid_argument("Unknown Bend Sketch.");
    const auto before=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[stage]);
    if(edited.id!=before.id||edited.owner_container_id!=feature.id)throw std::invalid_argument("Bend Sketch identity changed.");
    edited.validate();auto next=feature;
    if(stage==0) {
        if(edited.arcs.size()!=1)throw std::invalid_argument("Bend path requires one circular arc.");
        const auto& arc=edited.arcs.front();const auto p=resolved_bend_parameters(feature,defaults);
        if(arc.id!=before.arcs.front().id||arc.center_point_id!=before.arcs.front().center_point_id||
            arc.start_point_id!=before.arcs.front().start_point_id||arc.end_point_id!=before.arcs.front().end_point_id)
            throw std::invalid_argument("Bend path must preserve its arc and endpoint identities.");
        const auto* center=edited.find_point(arc.center_point_id);const auto* first=edited.find_point(arc.start_point_id);
        if(!center||!first||std::abs(center->x)>1e-5||std::abs(center->y-arc.radius)>1e-5||
            std::hypot(first->x,first->y)>1e-5||std::abs(arc.start_angle+std::numbers::pi/2)>1e-5)
            throw std::invalid_argument("Bend path must start at its origin and follow its prepared direction.");
        for(const auto& role:{":radius",":angle"})
            if(std::ranges::find(edited.dimensions,edited.id+role,&sketcher::SketchDimension::id)==edited.dimensions.end())
                throw std::invalid_argument("Bend path driving dimension is missing.");
        if((p.radius_follows_thickness||feature.value_locks.contains("radius"))&&std::abs(arc.radius-p.radius-p.thickness)>1e-6)
            throw std::invalid_argument("Radius is locked or linked to thickness.");
        if(feature.value_locks.contains("angle")&&std::abs((arc.end_angle-arc.start_angle)*180/std::numbers::pi-p.angle_degrees)>1e-6)
            throw std::invalid_argument("Bend angle is locked.");
        next.bend.radius=arc.radius-p.thickness;
        next.bend.angle_degrees=(arc.end_angle-arc.start_angle)*180/std::numbers::pi;
    } else {
        if(std::ranges::count_if(edited.segments,[](const auto& s){return !s.construction;})!=1||
            !edited.arcs.empty()||!edited.circles.empty()||!edited.bsplines.empty()||!edited.ellipses.empty()||!edited.elliptical_arcs.empty())
            throw std::invalid_argument("Bend end profile requires one straight segment.");
        for(bool last:{false,true})static_cast<void>(difference(edited,last));
        const auto& segment=*std::ranges::find_if(edited.segments,[](const auto& s){return !s.construction;});
        const auto& previous=*std::ranges::find_if(before.segments,[](const auto& s){return !s.construction;});
        if(segment.id!=previous.id||segment.first_point_id!=previous.first_point_id||segment.second_point_id!=previous.second_point_id)
            throw std::invalid_argument("Bend end profile must preserve its segment and endpoint identities.");
        for(const auto& point:edited.points)if(!point.construction&&std::abs(point.y)>1e-5)
            throw std::invalid_argument("Bend end profile cannot rotate independently of the trajectory.");
    }
    next.bend.auxiliary_sketches[stage]=edited.serialized();prepare_bend_sketches(next,start,defaults);
    feature=std::move(next);
}
kernel::FeatureGroupRequest bend_request(const HistoryContainer& input,const sketcher::Sketch& sketch,const SheetMetalDefaults& defaults) {
    auto feature=input;prepare_bend_sketches(feature,sketch,defaults);
    const auto p=resolved_bend_parameters(feature,defaults);const auto f=frame(feature,sketch);
    const double length=p.angle_degrees*std::numbers::pi/180*(p.radius+p.k_factor*p.thickness);
    kernel::FeatureGroupRequest group;
    const auto axis_start=add(f.first,p.unbend?scale(f.normal,length*.5):scale(f.inward,p.radius+p.thickness));
    group.axes.push_back({add(axis_start,scale(f.along,f.width*.5)),f.along,f.width+2,
        {feature.id,child(feature,"axis",f.segment),{}},p.unbend?"Osa ohybu":"Osa rotace ohybu"});
    if(p.angle_degrees==0)return group; // Explicit zero-material history boundary.
    if(p.unbend&&length<=0)throw std::invalid_argument("Unbend requires a positive developed length (R + K*t).");
    const auto extension=bend_profile_extensions(feature);
    if(!p.unbend&&p.radius==0&&(std::abs(extension[0])>1e-9||std::abs(extension[1])>1e-9))
        throw std::invalid_argument("Zero inner radius currently requires matching start and end profiles.");
    const auto path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    const auto& arc=path.arcs.front();kernel::Sweep3DRequest request;
    request.linear_tolerance=1e-7;
    request.path_points={path_point(f,p,0),path_point(f,p,1)};
    request.path_point_ids={arc.start_point_id,arc.end_point_id};
    kernel::Sweep3DRequest::PathSegment route;route.source_id=arc.id;
    route.start=request.path_points.front();route.end=request.path_points.back();
    if(!p.unbend)route.arc_midpoint=path_point(f,p,.5);
    request.path_segments.push_back(std::move(route));
    for(bool last:{false,true}) {
        const double a=last&&!p.unbend?p.angle_degrees*std::numbers::pi/180:0;
        const auto inward=add(scale(f.inward,std::cos(a)),scale(f.normal,-std::sin(a)));
        const auto normal=cross(f.along,inward);const auto origin=request.path_points[last?1:0];
        const double x1=f.first_coordinate-(last?extension[0]:0),x2=f.first_coordinate+f.width+(last?extension[1]:0);
        BendFrame section_frame=f;section_frame.first=add(origin,scale(f.along,x1));section_frame.last=add(origin,scale(f.along,x2));section_frame.inward=inward;
        kernel::ExtrusionRequest source;profile(source,feature,section_frame,p.thickness);
        kernel::Sweep3DRequest::Section section;section.profile_id=source.profile_region_id;
        section.point_id=request.path_point_ids[last?1:0];section.point_index=last?1:0;section.profile_normal=normal;
        section.profile.region_id=source.profile_region_id;section.profile.outer_boundary_id=source.outer_boundary_id;
        section.profile.outer_profile=std::move(source.outer_profile);
        section.profile.outer_edge_source_ids=std::move(source.outer_edge_source_ids);
        section.profile.outer_vertex_source_ids=std::move(source.outer_vertex_source_ids);
        request.sections.push_back(std::move(section));
    }
    group.children.emplace_back(std::move(request));
    return group;
}
kernel::ViewerMesh bend_preview(const HistoryContainer& input,const sketcher::Sketch& sketch,const SheetMetalDefaults& defaults) {
    auto feature=input;prepare_bend_sketches(feature,sketch,defaults);
    const auto request=bend_request(feature,sketch,defaults);const auto p=resolved_bend_parameters(feature,defaults);const auto f=frame(feature,sketch);
    const auto extension=bend_profile_extensions(feature);
    kernel::ViewerMesh mesh;mesh.axes=request.axes;
    const double angle=p.angle_degrees*std::numbers::pi/180;
    const double developed=angle*(p.radius+p.k_factor*p.thickness);
    const auto point=[&](double along,bool inner,double fraction) {
        along+=(along==0?-extension[0]:extension[1])*fraction;
        auto result=add(f.first,scale(f.along,along));
        if(p.unbend)return add(add(result,scale(f.inward,inner?p.thickness:0)),scale(f.normal,developed*fraction));
        const double r=p.radius+(inner?0:p.thickness),a=angle*fraction;
        return add(add(result,scale(f.inward,p.radius+p.thickness-r*std::cos(a))),scale(f.normal,r*std::sin(a)));
    };
    const auto edge=[&](std::string role,std::vector<V> points){kernel::ViewerEdge e;e.reference={feature.id,"preview:"+child(feature,role,f.segment),{}};e.points=std::move(points);mesh.edges.push_back(std::move(e));};
    for(bool inner:{false,true}) {
        edge(inner?"inner-start":"outer-start",{point(0,inner,0),point(f.width,inner,0)});
        edge(inner?"inner-end":"outer-end",{point(0,inner,1),point(f.width,inner,1)});
        for(bool last:{false,true}) {
            std::vector<V> points;const int samples=p.unbend?1:std::max(1,static_cast<int>(std::ceil(p.angle_degrees/3)));
            for(int i=0;i<=samples;++i)points.push_back(point(last?f.width:0,inner,static_cast<double>(i)/samples));
            edge(std::string(inner?"inner-":"outer-")+(last?"last":"first"),std::move(points));
        }
    }
    for(bool last:{false,true})for(bool end:{false,true})edge(std::string(last?"last-":"first-")+(end?"end":"start"),
        {point(last?f.width:0,false,end?1:0),point(last?f.width:0,true,end?1:0)});
    for(std::size_t stage=0;stage<2;++stage) {
        if(stage==0&&p.angle_degrees==0)continue;
        const auto source=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[stage]).viewer_mesh();
        mesh.dimensions.insert(mesh.dimensions.end(),source.dimensions.begin(),source.dimensions.end());
    }
    return mesh;
}
}

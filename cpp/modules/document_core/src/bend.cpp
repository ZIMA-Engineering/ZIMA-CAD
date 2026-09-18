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
    // The authored section defines ancestry; OCCT only locates these
    // identities, including the Start/End region children.
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
    return add(add(f.anchor,scale(f.inward,(p.radius+p.thickness)*(1-std::cos(angle*fraction)))),
        scale(f.normal,(p.radius+p.thickness)*std::sin(angle*fraction)));
}
const sketcher::SketchSegment* continuation(const sketcher::Sketch& path) {
    const sketcher::SketchSegment* result=nullptr;
    for(const auto& segment:path.segments)if(!segment.construction) {
        if(result)throw std::invalid_argument("Bend path allows one optional tangent continuation.");
        result=&segment;
    }
    return result;
}
double continuation_length(const sketcher::Sketch& path) {
    const auto* segment=continuation(path);if(!segment)return 0;
    const auto& arc=path.arcs.at(0);
    if(segment->first_point_id!=arc.end_point_id&&segment->second_point_id!=arc.end_point_id)
        throw std::invalid_argument("Bend continuation must share the arc end point.");
    const auto* first=path.find_point(segment->first_point_id);const auto* last=path.find_point(segment->second_point_id);
    if(!first||!last)throw std::invalid_argument("Bend continuation endpoint is missing.");
    const double length=std::hypot(last->x-first->x,last->y-first->y);
    if(!std::isfinite(length)||length<.001||length>1e6)
        throw std::invalid_argument("Bend continuation length must be between 0.001 and 1000000 mm.");
    return length;
}
}
double bend_straight_length(const HistoryContainer& feature) {
    if(feature.bend.auxiliary_sketches[0].empty())return 0;
    return continuation_length(sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]));
}
std::optional<double> bend_attachment_profile_direction(
        const std::vector<ConstructionReference>& references,
        const kernel::ViewerReferenceGeometry& geometry) {
    const auto matches=[](const auto& actual,const auto& reference) {
        return actual.owner_id==reference.owner_id && actual.semantic_key==reference.semantic_key &&
            actual.instance_path==reference.instance_path;
    };
    const ConstructionReference* edge_reference=nullptr;
    for(const auto& reference:references) {
        if(!reference.orientation_drives_rotation)continue;
        if(reference.orientation_role=="front"||reference.orientation_role=="direction") {
            edge_reference=&reference;break;
        }
    }
    if(!edge_reference)return std::nullopt;
    const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e){return matches(e.reference,*edge_reference);});
    if(edge==geometry.edges.end()||edge->points.size()<2)return std::nullopt;
    const auto first=edge->points.front();
    auto along=add(edge->points.back(),scale(first,-1));
    const double length=std::sqrt(dot(along,along));
    if(length<1e-7)return std::nullopt;
    along=scale(along,1/length);
    const auto straight=[&](const auto& points) {
        return std::ranges::all_of(points,[&](V p) {
            const auto deviation=cross(add(p,scale(first,-1)),along);
            return dot(deviation,deviation)<1e-14;
        });
    };
    if(!straight(edge->points)||(edge->exact_spline&&!straight(edge->exact_spline->poles)))return std::nullopt;
    if(edge_reference->flip)along=scale(along,-1);
    for(const auto& reference:references) {
        if(!reference.orientation_drives_rotation ||
            (reference.orientation_role!="top"&&reference.orientation_role!="bottom"))continue;
        if(matches(*edge_reference,reference))continue; // Mirrored FRONT row.
        V normal{},interior{};double area=0;
        std::vector<V> vertices;
        for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
            if(!matches(geometry.triangle_references[i],reference))continue;
            if(3*i+2>=geometry.triangles.size())return std::nullopt;
            const auto ai=geometry.triangles[3*i],bi=geometry.triangles[3*i+1],ci=geometry.triangles[3*i+2];
            if(ai>=geometry.vertices.size()||bi>=geometry.vertices.size()||ci>=geometry.vertices.size())return std::nullopt;
            const V a=geometry.vertices[ai],b=geometry.vertices[bi],c=geometry.vertices[ci];
            const auto n=cross(add(b,scale(a,-1)),add(c,scale(a,-1)));
            const double weight=std::sqrt(dot(n,n));
            if(weight<1e-14)continue;
            if(area==0)normal=scale(n,1/weight);
            vertices.insert(vertices.end(),{a,b,c});
            interior=add(interior,scale(add(add(a,b),c),weight/3));area+=weight;
        }
        // A different secondary direction must retain its normal placement
        // meaning; do not reinterpret a later unrelated face as the join.
        if(area==0)return std::nullopt;
        if(std::abs(dot(normal,along))>1e-7)return std::nullopt;
        if(std::ranges::any_of(vertices,[&](V p){return std::abs(dot(add(p,scale(first,-1)),normal))>1e-7;}))
            return std::nullopt;
        if(reference.flip!=(reference.orientation_role=="bottom"))normal=scale(normal,-1);
        const auto inward=cross(normal,along);
        const double side=dot(add(scale(interior,1/area),scale(first,-1)),inward);
        if(std::abs(side)<1e-7)return std::nullopt;
        const double sign=side>0?1.:-1.;
        // Only a boundary edge determines a unique material side. A datum
        // plane crossing the line or a curved face is not an attachment face.
        if(std::ranges::any_of(vertices,[&](V p){return sign*dot(add(p,scale(first,-1)),inward)<-1e-7;}))
            return std::nullopt;
        return sign;
    }
    return std::nullopt;
}
std::vector<ConstructionReference> bend_sheet_references(const kernel::ViewerEdge& edge,
        const kernel::VertexReference& start) {
    if(kernel::sheet_edge_role(edge)!=kernel::SheetEdgeRole::Boundary||edge.points.size()<2||
        edge.edge_treatment_endpoint_references.size()!=2)
        throw std::invalid_argument("Bend requires a sheet boundary edge with two native endpoints.");
    const auto delta=add(edge.points.back(),scale(edge.points.front(),-1));
    const double length=std::sqrt(dot(delta,delta));
    if(length<.001)throw std::invalid_argument("Bend attachment edge is too short.");
    const auto along=scale(delta,1/length);
    for(const auto p:edge.points)if(std::sqrt(dot(cross(add(p,scale(edge.points.front(),-1)),along),
        cross(add(p,scale(edge.points.front(),-1)),along)))>1e-7)
        throw std::invalid_argument("Bend attachment requires a straight boundary edge.");
    const auto face=std::ranges::find_if(edge.edge_treatment_side_references,[](const auto& r) {
        return r.sheet_role==kernel::SheetFaceRole::ThicknessFace;
    });
    const auto point=start.valid()?start:edge.edge_treatment_endpoint_references.front();
    if(std::ranges::find(edge.edge_treatment_endpoint_references,point)==edge.edge_treatment_endpoint_references.end())
        throw std::invalid_argument("Bend origin must be an endpoint of its attachment edge.");
    return {{edge.reference.instance_path,edge.reference.owner_id,edge.reference.semantic_key,0,false,"front",true},
        {face->instance_path,face->owner_id,face->semantic_key,0,true,"top",true},
        {point.instance_path,point.owner_id,point.semantic_key}};
}
void update_bend_sheet_profile(HistoryContainer& feature,sketcher::Sketch& sketch,
        const kernel::ViewerReferenceGeometry& geometry,const std::string& document_id) {
    if(!feature.bend.sheet_attachment)return;
    update_sheet_edge_profile(feature.placement,feature.bend.thickness,sketch,geometry,document_id);
}
void update_sheet_edge_profile(const Placement& placement,double& thickness,sketcher::Sketch& sketch,
        const kernel::ViewerReferenceGeometry& geometry,const std::string& document_id) {
    if(placement.references.size()<3||std::ranges::count_if(sketch.segments,[](const auto& s){return !s.construction;})!=1)
        throw std::invalid_argument("Incomplete Bend sheet attachment.");
    const auto& source=placement.references.front();
    if(!bend_attachment_profile_direction(placement.references,geometry))
        throw std::invalid_argument("Bend requires a straight boundary with a planar joining face.");
    const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e) {
        return e.reference.owner_id==source.owner_id&&e.reference.semantic_key==source.semantic_key&&
            e.reference.instance_path==source.instance_path;
    });
    if(edge==geometry.edges.end())throw std::invalid_argument("Missing Bend sheet edge.");
    thickness=edge->edge_treatment_side_references.front().sheet_thickness;
    const auto& anchor=placement.references[2];
    const auto expected=bend_sheet_references(*edge,{anchor.owner_id,anchor.semantic_key,anchor.instance_path});
    const auto& face=placement.references[1];
    if(face.owner_id!=expected[1].owner_id||face.semantic_key!=expected[1].semantic_key||
        face.instance_path!=expected[1].instance_path||face.offset!=0||face.flip||source.flip)
        throw std::invalid_argument("The Bend joining face and direction are derived from its sheet edge.");
    if(sketch.plane_offset!=0)throw std::invalid_argument("Bend does not support an offset profile plane.");
    std::array<std::pair<double,kernel::VertexReference>,2> ends;
    for(std::size_t i=0;i<2;++i) {
        const auto& reference=edge->edge_treatment_endpoint_references[i];
        const auto point=std::ranges::find_if(geometry.points,[&](const auto& p){return p.reference==reference;});
        if(point==geometry.points.end())throw std::invalid_argument("Missing Bend sheet endpoint.");
        const auto local=add(point->position,scale(sketch.resolved_origin,-1));
        if(std::abs(dot(local,sketch.resolved_y_axis))>1e-6||std::abs(dot(local,sketch.resolved_normal))>1e-6)
            throw std::invalid_argument("Bend profile does not lie on its attachment edge.");
        ends[i]={dot(local,sketch.resolved_x_axis),reference};
    }
    if(ends[1].first<ends[0].first)std::swap(ends[0],ends[1]);
    const auto segment=*std::ranges::find_if(sketch.segments,[](const auto& s){return !s.construction;});
    // Offset identities follow source-point ancestry across the sheet thickness.
    // They must not follow the incidental tangent direction of a picked edge.
    const auto parent=[](const std::string& key) {
        if(key.starts_with("start:"))return key.substr(6);
        if(key.starts_with("end:"))return key.substr(4);
        if(key.starts_with("sweep:vertex:")) {
            const auto from=key.rfind(":from:");
            if(from!=std::string::npos) {
                const auto colon=key.find(':',from+6);
                if(colon!=std::string::npos) {
                    const auto length=key.substr(from+6,colon-from-6);
                    if(!length.empty()&&std::ranges::all_of(length,[](char c){return c>='0'&&c<='9';})&&
                        std::stoull(length)==key.size()-colon-1)return key.substr(colon+1);
                }
            }
        }
        return key;
    };
    const auto old_first=std::ranges::find(sketch.external_references,sketch.id+":attachment:first",&sketcher::SketchExternalReference::id);
    const auto old_last=std::ranges::find(sketch.external_references,sketch.id+":attachment:last",&sketcher::SketchExternalReference::id);
    const auto related=[&](const auto& old,const auto& point) {
        return old!=sketch.external_references.end()&&old->source_owner_id==point.owner_id&&
            old->source_instance_path==point.instance_path&&parent(old->source_semantic_key)==parent(point.semantic_key);
    };
    bool reversed=related(old_first,ends[1].second)||related(old_last,ends[0].second);
    if(!reversed&&!related(old_first,ends[0].second)&&!related(old_last,ends[1].second)&&
        old_first!=sketch.external_references.end()&&old_last!=sketch.external_references.end()) {
        const auto old_point=[&](const auto& reference) {
            return std::ranges::find_if(geometry.points,[&](const auto& point) {
                return point.reference.owner_id==reference->source_owner_id&&point.reference.semantic_key==reference->source_semantic_key&&
                    point.reference.instance_path==reference->source_instance_path;
            });
        };
        const auto a=old_point(old_first),b=old_point(old_last);
        if(a!=geometry.points.end()&&b!=geometry.points.end()) {
            const auto direction=add(b->position,scale(a->position,-1));
            reversed=dot(direction,sketch.resolved_x_axis)<0;
        }
    }
    for(std::size_t i=0;i<2;++i) {
        const auto end_index=reversed?1-i:i;
        const auto& end=ends[end_index];
        const auto suffix=i?":last":":first";
        const auto reference_id=sketch.id+":attachment"+suffix;
        auto existing=std::ranges::find(sketch.external_references,reference_id,&sketcher::SketchExternalReference::id);
        const bool fresh=existing==sketch.external_references.end();
        if(fresh) {sketch.external_references.push_back({});existing=std::prev(sketch.external_references.end());}
        auto& external=*existing;external.id=reference_id;external.kind=sketcher::ExternalReferenceKind::Point;
        external.source_document_id=document_id;external.source_owner_id=end.second.owner_id;
        external.source_semantic_key=end.second.semantic_key;external.source_instance_path=end.second.instance_path;
        const auto source_point=std::ranges::find_if(geometry.points,[&](const auto& point){return point.reference==end.second;});
        external.cached_points={sketch.local_point(source_point->position)};external.broken=false;
        const auto dimension=std::ranges::find(sketch.dimensions,sketch.id+":position"+suffix,&sketcher::SketchDimension::id);
        if(dimension==sketch.dimensions.end())throw std::invalid_argument("Missing Bend endpoint offset dimension.");
        dimension->first_point_id=reference_id;
        const auto target=dimension->second_point_id;
        const auto old_end_index=target==segment.first_point_id?0u:1u;
        if(fresh)dimension->value=std::copysign(0.0,end_index?-1.:1.);
        else if(old_end_index!=end_index)dimension->value=-dimension->value;
        dimension->solution_side=std::signbit(dimension->value)?-1:1;
        auto* point=sketch.find_point(target);
        point->x=end.first+dimension->value;point->y=0;
    }
    auto& directed_segment=*std::ranges::find_if(sketch.segments,[](const auto& s){return !s.construction;});
    directed_segment.first_point_id=std::ranges::find(sketch.dimensions,sketch.id+(reversed?":position:last":":position:first"),&sketcher::SketchDimension::id)->second_point_id;
    directed_segment.second_point_id=std::ranges::find(sketch.dimensions,sketch.id+(reversed?":position:first":":position:last"),&sketcher::SketchDimension::id)->second_point_id;
    if(sketch.find_point(directed_segment.second_point_id)->x-sketch.find_point(directed_segment.first_point_id)->x<.001)
        throw std::invalid_argument("Bend endpoint offsets leave no profile width.");
    sketch.validate();
}
void initialize_bend_start_profile(sketcher::Sketch& sketch,double width) {
    if(!std::isfinite(width)||width<.001||!sketch.points.empty()||!sketch.segments.empty())
        throw std::invalid_argument("A new Bend needs an empty start Sketch and a positive width.");
    static_cast<void>(sketch.add_segment(0,0,width,0));
    const auto segment=sketch.segments.front();
    for(bool last:{false,true}) {
        const auto& point=last?segment.second_point_id:segment.first_point_id;
        static_cast<void>(sketch.add_point_on_line_constraint(point,"sketch_axis:x"));
        auto dimension=sketch.create_point_dimension("sketch_origin",point,sketcher::DimensionKind::DistanceX);
        dimension.id=sketch.id+(last?":position:last":":position:first");
        dimension.placement=std::array{last?width*.5:-5.,last?8.:-8.};
        sketch.dimensions.push_back(std::move(dimension));
    }
    sketch.validate();
}
void initialize_sheet_revolution(HistoryContainer& feature,sketcher::Sketch& sketch,const SheetMetalDefaults& defaults) {
    feature.feature_kind=FeatureKind::Revolution;feature.name="Rotační plech";
    auto& p=feature.revolution;p.sheet_metal=true;p.sketch_id=sketch.id;
    p.result_type=ProfileResultType::Thin;p.profile_source=ProfileSource::Internal;
    p.profile_plane_offset=0;p.angle_degrees=90;p.angle_reverse=90;
    p.thin_thickness=defaults.thickness_mm.value_or(1);
    sketch.owner_container_id=feature.id;sketch.name=feature.name;
    initialize_bend_start_profile(sketch,40);
    const auto axis=sketch.add_segment(40,10,0,10,1e-6,true);
    sketch.set_segment_centerline(axis,true);p.axis_segment_id=axis;
}
void validate_sheet_revolution(const HistoryContainer& feature,const sketcher::Sketch& sketch) {
    const auto& p=feature.revolution;if(!p.sheet_metal)return;
    if(feature.combine_mode!=CombineMode::Add||p.result_type!=ProfileResultType::Thin||
        p.profile_source!=ProfileSource::Internal||p.profile_plane_offset!=0||sketch.plane_offset!=0)
        throw std::invalid_argument("Sheet Revolution requires an additive owned thin profile without plane offset.");
    const auto unsupported=[](const auto& curves){return std::ranges::any_of(curves,[](const auto& c){return !c.construction;});};
    if(std::ranges::count_if(sketch.segments,[](const auto& s){return !s.construction;})!=1||
        unsupported(sketch.arcs)||unsupported(sketch.circles)||unsupported(sketch.ellipses)||
        unsupported(sketch.elliptical_arcs)||unsupported(sketch.bsplines)||!sketch.texts.empty())
        throw std::invalid_argument("Sheet Revolution requires one profile segment and a construction centerline.");
    const auto axis=std::ranges::find(sketch.segments,p.axis_segment_id,&sketcher::SketchSegment::id);
    if(axis==sketch.segments.end()||!axis->construction||!axis->centerline)
        throw std::invalid_argument("Sheet Revolution axis must be an oriented construction centerline.");
    const auto& profile=*std::ranges::find_if(sketch.segments,[](const auto& s){return !s.construction;});
    const auto* a=sketch.find_point(axis->first_point_id);const auto* b=sketch.find_point(axis->second_point_id);
    const auto* c=sketch.find_point(profile.first_point_id);const auto* d=sketch.find_point(profile.second_point_id);
    const double axis_length=std::hypot(b->x-a->x,b->y-a->y),length=std::hypot(d->x-c->x,d->y-c->y);
    if(axis_length<.001||length<.001||!std::isfinite(p.thin_thickness)||p.thin_thickness<=0)
        throw std::invalid_argument("Sheet Revolution profile, axis and thickness must be positive.");
    const double first=p.thin_mode==ThinMode::OneSide?0:p.thin_mode==ThinMode::OtherSide?-p.thin_thickness:-p.thin_thickness/2;
    const double second=first+p.thin_thickness;double sign=0;
    for(const auto* point:{c,d})for(double offset:{first,second}) {
        const double x=point->x-(d->y-c->y)/length*offset,y=point->y+(d->x-c->x)/length*offset;
        const double side=((b->x-a->x)*(y-a->y)-(b->y-a->y)*(x-a->x))/axis_length;
        if(std::abs(side)<1e-7||(sign!=0&&sign*side<0))
            throw std::invalid_argument("Sheet Revolution thickness must stay on one side of its axis.");
        sign=side;
    }
}
void orient_bend_start_toward_edge(sketcher::Sketch& sketch,const Placement& placement,
        const kernel::ViewerReferenceGeometry& geometry) {
    if(!sketch.plane_auto||!bend_attachment_profile_direction(placement.references,geometry))return;
    auto first_dimension=std::ranges::find(sketch.dimensions,sketch.id+":position:first",&sketcher::SketchDimension::id);
    auto last_dimension=std::ranges::find(sketch.dimensions,sketch.id+":position:last",&sketcher::SketchDimension::id);
    if(first_dimension==sketch.dimensions.end()||last_dimension==sketch.dimensions.end()||sketch.segments.size()!=1)return;
    auto* first=sketch.find_point(sketch.segments.front().first_point_id);
    auto* last=sketch.find_point(sketch.segments.front().second_point_id);
    if(!first||!last)return;
    const double width=last->x-first->x;if(width<.001)return;
    PartDocument carrier;auto feature=PartDocument::create_sketch_container();
    feature.id=sketch.owner_container_id;feature.feature_kind=FeatureKind::Bend;
    feature.bend.sketch_id=sketch.id;feature.placement=placement;
    carrier.history={feature};carrier.sketches={sketch};carrier.resolve_constructions(geometry);
    if(!carrier.history.front().placement.reference_valid)return;
    const auto& resolved=carrier.sketches.front();
    const auto front=std::ranges::find_if(placement.references,[](const auto& r) {
        return r.orientation_drives_rotation&&(r.orientation_role=="front"||r.orientation_role=="direction");
    });
    if(front==placement.references.end())return;
    const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e) {
        return e.reference.owner_id==front->owner_id&&e.reference.semantic_key==front->semantic_key&&e.reference.instance_path==front->instance_path;
    });
    if(edge==geometry.edges.end()||edge->points.empty())return;
    const auto middle=scale(add(edge->points.front(),edge->points.back()),.5);
    const bool reverse=dot(add(middle,scale(resolved.resolved_origin,-1)),resolved.resolved_x_axis)<-1e-7;
    first->x=reverse?-width:0;last->x=reverse?0:width;
    first_dimension->value=first->x;
    last_dimension->value=last->x;
}
BendParameters resolved_bend_parameters(const HistoryContainer& feature,const SheetMetalDefaults& defaults) {
    auto p=feature.bend;
    if(!p.thickness_override&&!p.sheet_attachment)p.thickness=defaults.thickness_mm.value_or(1.0);
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
        !path.circles.empty()||!path.ellipses.empty()||!path.elliptical_arcs.empty()||!path.bsplines.empty()||!path.texts.empty())
        throw std::invalid_argument("Bend path requires one circular arc.");
    const double straight_length=continuation_length(path);
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
    if(const auto* segment=continuation(path)) {
        const auto& arc=path.arcs.front();
        const auto* join=path.find_point(arc.end_point_id);
        auto* tip=path.find_point(segment->first_point_id==arc.end_point_id?segment->second_point_id:segment->first_point_id);
        const double a=arc.end_angle+std::numbers::pi/2;
        tip->x=join->x+straight_length*std::cos(a);tip->y=join->y+straight_length*std::sin(a);
        const auto dimension=std::ranges::find_if(path.dimensions,[&](const auto& d) {
            return d.kind==sketcher::DimensionKind::Distance&&d.driving&&!d.suppressed&&
                ((d.first_point_id==segment->first_point_id&&d.second_point_id==segment->second_point_id)||
                 (d.first_point_id==segment->second_point_id&&d.second_point_id==segment->first_point_id));
        });
        if(dimension==path.dimensions.end()) {
            auto d=path.create_segment_dimension(segment->id);d.id=segment->id+":length";
            d.placement=std::array{tip->x+8.,tip->y+8.};path.dimensions.push_back(std::move(d));
        } else dimension->value=straight_length;
        if(!std::ranges::any_of(path.constraints,[&](const auto& c) {
            return !c.suppressed&&c.kind==sketcher::ConstraintKind::Tangent&&
                ((c.geometry_id==arc.id&&c.second_geometry_id==segment->id)||
                 (c.geometry_id==segment->id&&c.second_geometry_id==arc.id));
        }))static_cast<void>(path.add_tangent_constraint(arc.id,segment->id,arc.end_point_id));
    }
    path.validate();data[0]=path.serialized();
    auto end=data[1].empty()?sketcher::Sketch::create_default():sketcher::Sketch::from_serialized(data[1]);
    end.owner_container_id=feature.id;end.name="Koncový profil ohybu";
    end.plane_reference_owner_id=feature.id+":bend:end";end.plane_offset=0;
    const double a=p.angle_degrees*std::numbers::pi/180;
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
        if(std::abs(next.bend.radius-p.radius)<1e-9)next.bend.radius=p.radius;
        if(std::abs(next.bend.angle_degrees-p.angle_degrees)<1e-9)next.bend.angle_degrees=p.angle_degrees;
        if(const auto* segment=continuation(edited)) {
            static_cast<void>(continuation_length(edited));
            const auto* join=edited.find_point(arc.end_point_id);
            const auto* tip=edited.find_point(segment->first_point_id==arc.end_point_id?segment->second_point_id:segment->first_point_id);
            const double a=arc.end_angle+std::numbers::pi/2;
            if(!continuation(before)&&(tip->x-join->x)*std::cos(a)+(tip->y-join->y)*std::sin(a)<=0)
                throw std::invalid_argument("Bend continuation must extend forward from the arc end.");
            if(!continuation(before)) {
                // A newly drawn line may carry H/V inference. Its feature-owned
                // direction is the outgoing tangent, not a fixed Sketch axis.
                std::erase_if(edited.constraints,[&](const auto& c) {
                    return (c.kind==sketcher::ConstraintKind::Horizontal||c.kind==sketcher::ConstraintKind::Vertical)&&
                        c.geometry_id==segment->id;
                });
            }
        }
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
    kernel::FeatureGroupRequest group;
    const auto axis_start=add(f.first,scale(f.inward,p.radius+p.thickness));
    group.axes.push_back({add(axis_start,scale(f.along,f.width*.5)),f.along,f.width+2,
        {feature.id,child(feature,"axis",f.segment),{}},"Osa rotace ohybu"});
    const double straight_length=bend_straight_length(feature);
    if(p.angle_degrees==0&&straight_length==0)return group; // Explicit zero-material history boundary.
    const auto extension=bend_profile_extensions(feature);
    if(p.radius==0&&(std::abs(extension[0])>1e-9||std::abs(extension[1])>1e-9))
        throw std::invalid_argument("Zero inner radius currently requires matching start and end profiles.");
    const auto path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    const auto& arc=path.arcs.front();kernel::Sweep3DRequest request;
    request.linear_tolerance=1e-7;
    request.path_points={path_point(f,p,0),path_point(f,p,1)};
    request.path_point_ids={arc.start_point_id,arc.end_point_id};
    kernel::Sweep3DRequest::PathSegment route;route.source_id=arc.id;
    route.start=request.path_points.front();route.end=request.path_points.back();
    route.arc_midpoint=path_point(f,p,.5);
    request.path_segments.push_back(std::move(route));
    for(bool last:{false,true}) {
        const double a=last?p.angle_degrees*std::numbers::pi/180:0;
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
    if(straight_length>0) {
        const auto* segment=continuation(path);const auto& join=arc.end_point_id;
        const auto tip=segment->first_point_id==join?segment->second_point_id:segment->first_point_id;
        const double a=p.angle_degrees*std::numbers::pi/180;
        const auto delta=scale(add(scale(f.normal,std::cos(a)),scale(f.inward,std::sin(a))),straight_length);
        kernel::Sweep3DRequest straight;straight.linear_tolerance=request.linear_tolerance;
        straight.path_points={request.path_points.back(),add(request.path_points.back(),delta)};
        straight.path_point_ids={join,tip};
        request.canonical_station_ids.insert(join);
        straight.canonical_station_ids.insert(join);
        kernel::Sweep3DRequest::PathSegment line;line.source_id=segment->id;
        line.start=straight.path_points.front();line.end=straight.path_points.back();straight.path_segments.push_back(line);
        auto first=request.sections.back();first.point_index=0;straight.sections.push_back(first);
        auto last=first;last.point_id=tip;last.point_index=1;
        auto& polygon=std::get<kernel::ExtrusionRequest::PolygonProfile>(last.profile.outer_profile);
        for(auto& vertex:polygon.vertices)vertex=add(vertex,delta);
        straight.sections.push_back(std::move(last));
        if(p.angle_degrees>0)group.children.emplace_back(std::move(request));
        group.children.emplace_back(std::move(straight));
    } else group.children.emplace_back(std::move(request));
    return group;
}
kernel::SheetMaterialDefinition bend_material_definition(const HistoryContainer& input,const sketcher::Sketch& sketch,const SheetMetalDefaults& defaults) {
    auto feature=input;prepare_bend_sketches(feature,sketch,defaults);
    const auto p=resolved_bend_parameters(feature,defaults);const auto f=frame(feature,sketch);
    kernel::SheetMaterialDefinition definition;
    definition.kind=kernel::SheetMaterialDefinition::Kind::Cylinder;definition.owner_id=feature.id;
    definition.origin=f.anchor;definition.along=f.along;definition.tangent=f.normal;definition.radial=scale(f.inward,-1);
    definition.radius=p.radius+p.thickness;definition.neutral_radius=p.radius+p.k_factor*p.thickness;
    definition.angle=p.angle_degrees*std::numbers::pi/180;definition.thickness=p.thickness;
    definition.continuation=bend_straight_length(feature);
    const auto path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    definition.curved_source_id=path.arcs.front().id;
    if(const auto* segment=continuation(path))definition.continuation_source_id=segment->id;
    return definition;
}
kernel::ViewerMesh bend_preview(const HistoryContainer& input,const sketcher::Sketch& sketch,const SheetMetalDefaults& defaults) {
    auto feature=input;prepare_bend_sketches(feature,sketch,defaults);
    const auto request=bend_request(feature,sketch,defaults);const auto p=resolved_bend_parameters(feature,defaults);const auto f=frame(feature,sketch);
    const auto extension=bend_profile_extensions(feature);
    kernel::ViewerMesh mesh;mesh.axes=request.axes;
    const double angle=p.angle_degrees*std::numbers::pi/180;
    const auto point=[&](double along,bool inner,double fraction) {
        along+=(along==0?-extension[0]:extension[1])*fraction;
        auto result=add(f.first,scale(f.along,along));
        const double r=p.radius+(inner?0:p.thickness),a=angle*fraction;
        return add(add(result,scale(f.inward,p.radius+p.thickness-r*std::cos(a))),scale(f.normal,r*std::sin(a)));
    };
    const auto edge=[&](std::string role,std::vector<V> points){kernel::ViewerEdge e;e.reference={feature.id,"preview:"+child(feature,role,f.segment),{}};e.points=std::move(points);mesh.edges.push_back(std::move(e));};
    for(bool inner:{false,true}) {
        edge(inner?"inner-start":"outer-start",{point(0,inner,0),point(f.width,inner,0)});
        edge(inner?"inner-end":"outer-end",{point(0,inner,1),point(f.width,inner,1)});
        for(bool last:{false,true}) {
            std::vector<V> points;const int samples=std::max(1,static_cast<int>(std::ceil(p.angle_degrees/3)));
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
    const double straight_length=bend_straight_length(feature);
    if(straight_length>0) {
        const double a=angle;
        const auto delta=scale(add(scale(f.normal,std::cos(a)),scale(f.inward,std::sin(a))),straight_length);
        for(bool inner:{false,true}) {
            for(bool last:{false,true}) {
                const auto start=point(last?f.width:0,inner,1);
                edge(std::string("straight-")+(inner?"inner-":"outer-")+(last?"last":"first"),{start,add(start,delta)});
            }
            edge(inner?"straight-inner-end":"straight-outer-end",{add(point(0,inner,1),delta),add(point(f.width,inner,1),delta)});
        }
        for(bool last:{false,true})edge(last?"straight-last-end":"straight-first-end",
            {add(point(last?f.width:0,false,1),delta),add(point(last?f.width:0,true,1),delta)});
    }
    return mesh;
}
}

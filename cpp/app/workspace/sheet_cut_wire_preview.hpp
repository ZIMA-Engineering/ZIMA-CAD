#pragma once

#include <zima/document/document_session.hpp>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <tuple>

namespace zima::app::workspace_detail {

namespace sheet_cut_wire_detail {
using Vec3=zima::kernel::Vec3;
inline constexpr std::size_t max_profile_points=16384;
inline constexpr std::size_t max_profile_chains=4096;
inline constexpr std::size_t max_examined_facets=100000;
inline constexpr std::size_t max_parallel_intervals=128;
inline constexpr std::size_t max_projection_work=4000000;
inline Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 subtract(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 multiply(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
inline double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double length(Vec3 a){return std::sqrt(dot(a,a));}
inline Vec3 unit(Vec3 a){const double l=length(a);return l>1.e-15?multiply(a,1/l):Vec3{};}
inline double cross2(QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();}
inline double dot2(QPointF a,QPointF b){return a.x()*b.x()+a.y()*b.y();}
inline QPainterPath polygon_path(const std::vector<QPointF>& points) {
    QPainterPath result;result.setFillRule(Qt::OddEvenFill);
    if(points.size()<3)return result;
    result.moveTo(points.front());for(std::size_t i=1;i<points.size();++i)result.lineTo(points[i]);
    result.closeSubpath();return result;
}
inline std::vector<QPointF> clip_polygon(std::vector<QPointF> polygon,
                                       const std::array<double,3>& values) {
    if(polygon.empty())return {};
    const auto value=[&](QPointF p){return values[0]+p.x()*(values[1]-values[0])+p.y()*(values[2]-values[0]);};
    std::vector<QPointF> result;
    QPointF previous=polygon.back();double before=value(previous);
    for(const auto current:polygon) {
        const double after=value(current);
        if((before>=-1.e-9)!=(after>=-1.e-9))
            result.push_back(previous+(current-previous)*(before/(before-after)));
        if(after>=-1.e-9)result.push_back(current);
        previous=current;before=after;
    }
    return result;
}
struct LimitPlane {Vec3 origin,normal;};
inline LimitPlane cap_plane(const std::vector<Vec3>& points,Vec3 toward) {
    LimitPlane result{points.front(),unit(toward)};
    for(std::size_t i=1;i+1<points.size();++i) {
        const auto n=cross(subtract(points[i],points.front()),subtract(points[i+1],points.front()));
        if(length(n)<=1.e-10)continue;
        result.normal=unit(n);if(dot(result.normal,toward)<0)result.normal=multiply(result.normal,-1);
        break;
    }
    // A non-planar Up-to surface is represented by its displayed envelope.
    // The exact trimmed target remains the responsibility of OK calculation.
    if(std::ranges::any_of(points,[&](Vec3 p){return std::abs(dot(subtract(p,result.origin),result.normal))>1.e-5;})) {
        result.normal=unit(toward);
        result.origin=*std::ranges::min_element(points,{},[&](Vec3 p){return dot(p,result.normal);});
    }
    return result;
}
struct SkinPoint {Vec3 outer,inner;};
inline SkinPoint skin_point(Vec3 point,const zima::kernel::FaceReference& reference,Vec3 facet_normal) {
    Vec3 normal=facet_normal;
    if(reference.surface) {
        const auto& surface=*reference.surface;
        const Vec3 axis=unit(surface.axis),relative=subtract(point,surface.origin);
        if(surface.kind==zima::kernel::SurfaceGeometry::Kind::Plane) {
            normal=axis;point=subtract(point,multiply(axis,dot(relative,axis)));
        } else {
            const double axial=dot(relative,axis);
            const Vec3 radial=unit(subtract(relative,multiply(axis,axial)));
            const double slope=surface.kind==zima::kernel::SurfaceGeometry::Kind::Cone?std::tan(surface.semi_angle):0;
            const double radius=surface.radius+axial*slope;
            if(length(radial)>0 && radius>0) {
                point=add(surface.origin,add(multiply(axis,axial),multiply(radial,radius)));
                normal=unit(subtract(radial,multiply(axis,slope)));
            }
        }
        // Stored triangle winding is authoritative for material side. Surface
        // axis/radial data intentionally do not encode an OCCT frame handedness.
        if(dot(normal,facet_normal)<0)normal=multiply(normal,-1);
    }
    return {point,subtract(point,multiply(normal,reference.sheet_thickness))};
}
using PointKey=std::array<long long,3>;
inline PointKey point_key(Vec3 p,double tolerance) {
    return {std::llround(p.x/tolerance),std::llround(p.y/tolerance),std::llround(p.z/tolerance)};
}
using FaceKey=std::tuple<std::string,std::string,std::string>;
inline FaceKey face_key(const zima::kernel::FaceReference& reference) {
    return {reference.owner_id,reference.semantic_key,reference.instance_path};
}
struct Segment {SkinPoint first,last;};
struct SharedEdge {
    Vec3 first,last,normal;
    const zima::kernel::FaceReference* reference{};
    std::vector<std::pair<double,int>> events;
};

// A facet parallel to the extrusion ray has a rank-one projection. Intersect
// the profile with that projected line rather than dropping the entire wall.
inline std::optional<QPainterPath> parallel_footprint(const QPainterPath& profile,
                                      const QList<QPolygonF>& loops,
                                      const std::array<QPointF,3>& projected,
                                      const std::vector<QPointF>& domain,
                                      std::size_t& work_remaining) {
    QPointF direction=projected[1]-projected[0];
    if(dot2(projected[2]-projected[0],projected[2]-projected[0])>dot2(direction,direction))direction=projected[2]-projected[0];
    const double norm=std::sqrt(dot2(direction,direction));
    if(norm<1.e-12)return profile.contains(projected[0])?polygon_path(domain):QPainterPath{};
    direction/=norm;
    std::array<double,3> positions{0,dot2(projected[1]-projected[0],direction),dot2(projected[2]-projected[0],direction)};
    std::vector<double> limits{*std::ranges::min_element(positions),*std::ranges::max_element(positions)};
    for(const auto& loop:loops)for(qsizetype i=1;i<loop.size();++i) {
        const QPointF a=loop[i-1]-projected[0],b=loop[i]-projected[0];
        const double da=cross2(direction,a),db=cross2(direction,b);
        if((da>0)==(db>0)||std::abs(da-db)<1.e-14)continue;
        const double at=dot2(a+(b-a)*(da/(da-db)),direction);
        if(at>limits[0]&&at<limits[1]) {
            limits.push_back(at);
            // Repeated contains()/united() over a slotted rank-one profile
            // would otherwise escape the ordinary facet-times-point budget.
            if(limits.size()>max_parallel_intervals+1)return std::nullopt;
        }
    }
    std::ranges::sort(limits);limits.erase(std::unique(limits.begin(),limits.end(),[](double a,double b){return std::abs(a-b)<1.e-9;}),limits.end());
    const auto intervals=limits.size()-1;
    const auto work=intervals*(static_cast<std::size_t>(profile.elementCount())+intervals);
    if(work>work_remaining)return std::nullopt;
    work_remaining-=work;
    QPainterPath result;
    for(std::size_t i=1;i<limits.size();++i) {
        if(!profile.contains(projected[0]+direction*((limits[i-1]+limits[i])*.5)))continue;
        auto piece=domain;
        piece=clip_polygon(std::move(piece),{positions[0]-limits[i-1],positions[1]-limits[i-1],positions[2]-limits[i-1]});
        piece=clip_polygon(std::move(piece),{limits[i]-positions[0],limits[i]-positions[1],limits[i]-positions[2]});
        result=result.united(polygon_path(piece));
    }
    return result;
}
} // namespace sheet_cut_wire_detail

// Display-only estimate, built entirely from authored Sketch wires and stored
// viewer facets/surface measurements. It never requests a body calculation.
// Curved walls follow the existing tessellation; clearance samples five depth
// levels. Only OK calculates the exact normal-wall envelope and resulting solid.
inline std::vector<zima::kernel::ViewerEdge> estimated_sheet_cut_wire(
        const zima::document::HistoryContainer& feature,
        const zima::sketcher::Sketch& sketch,
        const zima::kernel::ViewerMesh& input,
        const std::vector<zima::kernel::ViewerEdge>& prism_wire) {
    using namespace sheet_cut_wire_detail;
    if(!feature.extrusion.sheet_cut||prism_wire.empty()||
        prism_wire.size()>max_profile_chains*4)return {};
    const auto triangle_count=std::min(input.triangle_references.size(),input.triangles.size()/3);
    // Reject excessive input before projecting even the first facet. A large
    // unrelated mesh must not consume a full scan for every parameter edit.
    if(triangle_count>max_examined_facets)return {};
    std::vector<std::vector<QPointF>> chains;
    std::vector<Vec3> start_points,end_points;
    std::size_t point_count{};
    for(const auto& edge:prism_wire) {
        if(edge.reference.semantic_key=="preview:start") {
            if(edge.points.size()<2||chains.size()>=max_profile_chains||
                edge.points.size()>max_profile_points-point_count)return {};
            std::vector<QPointF> chain;
            chain.reserve(edge.points.size());
            for(const auto p:edge.points) {
                const auto xy=sketch.local_point(p);chain.emplace_back(xy[0],xy[1]);
                start_points.push_back(p);
            }
            point_count+=chain.size();if(chain.size()>1)chains.push_back(std::move(chain));
        } else if(edge.reference.semantic_key=="preview:end") {
            if(edge.points.size()>max_profile_points-end_points.size())return {};
            end_points.insert(end_points.end(),edge.points.begin(),edge.points.end());
        }
    }
    // Keep the transient path bounded even for pathological imported profiles.
    if(chains.empty()||start_points.empty()||end_points.empty())return {};
    const auto close=[](QPointF a,QPointF b){return dot2(a-b,a-b)<1.e-12;};
    QPainterPath profile;profile.setFillRule(Qt::OddEvenFill);
    while(!chains.empty()) {
        auto loop=std::move(chains.back());chains.pop_back();
        while(!close(loop.front(),loop.back())) {
            const auto next=std::ranges::find_if(chains,[&](const auto& candidate){return close(loop.back(),candidate.front())||close(loop.back(),candidate.back());});
            if(next==chains.end())return {};
            if(!close(loop.back(),next->front()))std::ranges::reverse(*next);
            loop.insert(loop.end(),next->begin()+1,next->end());chains.erase(next);
        }
        if(loop.size()<4)return {};
        profile.addPath(polygon_path(loop));
    }
    if(profile.isEmpty())return {};
    const auto loops=profile.toSubpathPolygons();
    const auto bounds=profile.boundingRect();
    const Vec3 toward=subtract(end_points.front(),start_points.front());
    if(length(toward)<=1.e-10)return {};
    const std::array<LimitPlane,2> limits{cap_plane(start_points,toward),cap_plane(end_points,multiply(toward,-1))};
    const double scale=std::max({1.,bounds.width(),bounds.height()});
    const double tolerance=std::max(1.e-7,scale*1.e-8);
    std::map<FaceKey,std::vector<Segment>> segments;
    std::map<std::tuple<FaceKey,PointKey,PointKey>,SharedEdge> shared;
    std::size_t candidate_count{};
    std::size_t parallel_work_remaining=max_projection_work;
    for(std::size_t triangle=0;triangle<triangle_count;++triangle) {
        const auto& reference=input.triangle_references[triangle];
        if(reference.sheet_role!=zima::kernel::SheetFaceRole::SideA||
            !std::isfinite(reference.sheet_thickness)||reference.sheet_thickness<=0)continue;
        std::array<Vec3,3> source;
        bool valid=true;
        for(std::size_t i=0;i<3;++i) {
            const auto index=input.triangles[triangle*3+i];
            if(index>=input.vertices.size()){valid=false;break;}source[i]=input.vertices[index];
        }
        if(!valid)continue;
        const Vec3 normal=unit(cross(subtract(source[1],source[0]),subtract(source[2],source[0])));
        if(length(normal)<.5)continue;
        std::array<SkinPoint,3> skin;
        for(std::size_t i=0;i<3;++i)skin[i]=skin_point(source[i],reference,normal);
        double xmin=std::numeric_limits<double>::infinity(),xmax=-xmin,ymin=xmin,ymax=-xmin;
        for(const auto& sample:skin)for(const Vec3 p:{sample.outer,sample.inner}) {
            const auto xy=sketch.local_point(p);xmin=std::min(xmin,xy[0]);xmax=std::max(xmax,xy[0]);ymin=std::min(ymin,xy[1]);ymax=std::max(ymax,xy[1]);
        }
        if(xmax<bounds.left()-tolerance||xmin>bounds.right()+tolerance||ymax<bounds.top()-tolerance||ymin>bounds.bottom()+tolerance)continue;
        ++candidate_count;
        const int samples=feature.extrusion.sheet_cut_clearance?5:1;
        if(candidate_count>20000||candidate_count*point_count*static_cast<std::size_t>(samples)>max_projection_work)
            return {}; // Keep both facet and profile complexity bounded.
        QPainterPath footprint;
        for(int depth=0;depth<samples;++depth) {
            const double fraction=samples==1?0.:depth/double(samples-1);
            std::array<Vec3,3> points;
            std::array<QPointF,3> projected;
            for(std::size_t i=0;i<3;++i) {
                points[i]=add(skin[i].outer,multiply(subtract(skin[i].inner,skin[i].outer),fraction));
                const auto xy=sketch.local_point(points[i]);projected[i]={xy[0],xy[1]};
            }
            std::vector<QPointF> domain{{0,0},{1,0},{0,1}};
            for(const auto& limit:limits) {
                std::array<double,3> distance;
                for(std::size_t i=0;i<3;++i)distance[i]=dot(subtract(points[i],limit.origin),limit.normal);
                domain=clip_polygon(std::move(domain),distance);
            }
            if(domain.size()<3)continue;
            const auto a=projected[1]-projected[0],b=projected[2]-projected[0];
            QPainterPath piece;
            if(std::abs(cross2(a,b))>1.e-10*std::max(1.,std::sqrt(dot2(a,a)*dot2(b,b)))) {
                const QTransform transform(a.x(),a.y(),b.x(),b.y(),projected[0].x(),projected[0].y());
                piece=transform.inverted().map(profile).intersected(polygon_path(domain));
            } else {
                const auto parallel=parallel_footprint(profile,loops,projected,domain,parallel_work_remaining);
                if(!parallel)return {}; // Retain the prism, never a partial cut wire.
                piece=*parallel;
            }
            footprint=footprint.isEmpty()?piece:footprint.united(piece);
        }
        if(footprint.isEmpty())continue;
        const auto world=[&](QPointF p){return add(source[0],add(multiply(subtract(source[1],source[0]),p.x()),multiply(subtract(source[2],source[0]),p.y())));};
        const auto face=face_key(reference);
        for(const auto& loop:footprint.toSubpathPolygons())for(qsizetype i=1;i<loop.size();++i) {
            const auto a=loop[i-1],b=loop[i];if(dot2(a-b,a-b)<1.e-18)continue;
            int edge=-1;
            if(std::abs(a.y())<1.e-7&&std::abs(b.y())<1.e-7)edge=0;
            else if(std::abs(a.x()+a.y()-1)<1.e-7&&std::abs(b.x()+b.y()-1)<1.e-7)edge=1;
            else if(std::abs(a.x())<1.e-7&&std::abs(b.x())<1.e-7)edge=2;
            if(edge<0) {
                segments[face].push_back({skin_point(world(a),reference,normal),skin_point(world(b),reference,normal)});
                continue;
            }
            Vec3 first_point=source[edge],last_point=source[(edge+1)%3];
            auto first_key=point_key(first_point,tolerance),last_key=point_key(last_point,tolerance);
            if(last_key<first_key){std::swap(first_point,last_point);std::swap(first_key,last_key);}
            auto& entry=shared[{face,first_key,last_key}];
            entry.first=first_point;entry.last=last_point;entry.normal=normal;entry.reference=&reference;
            const auto delta=subtract(last_point,first_point);const double denominator=dot(delta,delta);
            if(denominator<1.e-20)continue;
            double from=std::clamp(dot(subtract(world(a),first_point),delta)/denominator,0.,1.);
            double to=std::clamp(dot(subtract(world(b),first_point),delta)/denominator,0.,1.);
            if(from>to)std::swap(from,to);
            entry.events.emplace_back(from,1);entry.events.emplace_back(to,-1);
        }
    }
    // Cancel only overlapping portions of shared facet edges. This retains a
    // real cut contour even when it happens to lie exactly on a mesh diagonal.
    for(auto& [key,entry]:shared) {
        std::ranges::sort(entry.events);int count=0;double previous=0;
        for(std::size_t i=0;i<entry.events.size();) {
            const double at=entry.events[i].first;
            if((count%2)!=0&&at-previous>1.e-8) {
                const auto point=[&](double t){return skin_point(add(entry.first,multiply(subtract(entry.last,entry.first),t)),*entry.reference,entry.normal);};
                segments[std::get<0>(key)].push_back({point(previous),point(at)});
            }
            int change=0;
            while(i<entry.events.size()&&std::abs(entry.events[i].first-at)<1.e-8)change+=entry.events[i++].second;
            count+=change;previous=at;
        }
    }
    std::vector<zima::kernel::ViewerEdge> result;
    for(auto& [face,items]:segments) {
        struct Node {SkinPoint skin;std::vector<std::size_t> links;};
        std::map<PointKey,std::size_t> nodes_by_point;
        std::vector<Node> nodes;std::vector<std::array<std::size_t,2>> links;
        std::map<std::pair<std::size_t,std::size_t>,bool> unique;
        const auto node=[&](SkinPoint p) {
            const auto key=point_key(p.outer,tolerance);
            const auto found=nodes_by_point.find(key);if(found!=nodes_by_point.end())return found->second;
            const auto index=nodes.size();nodes_by_point.emplace(key,index);nodes.push_back({p,{}});return index;
        };
        for(const auto& item:items) {
            const auto a=node(item.first),b=node(item.last);if(a==b)continue;
            if(!unique.emplace(std::minmax(a,b),true).second)continue;
            nodes[a].links.push_back(links.size());nodes[b].links.push_back(links.size());links.push_back({a,b});
        }
        std::vector<bool> used(links.size());
        for(std::size_t start=0;start<links.size();++start) {
            if(used[start])continue;
            std::size_t current=links[start][0];
            if(nodes[links[start][1]].links.size()!=2)current=links[start][1];
            std::vector<std::size_t> chain{current};std::size_t edge=start;
            do {
                used[edge]=true;current=links[edge][0]==current?links[edge][1]:links[edge][0];chain.push_back(current);
                if(current==chain.front()||nodes[current].links.size()!=2)break;
                const auto next=std::ranges::find_if(nodes[current].links,[&](auto candidate){return !used[candidate];});
                if(next==nodes[current].links.end())break;edge=*next;
            } while(true);
            zima::kernel::ViewerEdge outer,inner;
            outer.reference={feature.id,"preview:sheetcut:side-a",{}};
            inner.reference={feature.id,"preview:sheetcut:side-b",{}};
            for(const auto index:chain){outer.points.push_back(nodes[index].skin.outer);inner.points.push_back(nodes[index].skin.inner);}
            result.push_back(std::move(outer));result.push_back(std::move(inner));
            const bool closed=chain.front()==chain.back();const std::size_t count=chain.size()-(closed?1:0);
            bool connected=false;
            for(std::size_t i=0;i<count;++i) {
                bool corner=!closed&&(i==0||i+1==count);
                if(!corner) {
                    const auto p=nodes[chain[i]].skin.outer;
                    const auto before=unit(subtract(p,nodes[chain[(i+count-1)%count]].skin.outer));
                    const auto after=unit(subtract(nodes[chain[(i+1)%count]].skin.outer,p));
                    corner=dot(before,after)<.94;
                }
                if(corner||(closed&&i+1==count&&!connected)) {
                    const auto p=nodes[chain[i]].skin;result.push_back({{p.outer,p.inner},{feature.id,"preview:sheetcut:thickness",{}}});connected=true;
                }
            }
        }
    }
    return result;
}

// Display-only snapshot: use already calculated exact cut edges while editing
// an unchanged feature; otherwise cache the bounded viewer-data estimate. No
// OCCT work is permitted here, in a property callback, or in viewer painting.
struct SheetCutWirePreview {
    std::string owner_id;
    zima::document::ExtrusionParameters parameters;
    zima::document::Placement placement;
    std::string sketch_definition;
    std::uint64_t source_generation{};
    std::vector<zima::kernel::ViewerEdge> boundary_edges;
    bool cache_valid{};
    std::size_t estimate_evaluations{};

    void assign(const zima::document::HistoryContainer& feature,
                const zima::sketcher::Sketch& sketch,
                const zima::kernel::BodyResult& calculated,
                std::uint64_t generation) {
        owner_id=feature.id;parameters=feature.extrusion;placement=feature.placement;
        sketch_definition=sketch.serialized();source_generation=generation;
        boundary_edges.clear();cache_valid=true;
        if(!feature.extrusion.sheet_cut||calculated.calculation_errors.contains(feature.id))return;
        for(const auto& edge:calculated.mesh.edges)
            if(edge.reference.owner_id==owner_id && edge.points.size()>1 &&
                !edge.construction&&!edge.parameter_seam)
                boundary_edges.push_back(edge);
    }

    void append_estimate_or_current(
            const zima::document::HistoryContainer& feature,
            const zima::sketcher::Sketch& sketch,
            const zima::kernel::ViewerMesh* input,
            std::uint64_t generation,
            std::vector<zima::kernel::ViewerEdge>& wire) {
        if(!feature.extrusion.sheet_cut||wire.empty()) {
            boundary_edges.clear();cache_valid=false;return;
        }
        const auto definition=sketch.serialized();
        if(!cache_valid||owner_id!=feature.id||source_generation!=generation||
            parameters!=feature.extrusion||placement!=feature.placement||sketch_definition!=definition) {
            owner_id=feature.id;parameters=feature.extrusion;placement=feature.placement;
            sketch_definition=definition;source_generation=generation;cache_valid=true;
            boundary_edges.clear();
            if(input) {
                ++estimate_evaluations;
                boundary_edges=estimated_sheet_cut_wire(feature,sketch,*input,wire);
            }
        }
        wire.insert(wire.end(),boundary_edges.begin(),boundary_edges.end());
    }
};

inline SheetCutWirePreview cached_sheet_cut_wire(
        const zima::document::DocumentSession& session,
        const std::string& owner_id) {
    SheetCutWirePreview result;
    const auto& document=session.document();
    const auto* feature=document.find_container(owner_id);
    if(!feature || !feature->extrusion.sheet_cut)return result;
    const auto sketch=std::ranges::find(document.sketches,feature->extrusion.sketch_id,
        &zima::sketcher::Sketch::id);
    if(sketch==document.sketches.end())return result;
    // Body snapshots are already in the same local coordinates as the owned
    // profile and analytical extrusion wire. Assembly occurrence transforms
    // are subsequently applied once by the common transient-edge renderer.
    if(const auto* body=document.body_owner_for_object(owner_id)) {
        std::size_t count{};
        for(const auto& entry:body->entries) {
            if(entry.kind!=zima::document::PartHistoryKind::Feature)continue;
            const auto* candidate=document.find_container(entry.id);
            if(candidate&&candidate->feature_kind!=zima::document::FeatureKind::Sketch)++count;
            if(entry.id!=owner_id)continue;
            if(const auto boundary=session.calculated_body_boundary(body->scope.id,count))
                result.assign(*feature,*sketch,*boundary,session.data_generation());
            return result;
        }
    }
    for(const auto& boundary:session.calculated_boundaries()) {
        if(std::ranges::none_of(boundary.sheet_cuts,[&](const auto& region){return region.cut_owner==owner_id;}))continue;
        result.assign(*feature,*sketch,boundary,session.data_generation());
        break;
    }
    return result;
}

} // namespace zima::app::workspace_detail

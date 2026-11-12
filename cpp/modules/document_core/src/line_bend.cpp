#include <zima/document/line_bend.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace zima::document {
namespace {
using V=kernel::Vec3;
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V scale(V a,double value){return {a.x*value,a.y*value,a.z*value};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,
    a.x*b.y-a.y*b.x};}
V unit(V value) {
    const double magnitude=std::sqrt(dot(value,value));
    if(magnitude<1e-12)throw std::invalid_argument(
        "Line Bend requires a nondegenerate straight segment.");
    return scale(value,1/magnitude);
}
struct Resolved {
    kernel::LineBendRequest request;
    kernel::SheetMaterialDefinition material;
};
Resolved resolve(const HistoryContainer& feature,const sketcher::Sketch& sketch,
        const SheetMetalDefaults& defaults) {
    if(feature.feature_kind!=FeatureKind::LineBend||
            feature.line_bend.sketch_id!=sketch.id||
            sketch.owner_container_id!=feature.id||
            feature.line_bend.segment_id.empty())
        throw std::invalid_argument("Invalid Line Bend definition.");
    const auto segment=std::ranges::find(sketch.segments,
        feature.line_bend.segment_id,&sketcher::SketchSegment::id);
    if(segment==sketch.segments.end()||segment->construction||
            std::ranges::count_if(sketch.segments,
                [](const auto& value){return !value.construction;})!=1)
        throw std::invalid_argument(
            "Line Bend requires exactly one ordinary straight Sketch segment.");
    const auto* first=sketch.find_point(segment->first_point_id);
    const auto* last=sketch.find_point(segment->second_point_id);
    if(!first||!last)throw std::invalid_argument(
        "Line Bend Sketch segment endpoints are missing.");
    const auto& parameters=feature.line_bend;
    const double thickness=parameters.thickness_override?
        parameters.thickness:defaults.thickness_mm.value_or(1.);
    const double k_factor=parameters.k_factor_override?
        parameters.k_factor:defaults.k_factor;
    if(!std::isfinite(parameters.radius)||parameters.radius<=0||
            parameters.radius>1000000||
            !std::isfinite(parameters.angle_degrees)||
            parameters.angle_degrees<0||parameters.angle_degrees>180||
            !std::isfinite(thickness)||thickness<.001||thickness>1000000||
            !std::isfinite(k_factor)||k_factor<0||k_factor>1||
            (parameters.fixed_side!=1&&parameters.fixed_side!=-1))
        throw std::invalid_argument("Invalid Line Bend parameters.");
    const V a=sketch.world_point(first->x,first->y);
    const V b=sketch.world_point(last->x,last->y);
    const V along=unit(sub(b,a));
    const V normal=unit(sketch.resolved_normal);
    V left=unit(cross(normal,along));
    // fixed_side identifies the authored side on the left/right of the
    // persisted segment direction. The material tangent always points from
    // the fixed region into the moving region.
    const V tangent=scale(left,-parameters.fixed_side);
    // The Sketch plane side is the single authored bend direction. The
    // segment direction and fixed_side select which sheet region moves.
    const V radial=scale(normal,-1);
    const double radius=parameters.radius;
    kernel::LineBendRequest request;
    request.along=along;request.tangent=tangent;
    request.radial=radial;request.along_min=0;
    request.along_max=std::sqrt(dot(sub(b,a),sub(b,a)));
    request.radius=radius;
    request.neutral_radius=parameters.radius+k_factor*thickness;
    request.angle=parameters.angle_degrees*std::numbers::pi/180;
    request.thickness=thickness;
    // The authored line is the press-brake tool centre in the developed
    // sheet.  Keep half of the bend allowance on each side of it instead of
    // starting the cylindrical material region at the line.
    request.origin=sub(a,scale(tangent,
        request.angle*request.neutral_radius*.5));
    kernel::SheetMaterialDefinition material;
    material.kind=kernel::SheetMaterialDefinition::Kind::Cylinder;
    material.owner_id=feature.id;material.feature_owner_id=feature.id;
    material.origin=request.origin;material.along=request.along;
    material.tangent=request.tangent;material.radial=request.radial;
    material.radius=request.radius;
    material.neutral_radius=request.neutral_radius;
    material.angle=request.angle;material.thickness=thickness;
    material.continuation=1;
    material.unfolded=false;
    material.curved_source_id=segment->id;
    return {request,material};
}
}
kernel::LineBendRequest line_bend_request(const HistoryContainer& feature,
        const sketcher::Sketch& sketch,const SheetMetalDefaults& defaults) {
    return resolve(feature,sketch,defaults).request;
}
kernel::SheetMaterialDefinition line_bend_material_definition(
        const HistoryContainer& feature,const sketcher::Sketch& sketch,
        const SheetMetalDefaults& defaults) {
    return resolve(feature,sketch,defaults).material;
}
kernel::ViewerMesh line_bend_preview(const HistoryContainer& feature,
        const sketcher::Sketch& sketch,const SheetMetalDefaults& defaults,
        const kernel::ViewerMesh& input) {
    const auto resolved=resolve(feature,sketch,defaults);
    auto before=resolved.material;before.unfolded=true;
    const auto& after=resolved.material;
    const double material_length=after.angle*after.neutral_radius;
    const auto map=[&](V point) {
        auto coordinate=kernel::sheet_material::coordinates(before,point);
        if(coordinate.length<-1e-8)
            return point;
        coordinate.continuation=coordinate.length>material_length+1e-8;
        return kernel::sheet_material::point(after,coordinate);
    };
    kernel::ViewerMesh result;
    const int samples=std::max(1,static_cast<int>(
        std::ceil(feature.line_bend.angle_degrees/3)));
    std::size_t edge_index{};
    double minimum_depth=INFINITY,maximum_depth=-INFINITY;
    for(const auto& source:input.edges) {
        if(source.construction||source.overlay||source.parameter_seam||
                source.points.size()<2)continue;
        kernel::ViewerEdge edge;
        edge.reference={feature.id,
            "preview:boundary:"+std::to_string(edge_index++),{}};
        for(std::size_t segment=1;segment<source.points.size();++segment) {
            const auto first=source.points[segment-1];
            const auto last=source.points[segment];
            const auto c0=kernel::sheet_material::coordinates(before,first);
            const auto c1=kernel::sheet_material::coordinates(before,last);
            minimum_depth=std::min({minimum_depth,c0.depth,c1.depth});
            maximum_depth=std::max({maximum_depth,c0.depth,c1.depth});
            std::vector<double> stations{0,1};
            const auto split=[&](double from,double to,double boundary) {
                if(std::abs(to-from)<1e-12)return;
                const double station=(boundary-from)/(to-from);
                if(station>1e-9&&station<1-1e-9)
                    stations.push_back(station);
            };
            split(c0.length,c1.length,0);
            split(c0.length,c1.length,material_length);
            for(int sample=1;sample<samples;++sample)
                split(c0.length,c1.length,
                    material_length*sample/samples);
            std::ranges::sort(stations);
            stations.erase(std::unique(stations.begin(),stations.end(),
                [](double lhs,double rhs){return std::abs(lhs-rhs)<1e-10;}),
                stations.end());
            for(const double station:stations) {
                if(segment>1&&station==0)continue;
                edge.points.push_back(map(add(first,scale(sub(last,first),
                    station))));
            }
        }
        if(edge.points.size()>1)result.edges.push_back(std::move(edge));
    }
    if(std::isfinite(minimum_depth)&&std::isfinite(maximum_depth)) {
        for(const double depth:{minimum_depth,maximum_depth})
            for(const double length:{0.,material_length}) {
                kernel::ViewerEdge edge;
                edge.reference={feature.id,
                    "preview:bend-boundary:"+std::to_string(edge_index++),{}};
                edge.points={
                    map(kernel::sheet_material::point(before,
                        {resolved.request.along_min,length,depth,false})),
                    map(kernel::sheet_material::point(before,
                        {resolved.request.along_max,length,depth,false}))};
                result.edges.push_back(std::move(edge));
            }
    }
    kernel::ViewerAxis axis;
    axis.point=add(sub(resolved.request.origin,
            scale(resolved.request.radial,resolved.request.radius)),
        scale(resolved.request.along,
            (resolved.request.along_min+resolved.request.along_max)*.5));
    axis.direction=resolved.request.along;
    axis.display_length=resolved.request.along_max-
        resolved.request.along_min+2;
    axis.reference={feature.id,"line-bend:axis",{}};
    result.axes.push_back(std::move(axis));
    const V center=result.axes.back().point;
    const double dimension_radius=resolved.request.radius+
        resolved.request.thickness+std::max(5.,resolved.request.thickness*2);
    const V start=add(center,scale(resolved.request.radial,dimension_radius));
    const double sine=std::sin(resolved.request.angle);
    const double cosine=std::cos(resolved.request.angle);
    const V end=add(center,scale(add(
        scale(resolved.request.radial,cosine),
        scale(resolved.request.tangent,sine)),dimension_radius));
    kernel::ViewerDimension angle{center,center,start,end,
        feature.line_bend.angle_degrees,
        {feature.id,"parameter:angle",{}},"","°"};
    angle.kind=kernel::ViewerDimensionKind::Angular;
    angle.plane_normal=unit(cross(resolved.request.radial,
        resolved.request.tangent));
    angle.sweep_degrees=feature.line_bend.angle_degrees;
    angle.value_lock_key="angle";
    result.dimensions.push_back(std::move(angle));
    return result;
}
}

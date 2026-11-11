#include <zima/document/line_bend.hpp>
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
            parameters.angle_degrees<=0||parameters.angle_degrees>=360||
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
    const V radial=scale(normal,parameters.reverse?-1:1);
    const double radius=parameters.reverse?parameters.radius:
        parameters.radius+thickness;
    kernel::LineBendRequest request;
    request.origin=a;request.along=along;request.tangent=tangent;
    request.radial=radial;request.along_min=0;
    request.along_max=std::sqrt(dot(sub(b,a),sub(b,a)));
    request.radius=radius;
    request.neutral_radius=parameters.radius+k_factor*thickness;
    request.angle=parameters.angle_degrees*std::numbers::pi/180;
    request.thickness=thickness;
    kernel::SheetMaterialDefinition material;
    material.kind=kernel::SheetMaterialDefinition::Kind::Cylinder;
    material.owner_id=feature.id;material.feature_owner_id=feature.id;
    material.origin=request.origin;material.along=request.along;
    material.tangent=request.tangent;material.radial=request.radial;
    material.radius=request.radius;
    material.neutral_radius=request.neutral_radius;
    material.angle=request.angle;material.thickness=thickness;
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
}

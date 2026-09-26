#include <zima/document/part_document.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <algorithm>

namespace zima::document {
// Presentation and references are derived from the native work frame. These
// datums never require a body calculation or manufacture a degenerate solid.
zima::kernel::ViewerMesh PartDocument::feature_result_mesh(const HistoryContainer& feature) const {
    using namespace zima::kernel;
    ViewerMesh mesh;
    if(feature.feature_kind!=FeatureKind::Feature || feature.suppressed)return mesh;
    const auto sketch=std::ranges::find(sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
    if(sketch==sketches.end())return mesh;
    const auto origin=sketch->world_point(0,0),normal=sketch->normal();
    const auto along=[&](double length) {return Vec3{origin.x+length*normal.x,
        origin.y+length*normal.y,origin.z+length*normal.z};};
    mesh.points.push_back({origin,{feature.id,"point",{}},feature.name});
    mesh.points.back().display_owner_id=feature.id;
    mesh.original_references.points=mesh.points;
    // Keep the persisted point available to reference-taking commands, while
    // visibility switches affect presentation only, never reference identity.
    mesh.points.back().always_visible=feature.feature.show_point;
    if(!feature.feature.show_text)mesh.points.back().label.clear();
    if(feature.feature.type==FeatureType::Axis) {
        const double forward=feature.feature.effective_side(0).length;
        const double reverse=feature.feature.effective_side(1).length;
        const auto first=along(-reverse),last=along(forward);
        ViewerEdge edge;edge.points={first,last};edge.reference={feature.id,"axis",{}};
        edge.display_owner_id=feature.id;edge.construction=edge.overlay=edge.dash_dot=true;
        edge.measured_length=forward+reverse;
        mesh.edges.push_back(edge);mesh.original_references.edges.push_back(std::move(edge));
        mesh.axes.push_back({along((forward-reverse)*.5),normal,
            forward+reverse,{feature.id,"axis",{}}});
        mesh.original_references.axes.push_back({along((forward-reverse)*.5),normal,
            forward+reverse,{feature.id,"axis",{}}});
        for(const auto side:{FeatureSide::Start,FeatureSide::End}) {
            const auto parent=feature_side_parent_key({feature.feature_id,side,feature.container_origin.id});
            ViewerPoint point{side==FeatureSide::Start?first:last,
                {feature.id,"point:from:"+parent,{}}};
            point.display_owner_id=feature.id;
            mesh.points.push_back(point);mesh.original_references.points.push_back(std::move(point));
        }
    } else if(feature.feature.type==FeatureType::Plane) {
        // Match the existing screen-constant construction Plane marker.
        constexpr double half=2.5;
        const auto& x=sketch->resolved_x_axis;const auto& y=sketch->resolved_y_axis;
        const auto corner=[&](double a,double b){return Vec3{
            origin.x+a*x.x+b*y.x,origin.y+a*x.y+b*y.y,origin.z+a*x.z+b*y.z};};
        const std::vector<Vec3> corners{corner(-half,-half),corner(half,-half),
            corner(half,half),corner(-half,half)};
        ViewerEdge edge;edge.reference={feature.id,"border",{}};edge.display_owner_id=feature.id;
        edge.points=corners;edge.points.push_back(corners.front());edge.overlay=true;
        mesh.edges.push_back(std::move(edge));
        mesh.original_references.vertices=corners;
        mesh.original_references.triangles={0,1,2,0,2,3};
        mesh.original_references.triangle_references.assign(2,{feature.id,"plane",{}});
    }
    return mesh;
}
} // namespace zima::document

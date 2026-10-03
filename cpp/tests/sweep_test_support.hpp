#pragma once
#include <zima/document/part_document.hpp>
#include <zima/kernel/stable_id.hpp>

namespace zima::test_support {
inline document::HistoryContainer sweep_fixture(document::FeatureKind kind) {
    if (kind == document::FeatureKind::HelicalSweep) {
        auto c = document::PartDocument::create_helical_sweep_container();
        auto base = sketcher::Sketch::from_serialized(c.helical.sketches[0]);
        base.plane = sketcher::SketchPlane::XY; base.refresh_default_frame();
        c.helical.circle_id = base.add_circle(0, 0, 10);
        c.helical.start_point_id = base.add_point(10, 0);
        c.helical.sketches[0] = base.serialized();
        auto guide = sketcher::Sketch::from_serialized(c.helical.sketches[1]);
        static_cast<void>(guide.add_segment(0, 0, 0, 10));
        c.helical.sketches[1] = guide.serialized();
        auto section = sketcher::Sketch::from_serialized(c.helical.sketches[2]);
        static_cast<void>(section.add_circle(0, 0, .5));
        c.helical.sketches[2] = section.serialized();
        return c;
    }
    if (kind == document::FeatureKind::Sweep2D) {
        auto c = document::PartDocument::create_sweep2d_container();
        auto path = sketcher::Sketch::from_serialized(c.sweep2d.path_sketch);
        static_cast<void>(path.add_segment(0, 0, 0, 20));
        c.sweep2d.path_sketch = path.serialized();
        const auto station = document::PartDocument::sweep2d_route(c).stations.front();
        const auto index = document::PartDocument::ensure_sweep2d_profile(c, station.point_id, station.incoming);
        auto section = sketcher::Sketch::from_serialized(c.sweep2d.profiles[index].sketch_serialized);
        static_cast<void>(section.add_circle(0, 0, 2));
        c.sweep2d.profiles[index].sketch_serialized = section.serialized();
        return c;
    }
    auto c = document::PartDocument::create_sweep3d_container();
    for (auto origin : {kernel::Vec3{}, kernel::Vec3{0, 0, 20}}) {
        auto point = document::PartDocument::create_construction(document::ConstructionKind::Point);
        point.parent_construction_id = c.sweep3d.path.id; point.origin = origin;
        c.sweep3d.path.curve_points.push_back(point);
    }
    auto section = sketcher::Sketch::create_default(); section.owner_container_id = c.id;
    static_cast<void>(section.add_circle(0, 0, 2));
    c.sweep3d.profiles.push_back({kernel::make_stable_id(), c.sweep3d.path.curve_points.front().id, section.id, section.serialized()});
    return c;
}
// Shared native/GUI continuation inputs. TOP aligns local Z to the end face;
// the planar guide therefore starts along +v of its explicit owned YZ plane.
inline document::HistoryContainer continuation_sweep_fixture(document::FeatureKind kind) {
    auto child=sweep_fixture(kind);child.sweep_precision.custom_tolerance=1e-4;
    if(kind==document::FeatureKind::Sweep2D) {
        auto guide=sketcher::Sketch::from_serialized(child.sweep2d.path_sketch);guide.segments.clear();guide.points.clear();
        guide.plane=sketcher::SketchPlane::YZ;guide.plane_auto=false;guide.refresh_default_frame();
        child.sweep2d.path_plane=document::ConstructionReference{{},child.container_origin.id,"origin:plane:yz"};
        static_cast<void>(guide.add_bspline({{0,0},{0,10},{5,20},{15,25}}));child.sweep2d.path_sketch=guide.serialized();
        child.sweep2d.profiles.clear();document::PartDocument::reframe_sweep2d_sketches(child);
        const auto station=document::PartDocument::sweep2d_route(child).stations.front();
        const auto index=document::PartDocument::ensure_sweep2d_profile(child,station.point_id,station.incoming);
        auto profile=sketcher::Sketch::from_serialized(child.sweep2d.profiles[index].sketch_serialized);
        static_cast<void>(profile.add_circle(0,0,.2));child.sweep2d.profiles[index].sketch_serialized=profile.serialized();
    } else if(kind==document::FeatureKind::Sweep3D) {
        child.sweep3d.path.curve_points.clear();child.sweep3d.path.curve_rounding_enabled=true;
        for(const auto p:std::vector<kernel::Vec3>{{0,0,0},{0,0,20},{10,0,20}}) {
            auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
            point.parent_construction_id=child.sweep3d.path.id;point.origin=p;point.curve_radius=5;
            child.sweep3d.path.curve_points.push_back(point);
        }
        child.sweep3d.profiles.front().point_id=child.sweep3d.path.curve_points.front().id;
        auto profile=sketcher::Sketch::from_serialized(child.sweep3d.profiles.front().sketch_serialized);
        profile.circles.clear();profile.points.clear();static_cast<void>(profile.add_circle(0,0,.2));
        child.sweep3d.profiles.front().sketch_serialized=profile.serialized();
    } else if(kind==document::FeatureKind::HelicalSweep) {
        child.helical.pitch=5;
        auto profile=sketcher::Sketch::from_serialized(child.helical.sketches[2]);profile.circles.clear();profile.points.clear();
        static_cast<void>(profile.add_circle(0,0,.2));child.helical.sketches[2]=profile.serialized();
    } else throw std::invalid_argument("Unsupported continuation fixture");
    return child;
}
inline document::PartDocument standalone_sweep_sources(const document::HistoryContainer& feature) {

    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Sweep inputs"));
    if(feature.feature_kind==document::FeatureKind::HelicalSweep) {
        for(std::size_t i=0;i<feature.helical.sketches.size();++i) {
            auto sketch=sketcher::Sketch::from_serialized(feature.helical.sketches[i]);
            auto container=document::PartDocument::create_sketch_container();sketch.owner_container_id=container.id;
            if(i==0)container.placement=feature.placement;
            graph.insert({document::PartHistoryKind::Feature,container.id});
            part.history.push_back(std::move(container));part.sketches.push_back(std::move(sketch));
        }
        part.set_body_history(graph);part.resolve_constructions();return part;
    }
    if(feature.feature_kind==document::FeatureKind::Sweep2D) {
        auto path=sketcher::Sketch::from_serialized(feature.sweep2d.path_sketch);
        auto container=document::PartDocument::create_sketch_container();
        container.placement=feature.placement;path.owner_container_id=container.id;
        graph.insert({document::PartHistoryKind::Feature,container.id});
        part.history.push_back(std::move(container));part.sketches.push_back(std::move(path));
    } else {
        auto path=feature.sweep3d.path;path.parent_construction_id.clear();
        path.origin={feature.placement.x,feature.placement.y,feature.placement.z};
        path.rotation={feature.placement.rotation_x,feature.placement.rotation_y,feature.placement.rotation_z};
        path.absolute_rotation=path.rotation;
        graph.insert({document::PartHistoryKind::Construction,path.id});part.constructions.push_back(std::move(path));
    }
    for(const auto& profile:feature.feature_kind==document::FeatureKind::Sweep2D ? feature.sweep2d.profiles : feature.sweep3d.profiles) {
        auto sketch=sketcher::Sketch::from_serialized(profile.sketch_serialized);
        auto container=document::PartDocument::create_sketch_container();sketch.owner_container_id=container.id;
        graph.insert({document::PartHistoryKind::Feature,container.id});
        part.history.push_back(std::move(container));part.sketches.push_back(std::move(sketch));
    }
    part.set_body_history(graph);part.resolve_constructions();return part;
}
} // namespace zima::test_support

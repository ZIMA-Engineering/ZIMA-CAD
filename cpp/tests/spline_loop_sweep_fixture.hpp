#pragma once
#include <zima/document/part_document.hpp>
#include <zima/kernel/stable_id.hpp>

namespace zima::test_support {
inline document::HistoryContainer spline_loop_sweep_fixture(bool closed=true,
        double radius=128.4737008298249,double height=100.,double section_radius=14.48) {
    auto sweep=document::PartDocument::create_sweep3d_container();
    sweep.sweep3d.path.curve_type=document::Curve3DType::InterpolatingSpline;
    using Tangent=document::Curve3DTangentMode;
    const std::array positions{kernel::Vec3{radius,0,0},kernel::Vec3{0,radius,height},
        kernel::Vec3{-radius,0,0},kernel::Vec3{0,-radius,height},kernel::Vec3{radius,0,0}};
    const std::array tangents{Tangent::PositiveY,Tangent::NegativeX,Tangent::NegativeY,
        Tangent::PositiveX,Tangent::PositiveY};
    for(std::size_t i=0;i<(closed?5u:4u);++i) {
        auto p=document::PartDocument::create_construction(document::ConstructionKind::Point);
        p.parent_construction_id=sweep.sweep3d.path.id;p.origin=positions[i];
        p.curve_tangent_enabled=true;p.curve_tangent=tangents[i];
        sweep.sweep3d.path.curve_points.push_back(std::move(p));
    }
    auto profile=sketcher::Sketch::create_default();profile.owner_container_id=sweep.id;
    static_cast<void>(profile.add_circle(0,0,section_radius));
    sweep.sweep3d.profiles.push_back({kernel::make_stable_id(),
        sweep.sweep3d.path.curve_points.front().id,profile.id,profile.serialized()});
    if(!document::PartDocument::reframe_sweep3d_profile(sweep,0))
        throw std::runtime_error("Cannot frame the spline-loop profile");
    return sweep;
}
inline document::PartDocument spline_loop_cut_document(bool closed=true,double section=14.48) {
    auto doc=document::PartDocument::create_default();
    auto profile=sketcher::Sketch::create_default();static_cast<void>(profile.add_circle(0,0,128.4737008298249));
    auto cylinder=document::PartDocument::create_extrusion_container(profile.id);profile.owner_container_id=cylinder.id;
    cylinder.extrusion.length_forward=250;doc.sketches.push_back(profile);doc.history.push_back(cylinder);
    auto sweep=spline_loop_sweep_fixture(closed,128.4737008298249,100,section);
    sweep.combine_mode=document::CombineMode::Subtract;doc.history.push_back(sweep);
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Spline-loop Body"));
    graph.insert({document::PartHistoryKind::Feature,cylinder.id});graph.insert({document::PartHistoryKind::Feature,sweep.id});
    doc.set_body_history(graph);return doc;
}
}

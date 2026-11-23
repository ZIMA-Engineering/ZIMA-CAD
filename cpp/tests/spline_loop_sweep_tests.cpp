#include "spline_loop_sweep_fixture.hpp"
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <iostream>
#include <numbers>
#include <cmath>
#include <filesystem>
using namespace zima;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static kernel::Vec3 position(const kernel::Sweep3DRequest::PathSegment& s,double t) {
    const auto& a=s.bezier_control_points;const double u=1-t;
    return {u*u*u*a[0].x+3*u*u*t*a[1].x+3*u*t*t*a[2].x+t*t*t*a[3].x,
            u*u*u*a[0].y+3*u*u*t*a[1].y+3*u*t*t*a[2].y+t*t*t*a[3].y,
            u*u*u*a[0].z+3*u*u*t*a[1].z+3*u*t*t*a[2].z+t*t*t*a[3].z};
}
static double length(const kernel::Sweep3DRequest::PathSegment& s) {
    const auto& a=s.bezier_control_points;
    const auto speed=[&](double t) {const double u=1-t;
        return std::hypot(3*u*u*(a[1].x-a[0].x)+6*u*t*(a[2].x-a[1].x)+3*t*t*(a[3].x-a[2].x),
            3*u*u*(a[1].y-a[0].y)+6*u*t*(a[2].y-a[1].y)+3*t*t*(a[3].y-a[2].y),
            3*u*u*(a[1].z-a[0].z)+6*u*t*(a[2].z-a[1].z)+3*t*t*(a[3].z-a[2].z));};
    constexpr int count=2000;double sum=speed(0)+speed(1);
    for(int i=1;i<count;++i)sum+=(i%2?4:2)*speed(double(i)/count);
    return sum/(3*count);
}
static void verify(document::PartDocument doc,double section_radius,double section_area=0) {
    kernel::OcctKernel engine;const auto operations=doc.kernel_operations();
    auto tool=operations.back();tool.operation=kernel::BooleanOperation::Add;
    const auto& request=std::get<kernel::Sweep3DRequest>(tool.primitive);
    const auto tool_result=engine.evaluate_history({tool}).back();
    require(tool_result.calculation_errors.empty(),"Spline tube calculation failed");
    double route_length=0;for(const auto& segment:request.path_segments)route_length+=length(segment);
    const auto expected=(section_area>0?section_area:std::numbers::pi*section_radius*section_radius)*route_length;
    require(std::abs(tool_result.volume-expected)<expected*5e-4,"Spline tube lost material along its route");
    auto tool_shape=kernel::read_calculated_body_shape(tool_result);
    require(BRepCheck_Analyzer(tool_shape).IsValid(),"Spline tube is not a valid BRep");
    const auto result=engine.evaluate_history(operations);
    require(result.back().calculation_errors.empty(),"Spline-loop subtraction failed");
    require(result.back().volume<result.front().volume-expected*.2,"Spline-loop subtraction removed too little material");
    auto cut_shape=kernel::read_calculated_body_shape(result.back());
    require(BRepCheck_Analyzer(cut_shape).IsValid(),"Spline-loop subtraction is not a valid BRep");
    for(const auto& segment:request.path_segments)for(double t:{.2,.5,.8}) {
        auto p=position(segment,t);const auto radial=std::hypot(p.x,p.y);
        p.x*=1-section_radius*.35/radial;p.y*=1-section_radius*.35/radial;
        BRepClass3d_SolidClassifier tube(tool_shape,gp_Pnt(p.x,p.y,p.z),1e-6);
        BRepClass3d_SolidClassifier cut(cut_shape,gp_Pnt(p.x,p.y,p.z),1e-6);
        require(tube.State()==TopAbs_IN,"Spline tube does not cover every authored segment");
        require(cut.State()==TopAbs_OUT,"Spline-loop subtraction omitted an authored segment");
    }
    const auto file=std::filesystem::temp_directory_path()/"zima-spline-loop-sweep.prtz";
    doc.save(file,result);std::vector<kernel::BodyResult> reopened;
    const auto loaded=document::PartDocument::load(file,&reopened);
    require(loaded.serialized()==doc.serialized(),"Spline-loop save/reopen changed authored data");
    require(document::serialize_body_result(reopened.back())==document::serialize_body_result(result.back()),"Spline-loop save/reopen changed the result or references");
    const auto repeated=engine.evaluate_history(loaded.kernel_operations()).back();
    require(std::abs(repeated.volume-result.back().volume)<1e-6,"Spline-loop regeneration changed volume");
    std::filesystem::remove(file);
}
static void verify_cached_point_supports(bool flip) {
    kernel::OcctKernel engine;
    auto doc=test_support::spline_loop_cut_document();
    auto input=doc.kernel_operations();input.resize(1);
    const auto cylinder=engine.evaluate_history(input).back();
    const auto face=std::ranges::find_if(cylinder.mesh.triangle_references,[](const auto& ref) {
        return ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;
    });
    require(face!=cylinder.mesh.triangle_references.end(),"Cylinder support is missing");
    for(auto& point:doc.history.back().sweep3d.path.curve_points) {
        document::ConstructionReference ref{{},face->owner_id,face->semantic_key};
        ref.flip=flip;ref.offset=std::copysign(0.,flip?-1.:1.);ref.supports_offset=true;
        point.references={ref};
    }
    const auto original=doc.history.back().sweep3d.path.curve_points;
    const auto calculated=workspace::calculate_part_with_resolved_references(engine,doc);
    const auto file=std::filesystem::temp_directory_path()/"zima-spline-loop-supports.prtz";
    doc.save(file,calculated);std::vector<kernel::BodyResult> cache;
    auto loaded=document::PartDocument::load(file,&cache);
    workspace::Workspace live;live.add_part(loaded,cache);live.activate(loaded.document_id);
    const auto check=[&] {
        const auto& actual=live.open_part(loaded.document_id)->session.document().history.back().sweep3d.path;
        require(actual.reference_valid,"Regeneration invalidated the saved spline route");
        for(std::size_t i=0;i<original.size();++i) {
            const auto& point=actual.curve_points[i];
            require(point.reference_valid,"Saved display triangles replaced an analytic Point support");
            require(point.references==original[i].references&&
                std::signbit(point.references[0].offset)==std::signbit(original[i].references[0].offset),
                "Regeneration changed a Point reference or its signed side");
            require(std::hypot(point.origin.x-original[i].origin.x,point.origin.y-original[i].origin.y,
                point.origin.z-original[i].origin.z)<1e-7,"Regeneration moved a constrained Point");
        }
    };
    static_cast<void>(workspace::regenerate_part(live,engine,loaded.document_id));check();
    auto edited=live.open_part(loaded.document_id)->session.document().history.back();
    auto profile=sketcher::Sketch::from_serialized(edited.sweep3d.profiles.front().sketch_serialized);
    profile.circles.front().radius*=.8;edited.sweep3d.profiles.front().sketch_serialized=profile.serialized();
    workspace::commit_sweep(live,engine,loaded.document_id,edited,workspace::SweepEditMode::Replace);check();
    static_cast<void>(workspace::regenerate_part(live,engine,loaded.document_id));check();
    std::filesystem::remove(file);
}
int main()try {
    for(bool closed:{false,true})for(double section:{5.,14.48}) {
        std::cout<<"Circle closed="<<closed<<" radius="<<section<<std::endl;
        verify(test_support::spline_loop_cut_document(closed,section),section);
    }
    for(bool closed:{false,true})for(bool ellipse:{false,true}) {
        std::cout<<(ellipse?"Ellipse":"Rectangle")<<" closed="<<closed<<std::endl;
        auto doc=test_support::spline_loop_cut_document(closed);
        auto& profile=doc.history.back().sweep3d.profiles.front();
        auto sketch=sketcher::Sketch::from_serialized(profile.sketch_serialized);
        sketch.circles.clear();sketch.points.clear();
        if(ellipse)static_cast<void>(sketch.add_ellipse(0,0,8,0,0,5));
        else static_cast<void>(sketch.add_rectangle(-8,-5,8,5));
        profile.sketch_serialized=sketch.serialized();
        verify(doc,5,ellipse?40*std::numbers::pi:160);
    }
    for(bool flip:{false,true})verify_cached_point_supports(flip);
    std::cout<<"Spline-loop sweep volume, subtraction, native references and reopen passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

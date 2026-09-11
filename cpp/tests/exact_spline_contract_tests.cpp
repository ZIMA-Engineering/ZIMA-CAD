#include <zima/sketcher/curve_geometry.hpp>
#include <zima/kernel/occt_curve_data.hpp>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TopoDS.hxx>
#include <TopLoc_Location.hxx>
#include <zima/sketcher/sketch.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/mirror_geometry.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>
#include <numbers>
#include <iostream>
#include <stdexcept>
using namespace zima;
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){try{
    // Capture full-period, trimmed, reversed and located OCCT edges through
    // the same adapter as body calculation, then independently compare Value.
    TColgp_Array1OfPnt periodic_poles(1,6);
    TColStd_Array1OfReal periodic_knots(1,7);
    TColStd_Array1OfInteger periodic_mults(1,7);
    for(int i=1;i<=6;++i){double a=(i-1)*std::numbers::pi/3;
        periodic_poles.SetValue(i,gp_Pnt(10*std::cos(a),10*std::sin(a),i*0.1));}
    for(int i=1;i<=7;++i){periodic_knots.SetValue(i,i-1);periodic_mults.SetValue(i,1);}
    Handle(Geom_BSplineCurve) periodic=new Geom_BSplineCurve(periodic_poles,periodic_knots,periodic_mults,3,true);
    for(bool trimmed:{false,true})for(bool reverse:{false,true}) {
        auto edge=BRepBuilderAPI_MakeEdge(periodic,trimmed?0.25:0,trimmed?5.25:6).Edge();
        if(reverse)edge.Reverse();
        gp_Trsf pose;pose.SetTranslation(gp_Vec(17,-31,9));
        edge=TopoDS::Edge(edge.Moved(TopLoc_Location(pose)));
        BRepAdaptor_Curve source(edge);
        const auto data=kernel::capture_bspline_geometry(source);
        require(data.has_value(),"Periodic source was not captured");
        for(int i=0;i<=512;++i){double t=i/512.0;
            const auto a=source.Value(source.FirstParameter()+(source.LastParameter()-source.FirstParameter())*t);
            const auto b=kernel::bspline_value(*data,t);
            require(std::hypot(a.X()-b.x,a.Y()-b.y,a.Z()-b.z)<1e-10,"Captured periodic/trimmed edge changed shape");
        }
    }
    // Rational quadratic quarter circle: an independent analytic oracle.
    kernel::BSplineGeometry exact{2,{{1,0,0},{1,1,0},{0,1,0}},
        {2,2,2,7,7,7},{1,std::sqrt(0.5),1}};
    exact.validate();
    for(int i=0;i<=1000;++i){auto p=kernel::bspline_value(exact,i/1000.0);
        require(std::abs(p.x*p.x+p.y*p.y-1)<1e-13,"Rational curve left its exact circle");}
    auto reversed=exact;
    std::reverse(reversed.poles.begin(),reversed.poles.end());
    kernel::reverse_bspline_parameters(reversed.knots,reversed.weights);
    for(int i=0;i<=100;++i){const auto a=kernel::bspline_value(exact,i/100.0);
        const auto b=kernel::bspline_value(reversed,1-i/100.0);
        require(std::hypot(a.x-b.x,a.y-b.y)<1e-13,"Reversing spline changed its geometry");}
    kernel::BSplineGeometry uneven{1,{{0,0,0},{3,6,0},{8,2,0}}, {0,0,0.3,1,1},{1,2,1}};
    uneven.validate();const auto midpoint=kernel::bspline_value(uneven,0.15);
    require(std::hypot(midpoint.x-2,midpoint.y-4)<1e-13,"Nonuniform knots ignored");
    auto sketch=sketcher::Sketch::create_default();
    auto ref=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
    ref.source_document_id="source";ref.source_owner_id="import";ref.source_semantic_key="source-quarter-circle";
    // Deliberately coarse display cache must not define the projected curve.
    ref.cached_points={{1,0},{0,1}};ref.exact_spline=exact;
    sketch.add_external_reference(ref);
    const auto id=sketch.add_external_profile_geometry(ref.id);
    require(sketch.bsplines.size()==1,"Exact source was replaced with a sampled line");
    const auto poles=sketch.bsplines.front().control_point_ids;
    auto restored=sketcher::Sketch::from_serialized(sketch.serialized());
    require(restored.bsplines==sketch.bsplines && restored.external_references==sketch.external_references,
        "Exact projected spline did not survive persistence");
    for(const auto& e:restored.viewer_mesh().edges)if(e.reference.semantic_key=="bspline:"+id)
        for(const auto& p:e.points)require(std::abs(p.x*p.x+p.y*p.y-1)<1e-13,"Sketch viewer changed rational geometry");
    kernel::ViewerReferenceGeometry geometry;
    kernel::ViewerEdge edge;edge.reference={"import","source-quarter-circle",{}};
    edge.points={{1,0,0},{0,1,0}};edge.exact_spline=exact;geometry.edges.push_back(edge);
    const auto packet=document::serialize_viewer_reference_geometry(geometry);
    require(document::load_viewer_reference_geometry(packet).edges.front().exact_spline==edge.exact_spline,
        "Reference packet lost exact data");
    kernel::BodyResult body;body.mesh.original_references=geometry;
    require(document::load_body_result(document::serialize_body_result(body)).mesh.original_references.edges.front().exact_spline==edge.exact_spline,
        "Body packet lost exact data");
    kernel::ViewerMesh mesh;mesh.original_references=geometry;
    const auto mirrored=kernel::mirrored_viewer_mesh(mesh,{{2,0,0},{1,0,0}});
    const auto m=kernel::bspline_value(*mirrored.original_references.edges.front().exact_spline,0.5);
    require(std::abs(m.x-(4-std::sqrt(0.5)))<1e-13,"Mirror failed to transform exact curve");
    auto updated=geometry;
    for(auto& p:updated.edges.front().points)p.x+=0.01;
    for(auto& p:updated.edges.front().exact_spline->poles)p.x+=0.01;
    require(restored.refresh_external_references("source",updated),"Small source change was ignored");
    require(restored.bsplines.front().id==id && restored.bsplines.front().control_point_ids==poles,
        "Source change replaced native curve identity");
    require(std::abs(restored.find_point(poles.front())->x-1.01)<1e-12,"Source change failed to update poles");
    const auto retained=restored.points;
    require(restored.refresh_external_references("source",{}),"Missing source was ignored");
    require(restored.external_references.front().broken && restored.points==retained,
        "Missing source destroyed last valid geometry");
    restored.remove_geometry(ref.id);
    require(restored.external_references.empty() && restored.import_blocks.empty() && restored.points==retained &&
        restored.bsplines.front().id==id,"Detach changed native geometry");
    // Closed profile calculation verifies knots and rational weights reach OCCT.
    sketch.remove_geometry(ref.id);
    static_cast<void>(sketch.add_segment(0,1,0,0));
    static_cast<void>(sketch.add_segment(0,0,1,0));
    auto doc=document::PartDocument::create_default();doc.sketches.push_back(sketch);
    doc.history.push_back(document::PartDocument::create_extrusion_container(sketch.id));
    auto operations=doc.kernel_operations();
    auto& request=std::get<kernel::ExtrusionRequest>(operations.front().primitive);
    request.direction={0,0,10};
    kernel::OcctKernel kernel;
    auto bodies=kernel.evaluate_history(operations);
    require(std::abs(bodies.back().volume-2.5*std::numbers::pi)<1e-7,"Rational profile extrusion volume is wrong");
    bool captured=false;
    for(const auto& e:bodies.back().mesh.original_references.edges)if(e.exact_spline){
        e.exact_spline->validate();captured=true;}
    require(captured,"Body calculation did not persist its original spline curves");
    const auto before=kernel::history_fingerprint(operations,operations.size());
    auto& profile=std::get<kernel::ExtrusionRequest::CurvedProfile>(request.outer_profile);
    for(auto& c:profile.curves)if(auto* s=std::get_if<kernel::ExtrusionRequest::BSplineCurve>(&c))s->weights[1]+=0.01;
    require(before!=kernel::history_fingerprint(operations,operations.size()),"Curve weights omitted from cache fingerprint");
    auto projected=sketcher::Sketch::create_default();projected.add_external_reference(ref);
    const auto own=projected.add_external_profile_geometry(ref.id);
    const auto offset_id=projected.add_offset(own,.1,true);
    static_cast<void>(projected.retain_curve_intervals(own,{{.1,.9}}));
    require(projected.refresh_external_references("source",updated),"Trimmed projection failed to refresh");
    const auto followed=projected.supporting_curve(offset_id);
    const auto expected=sketcher::offset_curve_geometry(*updated.edges.front().exact_spline,-.1);
    for(int i=0;i<=100;++i){const auto a=kernel::bspline_value(followed,i/100.),b=kernel::bspline_value(expected,i/100.);require(std::hypot(a.x-b.x,a.y-b.y)<1e-8,"Offset lost trimmed projected source");}
    require(!projected.external_references.front().broken,"Trim broke source projection");
    auto ring=sketcher::Sketch::create_default();const auto circle_id=ring.add_circle(0,0,10);
    static_cast<void>(ring.add_offset(circle_id,2,false));
    auto ring_doc=document::PartDocument::create_default();ring_doc.sketches.push_back(ring);
    ring_doc.history.push_back(document::PartDocument::create_extrusion_container(ring.id));
    auto ring_ops=ring_doc.kernel_operations();std::get<kernel::ExtrusionRequest>(ring_ops.front().primitive).direction={0,0,10};
    const auto ring_bodies=kernel.evaluate_history(ring_ops);
    require(std::abs(ring_bodies.back().volume-360*std::numbers::pi)<1e-6,"Offset circle extrusion is not exact");
    for(bool swap:{false,true})for(bool reverse:{false,true})for(bool flip:{false,true}) {
        auto ellipse_sketch=sketcher::Sketch::create_default();
        const auto ellipse_id=ellipse_sketch.add_ellipse(0,0,swap?5:10,0,0,swap?10:5);
        if(reverse){auto& ellipse=ellipse_sketch.ellipses.front();ellipse.reversed=true;ellipse_sketch.find_point(ellipse.minor_point_id)->y*=-1;}
        ellipse_sketch.validate();static_cast<void>(ellipse_sketch.add_offset(ellipse_id,1,flip));
        auto ellipse_doc=document::PartDocument::create_default();ellipse_doc.sketches.push_back(ellipse_sketch);
        ellipse_doc.history.push_back(document::PartDocument::create_extrusion_container(ellipse_sketch.id));
        auto ellipse_ops=ellipse_doc.kernel_operations();std::get<kernel::ExtrusionRequest>(ellipse_ops.front().primitive).direction={0,0,10};
        const auto ellipse_bodies=kernel.evaluate_history(ellipse_ops);
        const double perimeter=40*std::comp_ellint_2(std::sqrt(.75));
        const double expected=(perimeter+((reverse!=flip)?1:-1)*std::numbers::pi)*10;
        require(std::abs(ellipse_bodies.back().volume-expected)<.01,"Ellipse offset profile orientation/axis order is wrong");
    }
    for(double distance:{.001,.0001}) {
        auto narrow=sketcher::Sketch::create_default();const auto circle=narrow.add_circle(0,0,10);
        static_cast<void>(narrow.add_offset(circle,distance,false));
        auto part=document::PartDocument::create_default();part.sketches.push_back(narrow);
        part.history.push_back(document::PartDocument::create_extrusion_container(narrow.id));
        auto operations=part.kernel_operations();std::get<kernel::ExtrusionRequest>(operations.front().primitive).direction={0,0,10};
        const auto result=kernel.evaluate_history(operations);
        const double expected=std::numbers::pi*(20*distance-distance*distance)*10;
        require(std::abs(result.back().volume-expected)<1e-6,"Small circular offset was merged or misclassified");
    }
    std::cout<<"Exact spline projection, persistence, refresh, detach and extrusion passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

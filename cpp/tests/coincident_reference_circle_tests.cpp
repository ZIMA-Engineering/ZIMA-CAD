#include <zima/document/document_session.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <zima/sketcher/external_curve_center.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <zima/kernel/stable_id.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::size_t crossings(const sketcher::Sketch& sketch,const std::string& first,const std::string& second) {
    const auto mesh=sketch.viewer_mesh();
    const auto forward="sketch_intersection:"+first+"||"+second;
    const auto backward="sketch_intersection:"+second+"||"+first;
    return std::ranges::count_if(mesh.points,[&](const auto& point) {
        return point.reference.semantic_key==forward||point.reference.semantic_key==backward;
    });
}
sketcher::SketchExternalReference reference(double x,double y,double radius,unsigned samples,bool reverse,bool arc) {
    auto result=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
    result.source_document_id="read-only-source";result.source_owner_id="original-circle";
    result.source_semantic_key="circular-rim";
    result.exact_spline=sketcher::circular_curve_geometry({x,y,0},radius,.17,arc?4.8:.17+2*std::numbers::pi);
    for(unsigned i=0;i<=samples;++i) {
        const auto point=kernel::bspline_value(*result.exact_spline,double(i)/samples);
        result.cached_points.push_back({point.x,point.y});
    }
    if(reverse) {
        std::ranges::reverse(result.cached_points);std::ranges::reverse(result.exact_spline->poles);
        kernel::reverse_bspline_parameters(result.exact_spline->knots,result.exact_spline->weights);
    }
    return result;
}
void verify_cylinder_refresh() {
    auto sketch=sketcher::Sketch::create_default();sketch.plane_offset=123;
    auto source=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Face);
    source.source_document_id="cylinder-document";source.source_owner_id="cylinder";source.source_semantic_key="partial-side";
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;
    surface->origin={17,-9,-500};surface->axis={0,0,-1};surface->radius=7;surface->axial_min=0;surface->axial_max=2;
    kernel::ViewerReferenceGeometry geometry;kernel::FaceReference face{"cylinder","partial-side",{}};face.surface=surface;
    geometry.triangle_references={face};
    check(sketch.cache_external_cylinder_support(source,geometry),"Reversed cylinder axis/remote Sketch plane lost its support");
    sketch.add_external_reference(source);
    for(double radius:{13.,4.,7.}) {
        surface->radius=radius;check(sketch.refresh_external_references("cylinder-document",geometry),"Changed cylinder support was not refreshed");
        const auto& current=sketch.external_references.front();const auto circle=sketcher::external_reference_circle(current);
        check(current.id==source.id&&circle&&std::hypot((*circle)[0]-17,(*circle)[1]+9)<1e-9&&std::abs((*circle)[2]-radius)<1e-9,"Cylinder refresh lost identity or analytic center/radius");
        for(unsigned i=0;i<=1024;++i) {
            const auto point=kernel::bspline_value(*current.exact_spline,double(i)/1024);
            check(std::abs(std::hypot(point.x-17,point.y+9)-radius)<1e-9,"Full cylinder support does not satisfy its circle equation");
        }
        const auto serialized=sketch.serialized();
        check(!sketch.refresh_external_references("cylinder-document",geometry)&&sketch.serialized()==serialized,"Unchanged cylinder support introduced a change");
        check(sketcher::Sketch::from_serialized(serialized).serialized()==serialized,"Cylinder support exact geometry did not round-trip");
        const auto mesh=sketch.viewer_mesh();
        check(std::ranges::any_of(mesh.edges,[&](const auto& edge){return edge.reference.semantic_key=="external_face:"+source.id&&edge.exact_spline.has_value();}),"Cylinder View edge lost its exact supporting curve");
    }
    auto old_reference=source;old_reference.exact_spline.reset();old_reference.cached_points.clear();old_reference.cached_paths={{{1,2},{3,4}}};
    auto old_sketch=sketcher::Sketch::create_default();old_sketch.add_external_reference(old_reference);
    static_cast<void>(old_sketch.refresh_external_references("cylinder-document",geometry));
    check(!old_sketch.external_references.front().exact_spline&&old_sketch.external_references.front().cached_paths==old_reference.cached_paths,
        "New cylinder support rewrote a previously stored face reference");
    const auto valid=sketch.external_references.front();
    check(sketch.refresh_external_references("cylinder-document",{})&&sketch.external_references.front().broken&&
        sketch.external_references.front().exact_spline==valid.exact_spline,"Lost source discarded its last valid support");
    check(sketch.refresh_external_references("cylinder-document",geometry)&&sketch.external_references.front()==valid,"Restored cylinder source changed reference identity");
}
void verify(unsigned& cases) {
    for(bool cylindrical_face:{false,true})for(unsigned samples:{8u,17u,57u,128u})for(bool reverse:{false,true})
    for(bool arc:{false,true})for(bool fixed:{false,true})for(bool diameter:{false,true})for(bool locked:{false,true})
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ}) {
        auto sketch=sketcher::Sketch::create_default();sketch.plane=plane;sketch.refresh_default_frame();
        const double radius=31.331971171734438;
        auto source=reference(173,-81,radius,samples,reverse,arc);
        if(cylindrical_face) {
            source.kind=sketcher::ExternalReferenceKind::Face;
            auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;
            surface->origin=sketch.world_point(173,-81);surface->axis=sketch.normal();surface->radius=radius;
            kernel::ViewerReferenceGeometry geometry;
            kernel::FaceReference face{source.source_owner_id,source.source_semantic_key,{}};face.surface=surface;
            geometry.triangle_references={face};
            check(sketch.cache_external_cylinder_support(source,geometry),"Analytic cylinder support was not resolved");
            const auto saved=source;
            check(sketch.cache_external_cylinder_support(source,geometry)&&source==saved,"Unchanged cylinder support changed persisted data");
            auto oblique=sketch;oblique.plane=plane==sketcher::SketchPlane::XY?sketcher::SketchPlane::XZ:sketcher::SketchPlane::XY;
            oblique.refresh_default_frame();check(!oblique.cache_external_cylinder_support(source,geometry)&&source==saved,"Oblique cylinder gained a false circular support");
        }
        sketch.add_external_reference(source);
        const auto circle=sketch.add_circle(173,-81,radius);
        const auto center=sketch.circles.front().center_point_id;
        if(fixed)sketch.set_point_fixed(center,true);
        auto dimension=diameter?sketch.create_circle_diameter_dimension(circle):sketch.create_circle_radius_dimension(circle);
        dimension.locked=locked;sketch.apply_dimension(dimension);
        for(double next:{radius,radius+5,radius-3,radius}) {
            static_cast<void>(sketch.set_dimension_value(dimension.id,next*(diameter?2:1)));
            check(std::abs(sketch.circles.front().radius-next)<1e-7,"Successive radius/diameter edits lost their equation");
            const auto saved=sketch.serialized();
            const auto count=crossings(sketch,source.id,circle);
            check(sketch.serialized()==saved,"Picking geometry changed the Sketch");
            check(sketch.coincident_circular_support(source.id,circle)==sketch.coincident_circular_support(circle,source.id),"Support comparison depends on selection order");
            check(next!=radius||count==0,"Coincident circle offered display-chord vertices as intersections");
            check(sketch.external_references.front()==source,"Query/dimension edit changed the read-only source");
        }
        const auto valid=sketch.serialized();
        check(!sketch.set_dimension_value(dimension.id,-1)&&sketch.serialized()==valid,"Invalid negative radial dimension changed the document");
        for(const auto target:{std::array{183.,-76.},std::array{165.,-92.},std::array{173.,-81.}}) {
            const auto before=sketch.serialized();const bool moved=sketch.move_point(center,target[0],target[1]);
            const auto* actual=sketch.find_point(center);
            if(fixed)check(!moved&&before==sketch.serialized(),"Fully constrained circle center escaped during drag");
            else check(moved&&std::hypot(actual->x-target[0],actual->y-target[1])<1e-7,"Free circle did not follow repeated center dragging");
            check(std::abs(sketch.circles.front().radius-radius)<1e-7,"Center drag escaped the driving radius");
            check(sketch.external_references.front()==source,"Dragging changed external support");
        }
        check(crossings(sketch,source.id,circle)==0,"Return drag restored false coincident-circle points");
        auto reopened=sketcher::Sketch::from_serialized(sketch.serialized());
        check(reopened.serialized()==sketch.serialized()&&crossings(reopened,source.id,circle)==0,"Reopen changed circle type/identity or picking");
        // A reference dimension measures the existing circle; it does not
        // confer ownership of the external source or introduce editing poles.
        reopened.dimensions.front().driving=false;reopened.dimensions.front().locked=false;
        static_cast<void>(reopened.solve());
        check(reopened.bsplines.empty()&&reopened.circles.size()==1&&crossings(reopened,source.id,circle)==0,"Reference dimension changed the native circle");
        const auto measured=reopened.serialized();
        static_cast<void>(reopened.set_dimension_value(dimension.id,999));
        check(reopened.serialized()==measured,"A measured radius/diameter drove geometry");
        ++cases;
    }
    {
        auto sketch=sketcher::Sketch::create_default();auto source=reference(10,12,8,57,false,false);
        sketch.add_external_reference(source);const auto circle=sketch.add_circle(10,12,8);
        check(sketch.coincident_circular_support(source.id,circle),"Certified circle was not recognized");
        sketch.external_references.front().exact_spline->poles[1].x+=.002;
        check(!sketch.coincident_circular_support(source.id,circle),"Noncircular exact curve was classified from stale circular display samples");
        sketch.external_references.front()=source;sketch.external_references.front().broken=true;
        check(!sketch.coincident_circular_support(source.id,circle),"Broken reference was used as an active circular support");
        sketch.external_references.front()=source;
        for(const double difference:{.001,-.001}) {
            sketch.circles.front().radius=8+difference;
            check(!sketch.coincident_circular_support(source.id,circle)&&
                crossings(sketch,source.id,circle)==0,
                "Concentric separated circles produced false crossings");
        }
        ++cases;
    }
    {
        auto sketch=sketcher::Sketch::create_default();
        auto left=reference(-20,0,10,57,false,false);
        auto right=reference(20,0,10,128,true,false);
        left.kind=right.kind=sketcher::ExternalReferenceKind::Face;
        left.source_semantic_key="left-rim";
        right.source_semantic_key="right-rim";
        left.cached_paths={left.cached_points};left.cached_points.clear();
        right.cached_paths={right.cached_points};right.cached_points.clear();
        sketch.add_external_reference(left);sketch.add_external_reference(right);
        const auto left_center=sketch.add_point(-20,0);
        const auto right_center=sketch.add_point(20,0);
        static_cast<void>(sketch.add_point_reference_constraint(
            left_center,sketcher::external_center_point_id(left.id)));
        static_cast<void>(sketch.add_point_reference_constraint(
            right_center,sketcher::external_center_point_id(right.id)));
        const auto axis=sketch.add_segment(-20,0,20,0);
        sketch.set_geometry_construction(axis,true);
        sketch.set_segment_centerline(axis,true);
        const auto original=sketch.add_segment(-12,6,12,6);
        const auto mirrored=sketch.add_segment(-12,-6,12,-6);
        const auto original_line=std::ranges::find(
            sketch.segments,original,&sketcher::SketchSegment::id);
        const auto mirrored_line=std::ranges::find(
            sketch.segments,mirrored,&sketcher::SketchSegment::id);
        const auto original_first=original_line->first_point_id;
        const auto original_second=original_line->second_point_id;
        const auto mirrored_first=mirrored_line->first_point_id;
        const auto mirrored_second=mirrored_line->second_point_id;
        static_cast<void>(sketch.add_point_on_line_constraint(
            original_first,left.id));
        static_cast<void>(sketch.add_point_on_line_constraint(
            original_second,right.id));
        static_cast<void>(sketch.add_segment_pair_constraint(
            axis,original,sketcher::ConstraintKind::Parallel));
        static_cast<void>(sketch.add_symmetric_constraint(
            original_first,mirrored_first,axis));
        static_cast<void>(sketch.add_symmetric_constraint(
            original_second,mirrored_second,axis));
        const auto arc=sketch.add_arc(-20,0,-10,0,-20,10);
        static_cast<void>(sketch.add_equal_radius_constraint(left.id,arc));
        auto distance=sketch.create_line_pair_dimension(
            original,mirrored,sketcher::DimensionKind::DistanceLine);
        sketch.apply_dimension(distance);
        const auto measured_distance=[&] {
            const auto a=std::ranges::find(
                sketch.segments,original,&sketcher::SketchSegment::id);
            const auto b=std::ranges::find(
                sketch.segments,mirrored,&sketcher::SketchSegment::id);
            const auto* p=sketch.find_point(a->first_point_id);
            const auto* q=sketch.find_point(a->second_point_id);
            const auto* r=sketch.find_point(b->first_point_id);
            const double dx=q->x-p->x,dy=q->y-p->y;
            return std::abs(dx*(r->y-p->y)-dy*(r->x-p->x))/
                std::hypot(dx,dy);
        };
        for(const double value:{14.,8.,12.,10.,12.}) {
            check(sketch.set_dimension_value(distance.id,value),
                "Mirrored external-circle line distance rejected an edit");
            const auto solved=sketch.solve();
            check(solved.status==sketcher::SolveStatus::UnderConstrained&&
                solved.remaining_degrees_of_freedom>0&&
                solved.maximum_residual<1.0e-8&&
                std::abs(measured_distance()-value)<1.0e-7,
                "Mirrored external-circle line distance did not solve exactly");
        }
        const auto external_before=sketch.external_references;
        const auto arc_item=std::ranges::find(
            sketch.arcs,arc,&sketcher::SketchArc::id);
        const auto arc_center_id=arc_item->center_point_id;
        const auto arc_start_id=arc_item->start_point_id;
        for(const double angle:{.25,.4,.15}) {
            const auto* center=sketch.find_point(arc_center_id);
            check(sketch.move_point(arc_start_id,
                    center->x+10*std::cos(angle),
                    center->y+10*std::sin(angle)),
                "Free arc endpoint on the mirrored reference fixture did not drag");
            check(sketch.external_references==external_before,
                "Dragging changed an external circular source");
        }
        auto reopened=sketcher::Sketch::from_serialized(sketch.serialized());
        const auto solved=reopened.solve();
        check(solved.status==sketcher::SolveStatus::UnderConstrained&&
            solved.maximum_residual<1.0e-8&&
            crossings(reopened,"sketch_axis:x",left.id)==2,
            "Mirrored external-circle fixture changed after reopen");
        ++cases;
    }
    {
        auto source_sketch=sketcher::Sketch::create_default();
        const auto source_arc=source_sketch.add_arc(3,-7,0,-7,3,-10);
        const auto source_mesh=source_sketch.viewer_mesh();
        const auto source_edge=std::ranges::find_if(
            source_mesh.edges,[&](const auto& edge) {
                return edge.reference.semantic_key=="arc:"+source_arc;
            });
        check(source_edge!=source_mesh.edges.end(),
            "Double-tangent trim source arc is missing");
        auto external=sketcher::Sketch::create_external_reference(
            sketcher::ExternalReferenceKind::Edge);
        external.source_document_id="read-only-source";
        external.source_owner_id=source_sketch.id;
        external.source_semantic_key="arc:"+source_arc;
        external.exact_spline=source_edge->exact_spline;
        for(const auto& point:source_edge->points)
            external.cached_points.push_back(
                source_sketch.local_point(point));
        auto sketch=sketcher::Sketch::create_default();
        auto vertical=sketcher::Sketch::create_external_reference(
            sketcher::ExternalReferenceKind::Edge);
        vertical.source_document_id="read-only-source";
        vertical.source_owner_id="vertical-source";
        vertical.source_semantic_key="segment:vertical";
        vertical.infinite=true;
        vertical.cached_points={{-20,-20},{-20,20}};
        sketch.add_external_reference(vertical);
        sketch.add_external_reference(external);
        const auto circle=sketch.add_circle(
            -14.113200233087127,-10.6,13.633686118333486);
        static_cast<void>(sketch.add_tangent_constraint(
            "sketch_axis:x",circle));
        static_cast<void>(sketch.add_tangent_constraint(
            external.id,circle));
        const auto* center=sketch.find_point(
            sketch.circles.front().center_point_id);
        const auto topology=sketcher::sketch_trim_topology(sketch,false);
        const auto removed=sketcher::nearest_sketch_trim_piece(topology,
            {center->x-sketch.circles.front().radius,center->y},.01);
        check(removed.has_value(),
            "Double-tangent circle has no removable trim piece");
        static_cast<void>(sketcher::apply_sketch_trim(sketch,{*removed}));
        check(sketch.circles.empty()&&sketch.arcs.size()==1,
            "Double-tangent circle trim did not retain one native arc");
        sketch=sketcher::Sketch::from_serialized(sketch.serialized());
        const auto baseline=sketch.serialized();
        const auto arc=sketch.arcs.front();
        for(const auto& point_id:{arc.center_point_id,arc.start_point_id,
                arc.end_point_id}) {
            auto candidate=sketcher::Sketch::from_serialized(baseline);
            const auto* dragged=candidate.find_point(point_id);
            const auto* original_center=candidate.find_point(
                candidate.arcs.front().center_point_id);
            const auto before=*original_center;
            check(candidate.move_point(point_id,
                    dragged->x-1.000000648924618,
                    dragged->y-.250008536347602),
                "Double-tangent trimmed arc point rejected dragging");
            center=candidate.find_point(candidate.arcs.front().center_point_id);
            check(std::hypot(center->x-before.x,center->y-before.y)>1e-5,
                "Double-tangent trimmed arc point drag reported no movement");
            check(std::abs(center->y+candidate.arcs.front().radius)<1e-8 &&
                    std::abs(std::hypot(center->x-3,center->y+7)-
                        candidate.arcs.front().radius-3)<1e-8,
                "Double-tangent trimmed arc drag lost a tangency");
            for(const auto& endpoint:{candidate.arcs.front().start_point_id,
                    candidate.arcs.front().end_point_id}) {
                const auto* value=candidate.find_point(endpoint);
                check(std::abs(value->x+20)<1e-8 &&
                        std::abs(std::hypot(value->x-center->x,
                            value->y-center->y)-candidate.arcs.front().radius)<1e-8,
                    "Double-tangent trimmed arc drag lost an endpoint support");
            }
            const auto solved=candidate.solve();
            check(solved.status==sketcher::SolveStatus::UnderConstrained &&
                    solved.maximum_residual<1e-8 &&
                    candidate.external_references==sketch.external_references,
                "Double-tangent trimmed arc drag changed its exact sources");
        }
        ++cases;
    }
    for(bool reverse:{false,true}) {
        auto sketch=sketcher::Sketch::create_default();auto source=reference(15,27,12,57,reverse,false);
        sketch.add_external_reference(source);
        const auto circle=sketch.add_external_profile_geometry(source.id);
        check(sketch.circles.size()==1&&sketch.bsplines.empty()&&crossings(sketch,source.id,circle)==0,"Reference-to-contour did not retain one native circle");
        const auto saved=sketch.serialized();const auto center=sketch.circles.front().center_point_id;
        for(const auto target:{std::array{18.,30.},std::array{9.,21.}})
            check(!sketch.move_point(center,target[0],target[1])&&sketch.serialized()==saved,"Source-bound circular contour escaped repeated dragging");
        auto removed_contour=sketch;
        removed_contour.remove_geometry(circle);
        check(removed_contour.external_references==std::vector{source} &&
                removed_contour.import_blocks.empty() &&
                removed_contour.circles.empty(),
            "Deleting a circular Reference Profile changed its source or left an owned contour");
        auto dimension=sketch.create_circle_radius_dimension(circle);dimension.driving=false;sketch.apply_dimension(dimension);
        check(std::abs(sketch.dimensions.front().value-12)<1e-8,"Source-bound circle cannot be measured by radius");
        ++cases;
    }
    for(bool reverse:{false,true}) {
        auto sketch=sketcher::Sketch::create_default();auto source=reference(15,27,12,57,reverse,false);
        sketch.add_external_reference(source);const auto arc=sketch.add_arc(15,27,27,27,15,39);
        const auto points=sketch.points;
        check(crossings(sketch,source.id,arc)==0&&sketch.points==points,"Coincident arc gained artificial points or lost its native endpoints");
        const auto& native=sketch.arcs.front();
        for(const auto& id:{native.start_point_id,native.end_point_id}) {
            const auto* point=sketch.find_point(id);
            check(point&&std::abs(std::hypot(point->x-15,point->y-27)-12)<1e-8,"Coincident arc endpoint escaped its circle");
        }
        ++cases;
    }
    for(bool reverse:{false,true}) {
        auto sketch=sketcher::Sketch::create_default();
        auto first=reference(15,27,12,57,reverse,false),second=reference(15,27,12,128,!reverse,false);
        second.source_semantic_key="second-rim";
        sketch.add_external_reference(first);sketch.add_external_reference(second);
        check(crossings(sketch,first.id,second.id)==0,"Coincident external rims offered artificial crossings");
        const auto circle=sketch.add_circle(15,27,12);
        const auto pieces=sketcher::sketch_trim_topology(sketch,false);
        check(pieces.size()==1&&pieces.front().geometry_id==circle&&pieces.front().closed,"Coincident read-only supports split the trim contour");
        static_cast<void>(sketch.add_segment(15,7,15,47));
        const auto split=sketcher::sketch_trim_topology(sketch,false);
        const auto piece=std::ranges::find_if(split,[&](const auto& p){return p.geometry_id==circle;});
        check(piece!=split.end()&&std::ranges::count(split,circle,&sketcher::SketchTrimPiece::geometry_id)==2 &&
            std::abs(piece->end-piece->start-.5)<1e-8,"Actual cutting line no longer splits a referenced circle");
        static_cast<void>(sketcher::apply_sketch_trim(sketch,{*piece}));
        check(sketch.external_references==std::vector{first,second}&&sketch.circles.empty()&&sketch.arcs.size()==1&&sketch.bsplines.empty(),"Trim lost native arc type or changed its external cutters");
        ++cases;
    }
    // The document session owns Undo/Redo; read-only packet preparation owns
    // neither revisions nor transactions.
    auto document=document::PartDocument::create_default();
    auto source_sketch=sketcher::Sketch::create_default();const auto source_circle=source_sketch.add_circle(7,11,9);
    auto source_owner=document::PartDocument::create_sketch_container();source_sketch.owner_container_id=source_owner.id;
    source_owner.feature.sketch_id=source_sketch.id;document.history.push_back(source_owner);document.sketches.push_back(source_sketch);
    auto sketch=sketcher::Sketch::create_default();auto source=reference(7,11,9,57,false,false);
    source.source_document_id=document.document_id;source.source_owner_id=source_sketch.id;source.source_semantic_key="circle:"+source_circle;
    sketch.add_external_reference(source);const auto circle=sketch.add_circle(7,11,9);
    auto dimension=sketch.create_circle_radius_dimension(circle);sketch.apply_dimension(dimension);
    auto owner=document::PartDocument::create_sketch_container();sketch.owner_container_id=owner.id;owner.feature.sketch_id=sketch.id;
    document.history.push_back(owner);document.sketches.push_back(sketch);
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Circular support"));
    graph.insert({document::PartHistoryKind::Feature,source_owner.id});graph.insert({document::PartHistoryKind::Feature,owner.id});document.set_body_history(graph);
    document::DocumentSession session(document);
    const auto revision=session.revision();check(crossings(session.document().sketches.back(),source.id,circle)==0&&session.revision()==revision,"Inspection created a document revision");
    auto next=document;check(next.sketches.back().set_dimension_value(dimension.id,13),"Session dimension edit failed");session.commit(next,{});
    check(session.undo()&&crossings(session.document().sketches.back(),source.id,circle)==0,"Undo lost coincident selection behavior");
    check(session.redo()&&session.document().sketches.back().circles.front().radius==13,"Redo lost radius edit");
    const auto directory=std::filesystem::temp_directory_path()/("zima-coincident-circle-"+kernel::make_stable_id());
    std::filesystem::create_directory(directory);const auto file=directory/"circle.prtz";
    session.document().save(file,session.calculated_boundaries());
    const auto reopened=document::PartDocument::load(file);
    check(reopened.sketches.back().serialized()==session.document().sketches.back().serialized(),"Native Part save/reopen changed circular source/geometry identities");
    std::filesystem::remove(file);std::filesystem::remove(directory);
    ++cases;
}
}
int main(int argc,char** argv) {
    try {
        if(argc==3&&std::string_view(argv[1])=="--inspect-sketches") {
            const auto part=document::PartDocument::load(argv[2]);
            nlohmann::json report=nlohmann::json::array();
            for(const auto& source:part.sketches) {
                auto solved=source;
                const auto solve=solved.solve();
                const auto viewer=source.viewer_mesh();
                nlohmann::json sketch{{"name",source.name},{"id",source.id},
                    {"points",source.points.size()},
                    {"segments",source.segments.size()},
                    {"circles",source.circles.size()},
                    {"arcs",source.arcs.size()},
                    {"external_references",source.external_references.size()},
                    {"constraints",source.constraints.size()},
                    {"dimensions",source.dimensions.size()},
                    {"solve_status",static_cast<int>(solve.status)},
                    {"solve_dof",solve.remaining_degrees_of_freedom},
                    {"solve_residual",solve.maximum_residual},
                    {"viewer_points",nlohmann::json::array()}};
                for(const auto& point:viewer.points)
                    sketch["viewer_points"].push_back(point.reference.semantic_key);
                sketch["dimension_edits"]=nlohmann::json::array();
                for(const auto& dimension:source.dimensions) {
                    auto edited=source;
                    const double input=sketcher::dimension_display_value(
                        dimension,source.plane);
                    auto direct=source;
                    auto updated=dimension;
                    sketcher::set_dimension_input_value(
                        updated,input==0?.1:input*1.01,source.plane);
                    std::string direct_error;
                    try {
                        direct.apply_dimension(updated);
                    } catch(const std::exception& error) {
                        direct_error=error.what();
                    }
                    const bool accepted=edited.set_dimension_value(
                        dimension.id,input==0?.1:input*1.01);
                    const auto result=edited.solve();
                    sketch["dimension_edits"].push_back({
                        {"id",dimension.id},
                        {"kind",static_cast<int>(dimension.kind)},
                        {"value",dimension.value},
                        {"driving",dimension.driving},
                        {"locked",dimension.locked},
                        {"suppressed",dimension.suppressed},
                        {"direct_error",direct_error},
                        {"direct_changed",direct.serialized()!=source.serialized()},
                        {"accepted",accepted},
                        {"changed",edited.serialized()!=source.serialized()},
                        {"status",static_cast<int>(result.status)},
                        {"dof",result.remaining_degrees_of_freedom},
                        {"residual",result.maximum_residual}});
                }
                sketch["point_drags"]=nlohmann::json::array();
                for(const auto& point:source.points) {
                    auto dragged=source;
                    const bool accepted=dragged.move_point(
                        point.id,point.x+.5,point.y+.25);
                    const auto* actual=dragged.find_point(point.id);
                    const auto result=dragged.solve();
                    sketch["point_drags"].push_back({
                        {"id",point.id},{"fixed",point.fixed},
                        {"accepted",accepted},
                        {"movement",actual?std::hypot(
                            actual->x-point.x,actual->y-point.y):0.},
                        {"changed",dragged.serialized()!=source.serialized()},
                        {"status",static_cast<int>(result.status)},
                        {"dof",result.remaining_degrees_of_freedom},
                        {"residual",result.maximum_residual}});
                }
                report.push_back(std::move(sketch));
            }
            std::cout<<report.dump(2)<<'\n';
            return 0;
        }
        unsigned cases{};verify_cylinder_refresh();verify(cases);
        if(argc==2) {
            const auto document=document::PartDocument::load(argv[1]);
            for(const auto& sketch:document.sketches) {
                std::size_t rejected{};
                for(const auto& ref:sketch.external_references)for(const auto& circle:sketch.circles)
                    if(sketch.coincident_circular_support(ref.id,circle.id)) {
                        check(crossings(sketch,ref.id,circle.id)==0,"User Sketch still offers false circle points");++rejected;
                    }
                std::cout<<sketch.name<<": circles="<<sketch.circles.size()<<", splines="<<sketch.bsplines.size()<<", coincident pairs="<<rejected<<'\n';
            }
        }
        std::cout<<cases<<" coincident-reference circle manipulation cases passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

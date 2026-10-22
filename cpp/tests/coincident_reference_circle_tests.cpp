#include <zima/document/document_session.hpp>
#include <zima/sketcher/curve_geometry.hpp>
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
void verify(unsigned& cases) {
    for(unsigned samples:{8u,17u,57u,128u})for(bool reverse:{false,true})
    for(bool arc:{false,true})for(bool fixed:{false,true})for(bool diameter:{false,true})for(bool locked:{false,true})
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ}) {
        auto sketch=sketcher::Sketch::create_default();sketch.plane=plane;sketch.refresh_default_frame();
        const double radius=31.331971171734438;
        auto source=reference(173,-81,radius,samples,reverse,arc);
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
        sketch.external_references.front()=source;sketch.circles.front().radius+=.001;
        check(!sketch.coincident_circular_support(source.id,circle)&&crossings(sketch,source.id,circle)==0,"Concentric separated circles produced false crossings");
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
        unsigned cases{};verify(cases);
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

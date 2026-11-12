#include "profile_solid_fixture.hpp"
#include <zima/document/part_document.hpp>
#include <zima/document/profile_status.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/surface_results.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double value,double expected){if(!std::isfinite(value)||std::abs(value-expected)>=1e-5*std::max(1.,std::abs(expected)))throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(value));}
void profile_states(){
    using document::ProfileStatus;
    auto sketch=sketcher::Sketch::create_default();
    check(document::profile_status(sketch)==ProfileStatus::Empty,"Empty profile misclassified");
    static_cast<void>(sketch.add_segment(0,0,10,0));sketch.segments.back().construction=true;
    check(document::profile_status(sketch)==ProfileStatus::Empty,"Construction geometry counted as profile");
    sketch.segments.back().construction=false;
    check(document::profile_status(sketch)==ProfileStatus::Open,"Open line misclassified");
    static_cast<void>(sketch.add_segment(10,0,10,10));
    static_cast<void>(sketch.add_segment(10,10,0,0));
    check(document::profile_status(sketch)==ProfileStatus::Closed,"Closed triangle misclassified");
    sketch.segments.pop_back();
    check(document::profile_status(sketch)==ProfileStatus::Open,"Removing an edge did not open profile");
    static_cast<void>(sketch.add_segment(20,20,30,30));
    check(document::profile_status(sketch)==ProfileStatus::Invalid,"Disconnected chain accepted");
    auto circle=sketcher::Sketch::create_default();static_cast<void>(circle.add_circle(0,0,5));
    check(document::profile_status(circle)==ProfileStatus::Closed,"Circle misclassified");
    check(circle.circles.size()==1&&circle.segments.empty(),"Profile inspection changed native geometry");
}
struct Fixture {
    document::PartDocument part=document::PartDocument::create_default();
    Fixture(sketcher::Sketch sketch,bool revolve=false){
        auto f=revolve?document::PartDocument::create_revolution_container(sketch.id):document::PartDocument::create_extrusion_container(sketch.id);
        sketch.owner_container_id=f.id;
        f.extrusion.result_type=document::ProfileResultType::Surface;f.extrusion.height=3;f.extrusion.length_forward=3;
        f.revolution.result_type=document::ProfileResultType::Surface;
        part.history={f};part.sketches={sketch};
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Surfaces"));graph.insert({document::PartHistoryKind::Feature,f.id});part.set_body_history(graph);
    }
    void append(document::HistoryContainer f){part.history.push_back(f);auto graph=part.body_history;graph.insert({document::PartHistoryKind::Feature,f.id});part.set_body_history(graph);}
    auto calculate(const kernel::OcctKernel& kernel){part.resolve_constructions();return kernel.evaluate_history(part.kernel_operations());}
};
std::set<std::string> faces(const kernel::BodyResult& b){std::set<std::string> ids;for(const auto& r:b.mesh.original_references.triangle_references){check(r.surface_result,"Surface reference lost its type");check(r.semantic_key.starts_with("generated:"),"Surface acquired a closing cap");ids.insert(r.semantic_key);}return ids;}
void extrude(const kernel::OcctKernel& kernel){
    auto sketch=sketcher::Sketch::create_default();const auto line=sketch.add_segment(0,0,10,0);Fixture f(sketch);
    auto b=f.calculate(kernel);near(b.back().volume,0);near(b.back().surface_area,30);
    check(faces(b.back()).contains("generated:"+line),"Surface lost Sketch curve parent");
    check(std::ranges::all_of(b.back().mesh.triangle_references,[](const auto& r){return r.surface_result;}),"Display triangles lost surface marker");
    const auto& points=b.back().mesh.original_references.points;check(points.size()==4,"Open surface must retain both endpoint descendants");
    auto limited=f.part.kernel_operations();
    for(auto& operation:limited)if(auto* request=std::get_if<kernel::ExtrusionRequest>(&operation.primitive)) {
        request->extent=kernel::ExtrusionRequest::Extent::UpToPlane;request->target_face={"surface-limit","plane"};request->target_is_datum=true;
        request->target_plane_origin={0,0,5};request->target_plane_normal={0,0,1};
    }
    const auto clipped=kernel.evaluate_history(limited).back();near(clipped.volume,0);near(clipped.surface_area,50);faces(clipped);
    auto& p=f.part.history[0].extrusion;p.direction=document::ExtrusionDirection::Reverse;p.extent_mode=document::ProfileExtentMode::TwoSides;p.length_reverse=2;
    b=f.calculate(kernel);near(b.back().surface_area,50);check(faces(b.back()).contains("generated:"+line),"Direction changed surface identity");
    f.part.history[0].extrusion.result_type=document::ProfileResultType::Thin;f.part.history[0].extrusion.thin_thickness=1;b=f.calculate(kernel);near(b.back().volume,50);
    check(std::ranges::none_of(b.back().mesh.triangle_references,[](const auto& r){return r.surface_result;}),"Thin retained surface color marker");
    f.part.history[0].extrusion.result_type=document::ProfileResultType::Surface;f.part.history[0].combine_mode=document::CombineMode::Subtract;
    bool rejected=false;try{f.calculate(kernel);}catch(const std::exception&){rejected=true;}check(rejected,"Surface accepted subtraction");
}
void circle(const kernel::OcctKernel& kernel){
    auto sketch=sketcher::Sketch::create_default();const auto id=sketch.add_circle(0,0,5);Fixture f(sketch);
    auto b=f.calculate(kernel);near(b.back().volume,0);near(b.back().surface_area,30*std::numbers::pi);check(faces(b.back()).contains("generated:"+id),"Cylinder surface parent missing");
    const auto dir=std::filesystem::temp_directory_path()/("zima-surfaces-"+f.part.document_id);std::filesystem::create_directory(dir);
    f.part.save(dir/"surface.prtz",b);std::vector<kernel::BodyResult> cache;auto loaded=document::PartDocument::load(dir/"surface.prtz",&cache);
    check(loaded.history[0].extrusion.result_type==document::ProfileResultType::Surface,"Native surface mode lost");near(cache.back().volume,0);check(faces(cache.back())==faces(b.back()),"Native surface identity or type lost");
    kernel::OcctKernel cold;const auto regenerated=cold.evaluate_history_incremental(loaded.kernel_operations(),cache);near(regenerated.back().surface_area,b.back().surface_area);
    auto shown=b.back().mesh;kernel::hide_surface_results(shown);
    check(shown.triangles.empty()&&shown.edges.empty()&&shown.original_references.points.empty(),"Hidden sheet retained geometry/picking points");
    auto drawing_view=drawing::DrawingDocument::create_view(f.part.document_id,dir/"surface.prtz",b.back().mesh,drawing::ViewOrientation::Isometric);
    check(drawing_view.projected_edges.empty()&&drawing_view.projected_triangles.empty(),"Drawing displayed a yellow surface");
    auto with_thread=b.back().mesh;kernel::ViewerEdge thread;thread.reference={"thread-owner","thread:wire:test"};thread.points={{0,0,0},{10,5,3}};with_thread.edges.push_back(thread);
    kernel::hide_surface_results(with_thread);check(with_thread.edges.size()==1,"Surface filter hid a thread");
    drawing_view=drawing::DrawingDocument::create_view(f.part.document_id,dir/"surface.prtz",with_thread,drawing::ViewOrientation::Isometric);
    check(!drawing_view.projected_edges.empty(),"Drawing surface filter hid a thread");
    f.part.history[0].extrusion.result_type=document::ProfileResultType::Solid;b=f.calculate(kernel);near(b.back().volume,75*std::numbers::pi);
    check(std::ranges::none_of(b.back().mesh.triangle_references,[](const auto& r){return r.surface_result;}),"Solid retained surface marker");
    std::filesystem::remove(dir/"surface.prtz");std::filesystem::remove(dir);
}
void planar(const kernel::OcctKernel& kernel) {
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(-10,-5,10,5));
    const auto hole=sketch.add_circle(0,0,2);
    static_cast<void>(sketch.add_rectangle(20,-2,25,2));
    sketch.plane_offset=7;
    auto feature=document::PartDocument::create_feature_container(sketch.id);
    feature.name="Planar surface";
    feature.feature.automatic_name=feature.name;
    feature.feature.result_type=document::ProfileResultType::Surface;
    for(auto& side:feature.feature.sides)
        side.operation=document::FeatureSideOperation::None;
    feature.feature.profile_plane_offset=sketch.plane_offset;
    sketch.owner_container_id=feature.id;
    document::PartDocument part=document::PartDocument::create_default();
    part.history={feature};part.sketches={sketch};
    document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Planar surfaces"));
    graph.insert({document::PartHistoryKind::Feature,feature.id});
    part.set_body_history(graph);part.resolve_constructions();
    const auto operations=part.kernel_operations();
    check(operations.size()==1,"Planar surface did not create one history operation");
    const auto* group=std::get_if<kernel::FeatureGroupRequest>(
        &operations.front().primitive);
    check(group&&group->children.size()==1,
        "Planar surface did not create one grouped profile request");
    const auto* request=std::get_if<kernel::ExtrusionRequest>(
        &group->children.front());
    check(request&&request->planar_profile&&request->surface_result&&
        request->additional_profile_regions.size()==1&&
        request->inner_profiles.size()==1,
        "Planar surface lost its separate region or inner hole");
    auto result=kernel.evaluate_history(operations).back();
    near(result.volume,0);
    near(result.surface_area,220-4*std::numbers::pi);
    std::set<std::string> face_ids;
    for(const auto& reference:result.mesh.original_references.triangle_references) {
        check(reference.surface_result,
            "Planar face lost its surface-result marker");
        if(reference.owner_id==feature.id&&
                reference.semantic_key.starts_with("surface:from:"))
            face_ids.insert(reference.semantic_key);
    }
    check(face_ids.size()==2,
        "Separate closed profiles did not create two planar faces");
    const auto axis=std::ranges::find_if(
        result.mesh.original_references.axes,[&](const auto& value) {
            return value.reference.owner_id==feature.id&&
                value.reference.semantic_key=="axis:circle:"+hole;
        });
    check(axis!=result.mesh.original_references.axes.end(),
        "Planar circle did not publish its normal axis");
    near(axis->point.x,0);near(axis->point.y,0);near(axis->point.z,7);
    near(axis->direction.x,0);near(axis->direction.y,0);
    near(std::abs(axis->direction.z),1);near(axis->display_length,4);
    const auto directory=std::filesystem::temp_directory_path()/
        ("zima-planar-surface-"+part.document_id);
    std::filesystem::create_directory(directory);
    const auto path=directory/"planar-surface.prtz";
    part.save(path,{result});
    std::vector<kernel::BodyResult> cache;
    const auto reopened=document::PartDocument::load(path,&cache);
    const auto* persisted=reopened.find_container(feature.id);
    check(persisted&&persisted->feature.planar_surface()&&
        persisted->feature.profile_plane_offset==7,
        "Native reopen lost the planar-surface definition or offset");
    near(cache.back().surface_area,result.surface_area);
    const auto rebuilt=kernel.evaluate_history(reopened.kernel_operations()).back();
    near(rebuilt.surface_area,result.surface_area);
    check(std::ranges::any_of(rebuilt.mesh.original_references.axes,
        [&](const auto& value) {
            return value.reference.semantic_key=="axis:circle:"+hole;
        }),"Cold regeneration lost the circle axis identity");
    std::filesystem::remove(path);std::filesystem::remove(directory);

    auto open=sketcher::Sketch::create_default();
    static_cast<void>(open.add_segment(0,0,10,0));
    auto invalid=document::PartDocument::create_feature_container(open.id);
    invalid.feature.result_type=document::ProfileResultType::Surface;
    for(auto& side:invalid.feature.sides)
        side.operation=document::FeatureSideOperation::None;
    open.owner_container_id=invalid.id;
    document::PartDocument rejected=document::PartDocument::create_default();
    rejected.history={invalid};rejected.sketches={open};
    document::BodyHistoryGraph rejected_graph;
    static_cast<void>(rejected_graph.create_body("Invalid surface"));
    rejected_graph.insert({document::PartHistoryKind::Feature,invalid.id});
    rejected.set_body_history(rejected_graph);rejected.resolve_constructions();
    const auto invalid_operations=rejected.kernel_operations(false,true);
    check(invalid_operations.size()==1&&
        !invalid_operations.front().input_error.empty(),
        "Open planar profile was accepted as a valid surface");
}
void revolve(const kernel::OcctKernel& kernel){
    auto sketch=sketcher::Sketch::create_default();const auto line=sketch.add_segment(5,0,5,10);const auto axis=sketch.add_segment(0,0,0,10);sketch.segments.back().construction=true;sketch.segments.back().centerline=true;
    Fixture f(sketch,true);auto b=f.calculate(kernel);near(b.back().volume,0);near(b.back().surface_area,100*std::numbers::pi);check(faces(b.back()).contains("generated:"+line),"Revolve surface parent missing");
    f.part.history[0].revolution.angle_degrees=90;b=f.calculate(kernel);near(b.back().volume,0);near(b.back().surface_area,25*std::numbers::pi);check(std::ranges::count_if(b.back().mesh.original_references.points,[](const auto& p){return p.surface_result;})==4,"Partial revolution endpoint identities missing");
    auto torus=sketcher::Sketch::create_default();static_cast<void>(torus.add_circle(5,0,1));const auto ax=torus.add_segment(0,0,0,10);torus.segments.back().construction=true;torus.segments.back().centerline=true;Fixture t(torus,true);b=t.calculate(kernel);near(b.back().volume,0);near(b.back().surface_area,20*std::numbers::pi*std::numbers::pi);
}
void mixed(const kernel::OcctKernel& kernel){
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_segment(0,0,10,0));Fixture f(sketch);
    auto box=zima::test::rectangular_feature(f.part,{2,2,2});f.append(box);
    auto b=f.calculate(kernel);near(b.back().volume,8);
    auto cut=zima::test::rectangular_feature(f.part,{1,2,4});cut.combine_mode=document::CombineMode::Subtract;f.append(cut);
    b=f.calculate(kernel);near(b.back().volume,4);near(b.back().surface_area,53);
    auto add=zima::test::rectangular_feature(f.part,{2,2,2});f.append(add);b=f.calculate(kernel);near(b.back().volume,8);
    check(std::ranges::any_of(b.back().mesh.triangle_references,[](const auto& r){return r.surface_result;}),"Solid operation discarded sheets");
}
}
int main(){try{profile_states();kernel::OcctKernel kernel;extrude(kernel);circle(kernel);planar(kernel);revolve(kernel);mixed(kernel);std::cout<<"Profile classification, planar surfaces, surface ancestry, mixed solids, conversion and native persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

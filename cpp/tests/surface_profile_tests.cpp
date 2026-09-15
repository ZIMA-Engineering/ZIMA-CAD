#include <zima/document/part_document.hpp>
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
void revolve(const kernel::OcctKernel& kernel){
    auto sketch=sketcher::Sketch::create_default();const auto line=sketch.add_segment(5,0,5,10);const auto axis=sketch.add_segment(0,0,0,10);sketch.segments.back().construction=true;sketch.segments.back().centerline=true;
    Fixture f(sketch,true);auto b=f.calculate(kernel);near(b.back().volume,0);near(b.back().surface_area,100*std::numbers::pi);check(faces(b.back()).contains("generated:"+line),"Revolve surface parent missing");
    f.part.history[0].revolution.angle_degrees=90;b=f.calculate(kernel);near(b.back().volume,0);near(b.back().surface_area,25*std::numbers::pi);check(b.back().mesh.original_references.points.size()==4,"Partial revolution endpoint identities missing");
    auto torus=sketcher::Sketch::create_default();static_cast<void>(torus.add_circle(5,0,1));const auto ax=torus.add_segment(0,0,0,10);torus.segments.back().construction=true;torus.segments.back().centerline=true;Fixture t(torus,true);b=t.calculate(kernel);near(b.back().volume,0);near(b.back().surface_area,20*std::numbers::pi*std::numbers::pi);
}
void mixed(const kernel::OcctKernel& kernel){
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_segment(0,0,10,0));Fixture f(sketch);
    auto box=document::PartDocument::create_box_container();box.box={2,2,2};f.append(box);
    auto b=f.calculate(kernel);near(b.back().volume,8);
    auto cut=document::PartDocument::create_box_container();cut.box={1,2,4};cut.combine_mode=document::CombineMode::Subtract;f.append(cut);
    b=f.calculate(kernel);near(b.back().volume,4);near(b.back().surface_area,53);
    auto add=document::PartDocument::create_box_container();add.box={2,2,2};f.append(add);b=f.calculate(kernel);near(b.back().volume,8);
    check(std::ranges::any_of(b.back().mesh.triangle_references,[](const auto& r){return r.surface_result;}),"Solid operation discarded sheets");
}
}
int main(){try{kernel::OcctKernel kernel;extrude(kernel);circle(kernel);revolve(kernel);mixed(kernel);std::cout<<"Surface profiles, ancestry, mixed solids, conversion and native persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

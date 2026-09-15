#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/holes.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double a,double b){if(!std::isfinite(a)||std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
Json run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message);
    return result.data;
}
double dot(kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
void verify(kernel::OcctKernel& kernel,fs::path directory,const std::string& kind) {
    auto doc=document::PartDocument::create_default();const auto id=doc.document_id;
    if(kind=="holes") {auto box=document::PartDocument::create_box_container();box.box={40,40,40};doc.history.push_back(box);}
    auto container=document::PartDocument::create_sketch_container();auto sketch=sketcher::Sketch::create_default();
    sketch.owner_container_id=container.id;
    std::string axis;
    if(kind=="holes")static_cast<void>(sketch.add_segment(-20,0,20,0));
    else if(kind=="revolution") {
        static_cast<void>(sketch.add_rectangle(2,-3,4,3));
        axis=sketch.add_segment(0,-5,0,5);sketch.segments.back().construction=true;sketch.segments.back().centerline=true;
    } else static_cast<void>(sketch.add_circle(0,0,2));
    doc.history.push_back(container);doc.sketches.push_back(sketch);
    auto calculated=kernel.evaluate_history(doc.kernel_operations());
    workspace::Workspace live;const auto file=directory/(kind+".prtz");live.add_part(doc,std::move(calculated),file);live.activate(id);
    command_host::Host host(live,kernel,directory);
    const auto current=[&]{return workspace::document_sketch(live,id,sketch.id);};
    const auto reference=[&](const char* plane) {run(host,"sketch.reference.set",{{"sketch",sketch.id},{"index",0},{"reference",{{"owner",id+":origin"},{"key",plane}}}});};
    reference("origin:plane:xy");
    check(current().plane_auto,"New Sketch should follow first plane");near(std::abs(current().resolved_normal.z),1);
    if(kind=="holes")run(host,"holes.create",{{"sketch",sketch.id},{"diameter_mm",4}});
    if(kind=="extrusion")run(host,"extrusion.create",{{"sketch",sketch.id},{"length_forward_mm",5}});
    if(kind=="revolution")run(host,"revolution.create",{{"sketch",sketch.id},{"angle_degrees",360},{"axis",axis}});
    const bool profile=kind=="extrusion"||kind=="revolution";
    const auto set=[&](const char* plane) {
        if(profile)run(host,kind+".set",{{"container",container.id},{"profile_plane",plane},{"profile_offset_mm",3}});
        else run(host,"sketch.set",{{"sketch",sketch.id},{"plane",plane},{"plane_offset_mm",3}});
    };
    const auto automatic=current();set("XY");const auto manual=current();
    check(!manual.plane_auto&&manual.plane==sketcher::SketchPlane::XY,"Manual plane lost");
    near(dot(manual.resolved_normal,automatic.resolved_normal),0);
    near(dot(manual.resolved_origin,manual.resolved_normal),3);
    run(host,"undo");check(current().plane_auto,"Undo did not restore automatic plane");run(host,"redo");check(!current().plane_auto,"Redo did not restore manual plane");
    reference("origin:plane:yz");check(!current().plane_auto&&current().plane==sketcher::SketchPlane::XY,"Replacing reference overwrote manual plane");
    const auto saved=current();run(host,"regenerate");near(dot(current().resolved_normal,saved.resolved_normal),1);
    run(host,"save");std::vector<kernel::BodyResult> cache;auto loaded=document::PartDocument::load(file,&cache);
    const auto found=std::ranges::find(loaded.sketches,sketch.id,&sketcher::Sketch::id);
    check(found!=loaded.sketches.end()&&!found->plane_auto&&found->plane==sketcher::SketchPlane::XY,"Native data lost manual plane");
    loaded.resolve_constructions();near(dot(std::ranges::find(loaded.sketches,sketch.id,&sketcher::Sketch::id)->resolved_normal,saved.resolved_normal),1);
    if(kind!="sketch") {
        const double expected=kind=="holes"?64000-std::numbers::pi*4*40:kind=="extrusion"?std::numbers::pi*4*5:std::numbers::pi*12*6;
        near(cache.back().volume,expected);near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,expected);
    }
    set("AUTO");check(current().plane_auto,"AUTO was not restored");near(std::abs(current().resolved_normal.x),1);
}
}
int main(){try {
    const auto directory=fs::temp_directory_path()/("zima-work-planes-"+document::PartDocument::create_default().document_id);fs::create_directories(directory);
    kernel::OcctKernel kernel;for(const auto& kind:{"sketch","holes","extrusion","revolution"})verify(kernel,directory,kind);
    fs::remove_all(directory);std::cout<<"Work-plane defaults, overrides, references, geometry and native history passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

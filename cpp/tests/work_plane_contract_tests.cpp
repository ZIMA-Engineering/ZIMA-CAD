#include "profile_solid_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/holes.hpp>
#include <zima/document/sketch_placement.hpp>
#include <zima/document/body_origin_attachment.hpp>
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
void origin_frames() {
    const auto canonical=document::PartDocument::create_default();
    const auto geometry=canonical.origin_viewer_mesh().original_references;
    auto refs=document::body_origin_attachment(canonical.document_id).references;
    check(document::placement_references_use_whole_origin(refs),"Complete Origin not recognized");
    check(!document::sketch_placement_uses_front_plane(refs),"Complete Origin forced XZ");
    for(unsigned slot=0;slot<3;++slot) {
        auto partial=refs;partial.erase(partial.begin()+slot);
        check(!document::placement_references_use_whole_origin(partial),"Partial Origin mistaken for complete frame");
        auto other=refs;other[slot].instance_path="another-occurrence";
        check(!document::placement_references_use_whole_origin(other),"Distinct Origin occurrences merged");
        other=refs;other[slot].owner_id="another-origin";
        check(!document::placement_references_use_whole_origin(other),"Distinct Origins merged");
    }
    for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(const bool automatic:{false,true})for(const bool flipped:{false,true}) {
        auto doc=canonical;auto feature=document::PartDocument::create_sketch_container();
        feature.placement.references=refs;
        for(auto& ref:feature.placement.references)if(ref.semantic_key=="origin:plane:xz")ref.flip=flipped;
        feature.placement.rotation_offset_z=17.;
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
        sketch.plane=plane;sketch.plane_auto=automatic;sketch.plane_offset=3.;
        static_cast<void>(sketch.add_rectangle(0,0,10,20));
        doc.history={feature};doc.sketches={sketch};doc.resolve_constructions(geometry);
        const auto& resolved=doc.sketches.front();
        check(resolved.plane==plane,"Whole Origin rewrote a persisted work plane");
        const auto axes=document::sketch_container_frame_axes(resolved);
        const auto& p=doc.history.front().placement;
        const kernel::Vec3 rotation{p.rotation_x,p.rotation_y,p.rotation_z};
        for(unsigned i=0;i<3;++i) {
            const auto axis=document::construction_direction_from_local_axis(std::array<std::string,3>{"x","y","z"}[i],rotation);
            near(dot(axes[i],axis),1.);
        }
        const auto roundtrip=document::PartDocument::from_serialized(doc.serialized());
        check(roundtrip.history.front().placement==p&&roundtrip.sketches.front().plane==plane,
            "Whole Origin lost manual rotation, sides or plane in persistence");
        near(dot(resolved.resolved_normal,resolved.resolved_x_axis),0.);
        near(dot(resolved.resolved_normal,resolved.resolved_y_axis),0.);
        near(dot(resolved.resolved_origin,resolved.resolved_normal),3.);
    }
    std::cout<<"Whole Origin: 3 local planes x auto/manual x both sides; exact frame, correction, offset and persistence PASS\n";
    for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(int turns=0;turns<4;++turns) {
        auto doc=canonical;auto feature=document::PartDocument::create_sketch_container();
        feature.placement.references=refs;feature.placement.orientation_quarter_turns=turns;
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
        sketch.plane=plane;sketch.plane_auto=true;doc.history={feature};doc.sketches={sketch};
        doc.resolve_constructions(geometry);
        const auto axes=document::sketch_container_frame_axes(doc.sketches.front());
        const kernel::Vec3 rotation=plane==sketcher::SketchPlane::XZ?kernel::Vec3{0.,90.*turns,0.}:
            kernel::Vec3{0.,0.,90.*turns};
        for(unsigned i=0;i<3;++i)near(dot(axes[i],document::construction_direction_from_local_axis(
            std::array<std::string,3>{"x","y","z"}[i],rotation)),1.);
    }
    std::cout<<"Whole Origin: all quarter turns preserve authored XZ profiles and new XY frames PASS\n";
}
void verify(kernel::OcctKernel& kernel,fs::path directory,const std::string& kind) {
    auto doc=document::PartDocument::create_default();const auto id=doc.document_id;
    if(kind=="holes") {auto box=zima::test::rectangular_feature(doc,{40,40,40});doc.history.push_back(box);}
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
    origin_frames();
    const auto directory=fs::temp_directory_path()/("zima-work-planes-"+document::PartDocument::create_default().document_id);fs::create_directories(directory);
    kernel::OcctKernel kernel;for(const auto& kind:{"sketch","holes","extrusion","revolution"})verify(kernel,directory,kind);
    fs::remove_all(directory);std::cout<<"Work-plane defaults, overrides, references, geometry and native history passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

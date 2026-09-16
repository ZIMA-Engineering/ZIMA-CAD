#include <zima/command_host/host.hpp>
#include <zima/document/flat.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
using commands::Json;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",args}});
    if(!result.ok)throw std::runtime_error(std::string(command)+" "+args.dump()+": "+result.message);return result;
}
std::set<std::string> faces(const kernel::BodyResult& body,const std::string& owner) {
    std::set<std::string> result;for(const auto& f:body.mesh.original_references.triangle_references)
        if(f.owner_id==owner)result.insert(f.semantic_key);return result;
}
std::pair<double,double> span(const std::vector<kernel::Vec3>& vertices,const sketcher::Sketch& sketch) {
    double lo=INFINITY,hi=-INFINITY;for(const auto p:vertices) {
        const auto n=sketch.resolved_normal,o=sketch.resolved_origin;
        const double distance=(p.x-o.x)*n.x+(p.y-o.y)*n.y+(p.z-o.z)*n.z;
        lo=std::min(lo,distance);hi=std::max(hi,distance);
    }return {lo,hi};
}
void verify(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","flat-test"}});
    const auto id=live.active_document_id();
    const auto created=run(host,"flat.create",{{"width_mm",40.},{"height_mm",30.}}).data;
    const auto owner=created.at("container").get<std::string>(),sketch_id=created.at("sketch").get<std::string>();
    auto* state=live.open_part(id);
    const auto volume=[&]{return state->session.calculated_boundaries().back().volume;};
    const auto sketch=[&]{return workspace::document_sketch(live,id,sketch_id);};
    near(volume(),1200);check(!created.at("thickness_override").get<bool>(),"Flat did not inherit Part thickness");
    const auto identities=faces(state->session.calculated_boundaries().back(),owner);
    check(identities.size()==6,"Flat must expose six rectangle faces");
    for(const auto& segment:sketch().segments)
        check(identities.contains("generated:"+segment.id),"Flat side lost its source curve identity");
    check(std::ranges::count_if(identities,[](const auto& key){return key.starts_with("start:from:")||key.starts_with("end:from:");})==2,
        "Flat Start/End faces lost their profile region ancestry");
    for(const auto* direction:{"forward","reverse","symmetric"}) {
        run(host,"flat.set",{{"container",owner},{"direction",direction},{"thickness_mm",2.}});
        near(volume(),2400);
        const double start=std::string(direction)=="forward"?0:std::string(direction)=="reverse"?-2:-1;
        const auto bounds=span(state->session.calculated_boundaries().back().mesh.vertices,sketch());near(bounds.first,start);near(bounds.second,start+2);
        check(faces(state->session.calculated_boundaries().back(),owner)==identities,"Changing Flat side replaced topology identities");
        const auto preview=document::flat_preview(*state->session.document().find_container(owner),sketch(),document::sheet_metal_defaults(state->session.document()));
        std::vector<kernel::Vec3> points;for(const auto& e:preview.edges)points.insert(points.end(),e.points.begin(),e.points.end());
        const auto wire=span(points,sketch());near(wire.first,start);near(wire.second,start+2);
    }
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",3.}}}});run(host,"regenerate");near(volume(),2400);
    run(host,"flat.set",{{"container",owner},{"thickness_override",false}});near(volume(),3600);
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",4.}}}});run(host,"regenerate");near(volume(),4800);
    // A circle in the closed profile makes a through hole; it is not filled.
    check(workspace::mutate_document_sketch(live,id,sketch_id,[](auto& s){
        static_cast<void>(s.add_circle(20,15,3));static_cast<void>(s.add_ellipse(8,15,12,15,8,17));
    }),"Profile edit was not committed");
    run(host,"regenerate");
    const double area=1200-17*std::numbers::pi;near(volume(),area*4);
    const auto verify_axes=[&](const kernel::BodyResult& body) {
        std::set<std::string> keys;
        for(const auto& axis:body.mesh.original_references.axes)if(axis.reference.owner_id==owner) {
            keys.insert(axis.reference.semantic_key);
            check(std::ranges::any_of(body.mesh.axes,[&](const auto& visible){return visible.reference==axis.reference;}),"Flat axis is not visible in the View");
            const auto normal=sketch().normal();near(std::abs(axis.direction.x*normal.x+axis.direction.y*normal.y+axis.direction.z*normal.z),1);
        }
        check(keys.size()==2,"Circle and ellipse must produce two Flat axes");return keys;
    };
    const auto axis_keys=verify_axes(state->session.calculated_boundaries().back());
    for(const auto* side:{"forward","reverse","symmetric"}) {
        run(host,"flat.set",{{"container",owner},{"direction",side}});
        check(verify_axes(state->session.calculated_boundaries().back())==axis_keys,"Flat side change replaced hole axes");
    }
    std::string radius_dimension;
    check(workspace::mutate_document_sketch(live,id,sketch_id,[&](auto& s){
        auto dimension=s.create_circle_radius_dimension(s.circles.front().id);radius_dimension=dimension.id;s.apply_dimension(dimension);
    }),"Cannot dimension Flat profile opening");
    for(double radius:{5.,3.}) {
        check(workspace::mutate_document_sketch(live,id,sketch_id,[&](auto& s){
            check(s.set_dimension_value(radius_dimension,radius),"Cannot change Flat Sketch radius");
        }),"Flat profile dimension edit was ignored");
        run(host,"regenerate");near(volume(),(1200-(radius*radius+8)*std::numbers::pi)*4);
    }
    auto pending=*state->session.document().find_container(owner);auto profile=sketch();
    profile.plane=sketcher::SketchPlane::YZ;profile.plane_auto=false;
    check(workspace::commit_flat(live,kernel,id,pending,profile),"Changing Flat plane was ignored");near(volume(),area*4);
    const auto yz=span(state->session.calculated_boundaries().back().mesh.vertices,sketch());near(yz.first,-2);near(yz.second,2);
    run(host,"save");std::vector<kernel::BodyResult> saved;
    const auto reopened=document::PartDocument::load(directory/"flat-test.prtz",&saved);
    check(reopened.find_container(owner)->flat==state->session.document().find_container(owner)->flat,"Flat parameters changed after reopen");
    near(saved.back().volume,area*4);
    check(verify_axes(saved.back())==axis_keys,"Native reopen changed Flat hole axes");
    run(host,"flat.set",{{"container",owner},{"thickness_mm",5.}});near(volume(),area*5);
    check(verify_axes(state->session.calculated_boundaries().back())==axis_keys,"Flat thickness change replaced hole axes");
    run(host,"undo");near(volume(),area*4);run(host,"redo");near(volume(),area*5);
    for(const auto& bad:{Json{{"thickness_mm",0.}},Json{{"thickness_mm",-1.}},Json{{"direction","two_sides"}}}) {
        auto args=bad;args["container"]=owner;const auto revision=state->session.revision();
        check(!host.execute({{"command","flat.set"},{"arguments",args}}).ok,"Invalid Flat parameters were accepted");
        check(state->session.revision()==revision,"Rejected edit changed history");near(volume(),area*5);
    }
    pending=*state->session.document().find_container(owner);profile=sketch();profile.segments.pop_back();
    bool rejected=false;try{static_cast<void>(workspace::commit_flat(live,kernel,id,pending,profile));}catch(const std::exception&){rejected=true;}
    check(rejected,"Open sheet profile was accepted");near(volume(),area*5);
    pending=*state->session.document().find_container(owner);pending.combine_mode=document::CombineMode::Subtract;
    rejected=false;try{static_cast<void>(workspace::commit_flat(live,kernel,id,pending,sketch()));}catch(const std::exception&){rejected=true;}
    check(rejected,"Flat accepted subtraction");
    const auto references=workspace::family_references(live,id);
    check(std::ranges::any_of(references,[&](const auto& ref){return ref.binding.owner_id==owner&&ref.binding.semantic_key=="parameter:thickness";}),"Local Flat thickness is absent from Family Table");
    auto table=workspace::family_table(live,id);table.columns.push_back("Sheet thickness");
    table.bindings["Sheet thickness"]={"dimension",owner,"parameter:thickness"};
    table.instances.push_back({"Thin sheet",{{"Sheet thickness","1.5"}}});
    static_cast<void>(workspace::set_family_table(live,id,table));
    const auto variant=workspace::open_family_instance(live,kernel,id,"Thin sheet",false);
    near(live.open_part(variant)->session.calculated_boundaries().back().volume,area*1.5);
    run(host,"new",{{"type","assembly"},{"name","no-flat"}});
    check(!host.execute({{"command","flat.create"},{"arguments",Json::object()}}).ok,"Assembly accepted Flat");
}
void verify_ellipse_regions() {
    kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();
    auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Flat;
    feature.flat.thickness_override=true;feature.flat.thickness=2;
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;feature.flat.sketch_id=sketch.id;
    static_cast<void>(sketch.add_ellipse(0,0,10,0,0,5));
    static_cast<void>(sketch.add_ellipse(0,0,2,0,0,1));
    static_cast<void>(sketch.add_ellipse(30,0,34,0,30,2));
    part.history={feature};part.sketches={sketch};part.resolve_constructions();
    for(auto direction:{document::ExtrusionDirection::Forward,document::ExtrusionDirection::Reverse,document::ExtrusionDirection::Symmetric}) {
        part.history.front().flat.direction=direction;
        const auto result=kernel.evaluate_history(part.kernel_operations());near(result.back().volume,112*std::numbers::pi);
    }
    // The same exact profile classifier serves ordinary Extrusion.
    auto extrusion=document::PartDocument::create_extrusion_container(sketch.id);
    part.sketches.front().owner_container_id=extrusion.id;extrusion.extrusion.height=extrusion.extrusion.length_forward=2;
    part.history={extrusion};part.resolve_constructions();near(kernel.evaluate_history(part.kernel_operations()).back().volume,112*std::numbers::pi);
    part.sketches.front().ellipses.clear();part.sketches.front().circles.clear();
    static_cast<void>(part.sketches.front().add_rectangle(-10,-10,10,10));
    static_cast<void>(part.sketches.front().add_ellipse(9,0,13,0,9,2));
    bool rejected=false;try{static_cast<void>(part.kernel_operations());}catch(const std::exception&){rejected=true;}
    check(rejected,"Intersecting ellipse and rectangle were accepted as closed regions");
    part.sketches.front().ellipses.clear();static_cast<void>(part.sketches.front().add_circle(0,0,3));
    for(auto direction:{document::ExtrusionDirection::Forward,document::ExtrusionDirection::Reverse,document::ExtrusionDirection::Symmetric}) {
        part.history.front().extrusion.direction=direction;
        near(kernel.evaluate_history(part.kernel_operations()).back().volume,2*(400-9*std::numbers::pi));
    }
    auto text=sketcher::Sketch::create_text();text.value="I";
    text.contours={{{20,0},{25,0},{25,10},{20,10}}};part.sketches.front().add_text(std::move(text));
    for(auto direction:{document::ExtrusionDirection::Forward,document::ExtrusionDirection::Reverse,document::ExtrusionDirection::Symmetric}) {
        part.history.front().extrusion.direction=direction;
        near(kernel.evaluate_history(part.kernel_operations()).back().volume,2*(450-9*std::numbers::pi));
    }
}
}
int main() {
    const auto directory=std::filesystem::temp_directory_path()/("zima-flat-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try{verify(directory);verify_ellipse_regions();std::filesystem::remove_all(directory);std::cout<<"Flat geometry, directions, defaults, topology and history passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"; fixture: "<<directory<<'\n';return 1;}
}

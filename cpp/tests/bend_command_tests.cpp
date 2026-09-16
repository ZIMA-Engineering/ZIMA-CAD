#include <zima/command_host/host.hpp>
#include <zima/document/bend.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
using commands::Json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",args}});
    if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
    std::cout<<command<<' '<<args.dump()<<'\n';return result;
}
std::set<std::string> faces(const kernel::BodyResult& body,const std::string& owner) {
    std::set<std::string> result;
    for(const auto& face:body.mesh.original_references.triangle_references)if(face.owner_id==owner)result.insert(face.semantic_key);
    return result;
}
void verify(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","bend-test"}});
    const auto id=live.active_document_id();
    const auto created=run(host,"bend.create",{{"width_mm",40.},{"radius_mm",5.},{"angle_degrees",90.}}).data;
    const auto owner=created.at("container").get<std::string>();auto* state=live.open_part(id);
    const auto volume=[&]{return state->session.calculated_boundaries().back().volume;};
    const double pi=std::numbers::pi;
    const double initial_k=created.at("k_factor");
    near(volume(),40*pi/2*(36-25)/2); // Annular sector: width * angle * (Ro²-Ri²)/2.
    check(created.at("thickness_mm")==1.&&!created.at("thickness_override").get<bool>(),"Bend did not inherit default 1 mm");
    const auto bent_body=state->session.calculated_boundaries().back();
    const auto bent_faces=faces(bent_body,owner);
    check(bent_faces.size()==6,"Bend must have six authored faces");
    check(std::ranges::count_if(bent_faces,[](const auto& s){return s.starts_with("start:from:")||s.starts_with("end:from:");})==2,"Start/End cap ancestry missing");
    run(host,"bend.set",{{"container",owner},{"state","unbend"}});near(volume(),40*pi/2*(5+initial_k));
    check(faces(state->session.calculated_boundaries().back(),owner)==bent_faces,"Bend/Unbend changed face identities");
    for(const auto& key:bent_faces)if(key.starts_with("start:from:")||key.starts_with("end:from:")) {
        auto plane=document::PartDocument::create_construction(document::ConstructionKind::Plane);
        plane.definition=document::ConstructionDefinition::PlaneReference;plane.references={{{},owner,key}};
        check(document::resolve_construction(plane,bent_body.mesh.original_references),"Cannot attach a plane to bent cap");
        const auto direction=plane.direction;
        check(document::resolve_construction(plane,state->session.calculated_boundaries().back().mesh.original_references),"Cap reference did not survive Unbend");
        const auto dot=direction.x*plane.direction.x+direction.y*plane.direction.y+direction.z*plane.direction.z;
        near(std::abs(dot),key.starts_with("start:")?1.:0.);
    }
    const auto& saved=state->session.document();const auto* feature=saved.find_container(owner);
    const auto sketch=*std::ranges::find(saved.sketches,feature->bend.sketch_id,&sketcher::Sketch::id);
    const auto preview=document::bend_preview(*feature,sketch,document::sheet_metal_defaults(saved));
    check(preview.edges.size()==12&&preview.axes.size()==1,"Unbend preview or bend axis missing");
    run(host,"undo");near(volume(),40*pi/2*(36-25)/2);run(host,"redo");near(volume(),40*pi/2*(5+initial_k));
    const auto revision=state->session.revision();
    check(!run(host,"bend.set",{{"container",owner},{"state","unbend"}}).data.at("changed").get<bool>(),"No-op Bend edit changed history");
    for(auto bad: {Json{{"radius_mm",0}},Json{{"angle_degrees",181}},Json{{"angle_degrees",-1}},Json{{"thickness_mm",0}},Json{{"k_factor",1.1}},Json{{"state","unknown"}}}) {
        bad["container"]=owner;check(!host.execute({{"command","bend.set"},{"arguments",bad}}).ok,"Invalid Bend parameters accepted");
    }
    check(state->session.revision()==revision,"Rejected Bend edit changed history");
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",2.},{"k_factor",.4}}}});
    near(volume(),40*pi/2*(5+initial_k)); // Defaults alone preserve calculated geometry until explicit regeneration.
    run(host,"regenerate");near(volume(),40*2*pi/2*5.8);
    run(host,"bend.set",{{"container",owner},{"thickness_mm",1.5},{"k_factor",.3}});near(volume(),40*1.5*pi/2*5.45);
    run(host,"bend.set",{{"container",owner},{"thickness_override",false},{"k_factor_override",false}});near(volume(),40*2*pi/2*5.8);
    run(host,"bend.set",{{"container",owner},{"state","bend"},{"angle_degrees",180.}});near(volume(),40*pi*(49-25)/2);
    check(faces(state->session.calculated_boundaries().back(),owner)==bent_faces,"180-degree Bend changed face identity");
    run(host,"bend.set",{{"container",owner},{"angle_degrees",0.}});near(volume(),0);
    check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Zero angle produced a calculation error");
    run(host,"bend.set",{{"container",owner},{"angle_degrees",90.}});
    run(host,"save");std::vector<kernel::BodyResult> cached;
    const auto loaded=document::PartDocument::load(directory/"bend-test.prtz",&cached);
    check(loaded.find_container(owner)->bend==state->session.document().find_container(owner)->bend,"Native Bend parameters changed on reload");
    check(faces(cached.back(),owner)==bent_faces,"Native cached face identity changed on reload");
    auto bad_sketch=sketch;static_cast<void>(bad_sketch.add_segment(0,20,10,20));const auto before=state->session.revision();
    try{static_cast<void>(workspace::commit_bend(live,kernel,id,*state->session.document().find_container(owner),bad_sketch));throw std::runtime_error("Multiple Bend segments accepted");}
    catch(const std::invalid_argument&){}
    check(state->session.revision()==before,"Invalid owned Sketch changed the Part");
    document::FamilyTable table;table.columns={"Bend angle"};table.bindings["Bend angle"]={"dimension",owner,"parameter:angle"};
    table.instances={{"Half angle",{{"Bend angle","45"}}},{"Zero",{{"Bend angle","0"}}}};
    static_cast<void>(workspace::set_family_table(live,id,table));
    const auto half=workspace::open_family_instance(live,kernel,id,"Half angle",false);
    near(live.open_part(half)->session.calculated_boundaries().back().volume,40*pi/4*(49-25)/2);
    const auto zero=workspace::open_family_instance(live,kernel,id,"Zero",false);
    near(live.open_part(zero)->session.calculated_boundaries().back().volume,0);
    run(host,"new",{{"type","assembly"},{"name","no-bend"}});
    check(!host.execute({{"command","bend.create"},{"arguments",Json::object()}}).ok,"Assembly accepted Bend");
}
}
int main() {
    const auto directory=std::filesystem::temp_directory_path()/("zima-bend-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try{verify(directory);std::filesystem::remove_all(directory);std::cout<<"Bend geometry, identities, defaults, history and persistence passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"; fixture: "<<directory<<'\n';return 1;}
}

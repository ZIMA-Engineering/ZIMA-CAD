#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const std::string& command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(command+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","dimensions"}});
    const auto document=live.active_document_id();std::string sketch;
    const auto create=[&](const std::string& name){sketch=run(host,"sketch.create",{{"name",name}}).data.at("sketch").get<std::string>();};
    const auto command=[&](const char* name,Json args=Json::object()){args["sketch"]=sketch;return run(host,name,std::move(args)).data;};
    const auto current=[&]{return workspace::document_sketch(live,document,sketch);};
    const auto point=[&](double x,double y){return command("sketch.point.create",{{"position",{x,y}}}).at("point").get<std::string>();};
    const auto segment=[&](double x1,double y1,double x2,double y2){return command("sketch.segment.create",{{"first",{x1,y1}},{"second",{x2,y2}}}).at("geometry").get<std::string>();};
    const auto circle=[&](double radius){return command("sketch.circle.create",{{"center",{0,0}},{"radius_mm",radius}}).at("geometry").get<std::string>();};
    create("Radius");const auto c=circle(5);
    const auto radius=command("sketch.dimension.create",{{"kind","radius"},{"geometry",{c}},{"value",7},{"locked",true},{"layout",{{"text_along",3},{"arrows_reversed",true}}}}).at("dimension").get<std::string>();
    auto* state=live.open_part(document);const auto dimension=[&]{return command("sketch.dimension.get",{{"dimension",radius}});};
    require(current().circles.front().radius==7 && dimension().at("locked")==true && dimension().at("document_layout").at("text_along")==3,"Radius value/lock/label did not commit together");
    command("sketch.dimension.set",{{"dimension",radius},{"value",8}});require(current().circles.front().radius==8 && dimension().at("locked")==true,"Explicit locked Properties edit was blocked or unlocked");
    const auto same_revision=state->session.revision();require(command("sketch.dimension.set",{{"dimension",radius},{"value",8}}).at("changed")==false && state->session.revision()==same_revision,"No-op dimension edit committed");
    const auto unchanged=current().serialized();command("sketch.dimension.set",{{"dimension",radius},{"layout",{{"text_along",4}}}});
    require(current().serialized()==unchanged && dimension().at("document_layout").at("text_along")==4,"Label-only edit changed geometry or was ignored");
    run(host,"undo");require(dimension().at("document_layout").at("text_along")==3 && current().circles.front().radius==8,"Label-only Undo failed");run(host,"redo");
    command("sketch.dimension.set",{{"dimension",radius},{"driving",false},{"value",999}});
    require(current().circles.front().radius==8 && dimension().at("value")==8 && dimension().at("locked")==false,"Reference dimension drove geometry or kept its lock");
    require(command("sketch.dimension.set",{{"dimension",radius},{"value",50}}).at("changed")==false,"Read-only measured value became an edit");
    command("sketch.dimension.set",{{"dimension",radius},{"driving",true},{"value",9},{"limits",{{"lower",1},{"upper",12}}},{"position",{12,13}},
        {"text",{{"prefix","TEST "},{"suffix"," mm"},{"tolerance_mode","symmetric"},{"symmetric_tolerance","0.1"}}}});
    require(current().circles.front().radius==9 && dimension().at("position")==Json::array({12,13}) && dimension().at("text").at("prefix")=="TEST ","Dimension properties patch failed");
    const auto revision=state->session.revision();const auto saved=current().serialized();const auto labels=state->session.document().dimension_layouts;
    for(const Json& patch:std::vector<Json>{
        {{"value",13},{"layout",{{"text_along",200}}}},{{"value",-1}},{{"layout",{{"plane_quarter_turns",4}}}},
        {{"layout",{{"arrows_reversed","true"}}}},{{"text",{{"unknown","x"}}}},{{"limits",{{"bad",1}}}},{{"solution_side",0}},{{"angle_sector",5}}}) {
        auto a=patch;a["dimension"]=radius;a["sketch"]=sketch;require(!host.execute({{"command","sketch.dimension.set"},{"arguments",a}}).ok && state->session.revision()==revision && current().serialized()==saved && state->session.document().dimension_layouts==labels,"Rejected dimension edit partially changed value or label");
    }
    command("sketch.dimension.set",{{"dimension",radius},{"limits",{{"lower",nullptr},{"upper",nullptr}}}});require(dimension().at("limits").at("lower").is_null(),"Limits could not be cleared");
    command("sketch.dimension.delete",{{"dimension",radius}});require(current().dimensions.empty() && current().circles.front().radius==9,"Dimension removal deleted its circle");run(host,"undo");require(current().dimensions.size()==1,"Dimension delete Undo failed");
    // Independent known dimensions verify every factory without relying on its
    // own returned measurement to define the expectation.
    for(const auto& [kind,expected]:std::vector<std::pair<std::string,double>>{{"distance",5},{"distance_x",3},{"distance_y",4},
        {"point_line",3},{"symmetric",6},{"line_distance",3},{"radius",5},{"diameter",10},{"angle",45},
        {"three_point_angle",90},{"angle_between",45},{"symmetric_angle",90},{"symmetric_line_distance",6},
        {"ellipse_major",8},{"ellipse_minor",3},{"ellipse_rotation",0}}) {
        create(kind);Json p=Json::array(),g=Json::array();
        if(kind=="radius"||kind=="diameter")g.push_back(circle(5));
        else if(kind.starts_with("ellipse"))g.push_back(command("sketch.ellipse.create",{{"center",{0,0}},{"major",{8,0}},{"minor",{0,3}}}).at("geometry"));
        else if(kind=="three_point_angle"){p={point(1,0),point(0,0),point(0,1)};}
        else if(kind=="point_line"||kind=="symmetric"){p.push_back(point(2,3));g.push_back("sketch_axis:x");}
        else if(kind=="line_distance"||kind=="symmetric_line_distance"){g={"sketch_axis:x",segment(-4,3,4,3)};}
        else if(kind=="angle_between"||kind=="symmetric_angle"){g={"sketch_axis:x",segment(0,0,10,10)};}
        else if(kind=="angle")g.push_back(segment(0,0,10,10));
        else g.push_back(segment(0,0,3,4));
        const auto d=command("sketch.dimension.create",{{"kind",kind},{"points",p},{"geometry",g},{"driving",false}});
        require(d.at("kind")==kind && std::abs(d.at("value").get<double>()-expected)<1e-6,"Dimension factory returned wrong kind or measurement");
        require(d.at("unit")==((kind.find("angle")!=std::string::npos||kind=="ellipse_rotation")?"deg":"mm"),"Dimension unit mismatch");
        if(d.at("unit")=="deg") {
            const auto saved_revision=state->session.revision();const auto geometry=current().serialized();
            require(!host.execute({{"command","sketch.dimension.set"},{"arguments",{{"sketch",sketch},{"dimension",d.at("dimension")},{"layout",{{"plane_quarter_turns",1}}}}}}).ok &&
                state->session.revision()==saved_revision && current().serialized()==geometry,"Angular label plane accepted invalid rotation");
        }
    }
    for(const auto* kind:{"distance_x","distance_y"}) {
        create(kind);const auto p=point(3,4);const auto axis=std::string(kind)=="distance_x"?"sketch_axis:y":"sketch_axis:x";
        const auto value=command("sketch.dimension.create",{{"kind",kind},{"points",{p}},{"geometry",{axis}},{"value",8}});
        require(value.at("geometry")==axis && (std::string(kind)=="distance_x"?current().find_point(p)->x:current().find_point(p)->y)==8,"Coordinate-axis dimension lost its original axis");
    }
    create("four point angle");const auto a=point(0,0),b=point(10,0),e=point(0,10),f=point(10,20);
    require(std::abs(command("sketch.dimension.create",{{"kind","angle_between"},{"points",{a,b,e,f}},{"driving",false}}).at("value").get<double>()-45)<1e-7,"Four point angle factory failed");
    create("point line angle");const auto p1=point(0,0),p2=point(10,10);
    require(std::abs(command("sketch.dimension.create",{{"kind","angle_between"},{"points",{p1,p2}},{"geometry",{"sketch_axis:x"}},{"driving",false}}).at("value").get<double>()-45)<1e-7,"Point-line angle factory failed");
    run(host,"save");const auto loaded=document::PartDocument::load(directory/"dimensions.prtz");
    require(loaded.dimension_layouts==state->session.document().dimension_layouts,"Native save lost dimension label layouts");
    for(std::size_t i=0;i<loaded.sketches.size();++i)require(loaded.sketches[i].serialized()==state->session.document().sketches[i].serialized(),"Native save changed dimensions");
    // Geometry changes remain pending for an explicitly requested solid calculation.
    run(host,"new",{{"type","part"},{"name","dimension-body"}});auto* body=live.open_part(live.active_document_id());auto next=body->session.document();
    auto profile=sketcher::Sketch::create_default();const auto rectangle=profile.add_rectangle(0,0,10,5);auto extrusion=document::PartDocument::create_extrusion_container(profile.id);
    profile.owner_container_id=extrusion.id;extrusion.extrusion.height=2;const auto profile_id=profile.id;
    next.sketches.push_back(profile);next.insert_history_entry(document::PartHistoryKind::Feature,extrusion.id);next.history.push_back(extrusion);
    auto calculated=workspace::calculate_part_with_resolved_references(kernel,next);body->session.commit(std::move(next),std::move(calculated));
    require(std::abs(body->session.calculated_boundaries().back().volume-100)<1e-6,"Extrusion fixture volume invalid");
    run(host,"sketch.dimension.create",{{"sketch",profile_id},{"kind","distance"},{"geometry",{rectangle[0]}},{"value",20}});
    require(std::abs(body->session.calculated_boundaries().back().volume-100)<1e-6,"Dimension implicitly recalculated its solid body");
    run(host,"regenerate");require(std::abs(body->session.calculated_boundaries().back().volume-200)<1e-6,"Explicit regeneration did not consume new profile dimension");
    run(host,"new",{{"type","assembly"},{"name","assembly-dimensions"}});
    const auto assembly_id=live.active_document_id();const auto assembly_sketch=run(host,"sketch.create",{{"name","Layout"}}).data.at("sketch");
    const auto assembly_circle=run(host,"sketch.circle.create",{{"sketch",assembly_sketch},{"center",{0,0}},{"radius_mm",3}}).data.at("geometry");
    const auto assembly_dimension=run(host,"sketch.dimension.create",{{"sketch",assembly_sketch},{"kind","diameter"},{"geometry",{assembly_circle}},{"value",10},{"layout",{{"text_along",6}}}}).data.at("dimension");
    auto* assembly=live.open_assembly(assembly_id);
    require(assembly->session.document().sketches.back().circles.front().radius==5 && assembly->session.document().dimension_layouts.back().layout.text_along==6,"Assembly dimension and label did not share the transaction");
    run(host,"undo");require(assembly->session.document().sketches.back().dimensions.empty() && assembly->session.document().dimension_layouts.empty(),"Assembly dimension Undo retained part of the transaction");run(host,"redo");
    run(host,"save");const auto loaded_assembly=assembly::AssemblyDocument::load(directory/"assembly-dimensions.asmz");
    require(loaded_assembly.sketches.back().dimensions.front().id==assembly_dimension.get<std::string>() && loaded_assembly.dimension_layouts==assembly->session.document().dimension_layouts,"Assembly dimension native persistence failed");
}
}
int main() {
    try {kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto directory=parent/("zima-sketch-dimensions-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);verify(kernel,directory);require(directory.parent_path()==parent,"Unsafe cleanup");fs::remove_all(directory);
        std::cout<<"Sketch dimension factories, value policies, labels, atomicity, Undo, persistence and explicit solid regeneration passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

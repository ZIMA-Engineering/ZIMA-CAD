#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <algorithm>
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
    workspace::Workspace live;command_host::Options options;command_host::Interaction interaction;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","relations"}});const auto document=live.active_document_id();std::string sketch;
    const auto create=[&](const std::string& name){sketch=run(host,"sketch.create",{{"name",name}}).data.at("sketch").get<std::string>();};
    const auto command=[&](const char* name,Json args=Json::object()){args["sketch"]=sketch;return run(host,name,std::move(args)).data;};
    const auto current=[&]{return workspace::document_sketch(live,document,sketch);};
    const auto point=[&](double x,double y){return command("sketch.point.create",{{"position",{x,y}}}).at("point").get<std::string>();};
    const auto segment=[&](double x1,double y1,double x2,double y2){return command("sketch.segment.create",{{"first",{x1,y1}},{"second",{x2,y2}}}).at("geometry").get<std::string>();};
    const auto circle=[&](double x,double y,double r){return command("sketch.circle.create",{{"center",{x,y}},{"radius_mm",r}}).at("geometry").get<std::string>();};
    const auto relation=[&](const std::string& kind,Json points,Json geometry){return command("sketch.constraint.create",{{"kind",kind},{"points",points},{"geometry",geometry}});};
    const auto checked=[&]{const auto result=command("sketch.solve");require(result.at("status")!="invalid" && result.at("status")!="conflicting" && result.at("maximum_residual").get<double>()<1e-6,"Constraint solver residual too large");};
    for(const auto* kind:{"horizontal","vertical"})for(bool pair:{false,true}) {
        create(std::string(kind)+(pair?" points":" segment"));std::string a,b;Json lines=Json::array();
        if(pair){a=point(1,2);b=point(4,5);}else {lines.push_back(segment(1,2,4,5));const auto s=current().segments.front();a=s.first_point_id;b=s.second_point_id;}
        relation(kind,pair?Json::array({a,b}):Json::array(),lines);checked();const auto s=current();
        require(std::string(kind)=="horizontal"?std::abs(s.find_point(a)->y-s.find_point(b)->y)<1e-7:std::abs(s.find_point(a)->x-s.find_point(b)->x)<1e-7,"Directional constraint has wrong axis");
    }
    for(const auto* kind:{"parallel","perpendicular","equal_length"}) {
        create(kind);const auto a=segment(0,0,10,0),b=segment(20,0,26,3);relation(kind,Json::array(),{a,b});checked();const auto s=current();
        const auto vector=[&](const auto& line){const auto* a=s.find_point(line.first_point_id);const auto* b=s.find_point(line.second_point_id);return std::array{b->x-a->x,b->y-a->y};};
        const auto u=vector(s.segments[0]),v=vector(s.segments[1]);const double residual=std::string(kind)=="parallel"?u[0]*v[1]-u[1]*v[0]:std::string(kind)=="perpendicular"?u[0]*v[0]+u[1]*v[1]:std::hypot(u[0],u[1])-std::hypot(v[0],v[1]);
        require(std::abs(residual)<1e-6,"Pair constraint produced wrong geometry");
    }
    for(const auto* kind:{"equal_radius","concentric"}) {
        create(kind);const auto a=circle(0,0,2),b=circle(10,0,3);relation(kind,Json::array(),{a,b});checked();const auto s=current();
        if(std::string(kind)=="equal_radius")require(std::abs(s.circles[0].radius-s.circles[1].radius)<1e-7,"Radii differ after equality");
        else {const auto* a=s.find_point(s.circles[0].center_point_id);const auto* b=s.find_point(s.circles[1].center_point_id);require(std::hypot(a->x-b->x,a->y-b->y)<1e-7,"Concentric circles have different centers");}
    }
    for(const auto* kind:{"point_on_circle","point_on_line","midpoint"}) {
        create(kind);const auto geometry=std::string(kind)=="point_on_circle"?circle(0,0,5):segment(0,0,20,0);const auto p=point(2,3);
        relation(kind,{p},{geometry});checked();const auto s=current();const auto* value=s.find_point(p);
        if(std::string(kind)=="point_on_circle") {const auto* center=s.find_point(s.circles[0].center_point_id);require(std::abs(std::hypot(value->x-center->x,value->y-center->y)-s.circles[0].radius)<1e-7,"Point not on circle");}
        else {const auto* a=s.find_point(s.segments[0].first_point_id);const auto* b=s.find_point(s.segments[0].second_point_id);
            require(std::abs((value->x-a->x)*(b->y-a->y)-(value->y-a->y)*(b->x-a->x))<1e-6,"Point not on line");
            if(std::string(kind)=="midpoint")require(std::hypot(value->x-(a->x+b->x)/2,value->y-(a->y+b->y)/2)<1e-7,"Point not at midpoint");}
    }
    create("midpoint on line");const auto midpoint_line=segment(0,4,10,8);relation("midpoint_on_line",Json::array(),{midpoint_line,"sketch_axis:x"});checked();
    {const auto s=current();require(std::abs(s.find_point(s.segments[0].first_point_id)->y+s.find_point(s.segments[0].second_point_id)->y)<1e-7,"Midpoint not on base axis");}
    create("symmetric");const auto a=point(2,3),b=point(-1,-4);relation("symmetric",{a,b},{"sketch_axis:x"});checked();
    {const auto s=current();require(std::abs(s.find_point(a)->x-s.find_point(b)->x)<1e-7 && std::abs(s.find_point(a)->y+s.find_point(b)->y)<1e-7,"Symmetry about wrong axis");}
    create("tangent");const auto tangent_circle=circle(0,0,5),tangent_line=segment(5,0,9,2);const auto contact=current().segments[0].first_point_id;
    relation("point_on_circle",{contact},{tangent_circle});
    relation("tangent",{contact},{tangent_circle,tangent_line});checked();
    {const auto s=current();const auto* c=s.find_point(s.circles[0].center_point_id);const auto* a=s.find_point(s.segments[0].first_point_id);const auto* b=s.find_point(s.segments[0].second_point_id);
        require(std::abs((a->x-c->x)*(b->x-a->x)+(a->y-c->y)*(b->y-a->y))<1e-6,"Tangent not perpendicular to contact radius");}
    create("point reference");const auto anchored=point(3,4);const auto reference=relation("point_reference",{anchored,"sketch_origin"},Json::array()).at("constraint").get<std::string>();checked();
    require(std::hypot(current().find_point(anchored)->x,current().find_point(anchored)->y)<1e-9 && command("sketch.solve_status").at("remaining_degrees_of_freedom")==0,"Origin reference did not fix point");
    command("sketch.constraint.delete",{{"constraint",reference}});require(command("sketch.solve_status").at("remaining_degrees_of_freedom")==2,"Deleting reference did not release point");run(host,"undo");require(current().constraints.size()==1,"Constraint deletion Undo failed");run(host,"redo");
    auto* state=live.open_part(document);const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();const auto before=current().serialized();
    command("sketch.solve_status");require(current().serialized()==before && state->session.revision()==revision && cache==state->session.calculated_boundaries().data(),"Solve query changed real Sketch or cache");
    for(const Json& args:std::vector<Json>{
        {{"kind","horizontal"},{"points",{anchored}},{"geometry",Json::array()}},
        {{"kind","unknown"}},{{"kind","point_reference"},{"points",{anchored,"missing"}}},
        {{"kind","coincident"},{"points",{anchored,anchored}}},{{"kind","tangent"},{"geometry",Json::array()}},
        {{"kind","horizontal"},{"points",{anchored,3}}}}) {
        auto input=args;input["sketch"]=sketch;require(!host.execute({{"command","sketch.constraint.create"},{"arguments",input}}).ok && state->session.revision()==revision,"Invalid relation partly committed");
    }
    require(host.execute({{"command","sketch.solve"},{"arguments",{{"sketch",sketch},{"iterations",0}}}}).code=="invalid_arguments","Invalid solver budget accepted");
    interaction.editing=true;command("sketch.solve_status");require(host.execute({{"command","sketch.solve"},{"arguments",{{"sketch",sketch}}}}).code=="editing_in_progress","Solve overwrote pending edit");interaction.editing=false;
    create("coincident");const auto left=segment(0,0,0,5),right=segment(10,0,10,5);const auto first=current().segments[0].first_point_id,absorbed=current().segments[1].first_point_id;
    const auto merged=relation("coincident",{first,absorbed},Json::array());require(merged.at("point")==first && !current().find_point(absorbed) && current().segments[1].first_point_id==first && current().constraints.empty(),"Coincidence did not merge topology");
    run(host,"undo");require(current().find_point(absorbed),"Point merge Undo failed");run(host,"redo");
    run(host,"save");const auto loaded=document::PartDocument::load(directory/"relations.prtz");
    for(std::size_t i=0;i<loaded.sketches.size();++i)require(loaded.sketches[i].serialized()==state->session.document().sketches[i].serialized(),"Relation persistence changed topology");
}
}
int main() {
    try {kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto directory=parent/("zima-sketch-relations-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);verify(kernel,directory);require(directory.parent_path()==parent,"Unsafe cleanup");fs::remove_all(directory);std::cout<<"All native Sketch relation kinds, geometric residuals, DOF, topology merge, Undo and persistence passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

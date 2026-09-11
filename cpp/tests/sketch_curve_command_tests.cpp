#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/sketcher/curve_geometry.hpp>
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
double error(kernel::Vec3 a,kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","curve-operations"}});
    const auto document=live.active_document_id();std::string sketch;
    const auto create=[&](const char* name){sketch=run(host,"sketch.create",{{"name",name}}).data.at("sketch").get<std::string>();};
    const auto command=[&](const char* name,Json args=Json::object()){args["sketch"]=sketch;return run(host,name,std::move(args)).data;};
    const auto current=[&]{return workspace::document_sketch(live,document,sketch);};
    const auto line=[&](double x1,double y1,double x2,double y2){return command("sketch.segment.create",{{"first",{x1,y1}},{"second",{x2,y2}}}).at("geometry").get<std::string>();};
    create("offset");const auto source=line(0,10,20,10);
    const auto offset=command("sketch.offset.create",{{"source",source},{"distance_mm",2}}).at("geometry").get<std::string>();
    const auto visible=[&](const std::string& id,double parameter){return kernel::bspline_value(sketcher::sketch_curve_geometry(current(),id),parameter);};
    require(error(visible(offset,.3),{6,12,0})<1e-10,"Offset distance/direction changed");
    command("sketch.curve.retain",{{"geometry",source},{"intervals",{{.2,.8}}}});
    require(error(kernel::bspline_value(current().supporting_curve(source),0),{0,10,0})<1e-10 && error(visible(offset,0),{0,12,0})<1e-10,"Source trim discarded offset support");
    const auto pieces=command("sketch.curve.retain",{{"geometry",offset},{"intervals",{{.1,.4},{.6,.9}}}}).at("geometry");
    require(pieces.size()==2 && pieces[0]==offset,"Retained pieces lost original identity");const auto second=pieces[1].get<std::string>();
    command("sketch.offset.set",{{"geometry",second},{"distance_mm",3},{"flipped",true}});
    require(command("sketch.offset.get",{{"geometry",offset}}).at("distance_mm")==3 && error(visible(offset,0),{2,7,0})<1e-10 && error(visible(second,0),{12,7,0})<1e-10,"Offset piece edit failed to update its operation");
    const auto before_free=sketcher::sketch_curve_geometry(current(),offset);command("sketch.offset.free",{{"geometry",offset}});
    require(!current().find_offset(offset) && sketcher::sketch_curve_geometry(current(),offset)==before_free,"Free changed curve shape or ID");
    run(host,"undo");require(current().find_offset(offset),"Offset free Undo failed");run(host,"redo");
    const auto curve=command("sketch.curve.get",{{"geometry",source}});require(curve.at("start")==.2 && curve.at("end")==.8 && curve.at("support").at("poles")[0]==Json::array({0,10,0}),"Curve query lost full support or visible interval");
    require(command("sketch.curve.get",{{"geometry",source},{"limit",1}}).at("geometry_omitted_by_limit")==true,"Curve query ignored limit");
    auto* part=live.open_part(document);const auto revision=part->session.revision();const auto* cache=part->session.calculated_boundaries().data();
    require(command("sketch.curve.retain",{{"geometry",source},{"intervals",{{0,1}}}}).at("changed")==false,"Retain entire visible curve committed");
    require(part->session.revision()==revision && cache==part->session.calculated_boundaries().data(),"Read/no-op changed cache");
    for(const Json& request:std::vector<Json>{
        {{"command","sketch.offset.create"},{"arguments",{{"sketch",sketch},{"source",source},{"distance_mm",-1}}}},
        {{"command","sketch.offset.set"},{"arguments",{{"sketch",sketch},{"geometry",second},{"source",second}}}},
        {{"command","sketch.curve.retain"},{"arguments",{{"sketch",sketch},{"geometry",source},{"intervals",{{.2,.8},{.5,1}}}}}},
        {{"command","sketch.curve.retain"},{"arguments",{{"sketch",sketch},{"geometry",source},{"intervals",{{-1,1}}}}}},
        {{"command","sketch.offset.create"},{"arguments",{{"sketch",sketch},{"source","external:missing"},{"distance_mm",1}}}}}) {
        require(!host.execute(request).ok && part->session.revision()==revision && cache==part->session.calculated_boundaries().data(),"Invalid curve edit partly committed");
    }
    create("intersection");const auto baseline=line(0,10,20,10);
    const auto follower=command("sketch.offset.create",{{"source",baseline},{"distance_mm",2}}).at("geometry").get<std::string>();
    const auto cutter=line(10,5,10,15);command("sketch.curve.retain",{{"geometry",follower},{"intervals",{{0,.5}}}});
    require(!command("sketch.offset.get",{{"geometry",follower}}).at("end_anchor").is_null(),"Trim lost cutter reference");
    command("sketch.translate",{{"geometry",{cutter}},{"delta",{.2,0}}});
    require(error(visible(follower,1),{10.2,12,0})<1e-7,"Small cutter movement lost intersection branch");
    command("sketch.geometry.delete",{{"geometry",cutter}});require(command("sketch.offset.get",{{"geometry",follower}}).at("broken")==true,"Missing cutter was silently replaced");run(host,"undo");
    create("trim");const auto horizontal=line(0,10,20,10);const auto vertical=line(10,5,10,15);
    const auto topology=command("sketch.trim.pieces",{{"geometry",horizontal},{"include_axes",false}});
    require(topology.at("items").size()==2,"Trim did not split at native intersection");
    const auto& first=topology.at("items")[0];Json selected={{"geometry",first.at("geometry")},{"start",first.at("start")},{"end",first.at("end")}};
    command("sketch.trim",{{"pieces",Json::array({selected})},{"include_axes",false}});
    const auto after_trim=current().serialized();run(host,"undo");require(current().serialized()!=after_trim,"Trim Undo failed");
    command("sketch.translate",{{"geometry",{vertical}},{"delta",{1,0}}});const auto stale_revision=part->session.revision();
    require(host.execute({{"command","sketch.trim"},{"arguments",{{"sketch",sketch},{"pieces",Json::array({selected})},{"include_axes",false}}}}).code=="stale_geometry" && part->session.revision()==stale_revision,"Stale trim silently selected a new piece");
    create("spline");const auto spline=command("sketch.bspline.create",{{"points",{{0,0},{3,8},{7,-3},{10,0}}},{"degree",3}}).at("geometry").get<std::string>();
    const auto spline_support=current().supporting_curve(spline);const auto spline_offset=command("sketch.offset.create",{{"source",spline},{"distance_mm",.1}}).at("geometry").get<std::string>();
    const auto offset_shape=sketcher::sketch_curve_geometry(current(),spline_offset);
    for(int i=0;i<=1024;++i)require(error(kernel::bspline_value(offset_shape,i/1024.),sketcher::curve_offset_point(spline_support,i/1024.,.1))<1e-5,"Commanded spline offset exceeded native tolerance");
    command("sketch.curve.retain",{{"geometry",spline},{"intervals",{{.13,.86}}}});const auto spline_trim=sketcher::sketch_curve_geometry(current(),spline);
    for(int i=0;i<=256;++i)require(error(kernel::bspline_value(spline_trim,i/256.),kernel::bspline_value(spline_support,.13+.73*i/256.))<1e-10,"Commanded spline trim changed supporting geometry");
    create("mirror");const auto original=line(1,2,4,2);const auto mirrored=command("sketch.mirror",{{"entities",{original}},{"axis","sketch_axis:y"}});
    const auto mirrored_id=mirrored.at("geometry")[0].get<std::string>();const auto mirror_curve=sketcher::sketch_curve_geometry(current(),mirrored_id);
    require(error(kernel::bspline_value(mirror_curve,0),{-1,2,0})<1e-10 && error(kernel::bspline_value(mirror_curve,1),{-4,2,0})<1e-10,"Mirror reflected about wrong axis");
    create("oriented rectangle");require(command("sketch.oriented_rectangle.create",{{"first",{0,4}},{"guide",{12,7}},{"axis","sketch_axis:x"}}).at("geometry").size()==4,"Oriented rectangle failed");
    create("tangent arc");const auto tangent=line(0,0,10,0);const auto start=current().segments.front().second_point_id;
    command("sketch.tangent_arc.create",{{"start_point",start},{"end",{20,10}},{"tangent",tangent}});
    require(current().arcs.size()==1 && std::ranges::any_of(current().constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::Tangent;}),"Tangent arc lost relation");
    create("common tangent");const auto a=command("sketch.circle.create",{{"center",{0,0}},{"radius_mm",2}}).at("geometry").get<std::string>();
    const auto b=command("sketch.circle.create",{{"center",{10,0}},{"radius_mm",2}}).at("geometry").get<std::string>();
    const auto common=command("sketch.common_tangent.create",{{"first",a},{"first_hint",{0,2}},{"second",b},{"second_hint",{10,2}}}).at("geometry").get<std::string>();
    require(error(kernel::bspline_value(sketcher::sketch_curve_geometry(current(),common),0),{0,2,0})<1e-6,"Common tangent branch changed");
    create("corner");const auto left=line(-10,0,0,0),up=line(0,0,0,10);const auto source_segments=current().segments;
    const auto corner=command("sketch.corner_fillet.create",{{"first",left},{"second",up},{"radius_mm",2}}).at("geometry").get<std::string>();
    require(!corner.empty() && current().segments==source_segments && current().corner_radii.front().radius==2,"Corner round destroyed source topology");
    run(host,"save");const auto loaded=document::PartDocument::load(directory/"curve-operations.prtz");
    require(loaded.sketches.size()==part->session.document().sketches.size(),"Curve operation save lost sketches");
    for(std::size_t i=0;i<loaded.sketches.size();++i)require(loaded.sketches[i].serialized()==part->session.document().sketches[i].serialized(),"Curve operations lost native definitions on save");
}
}
int main() {
    try {kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto directory=parent/("zima-sketch-curves-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);verify(kernel,directory);require(directory.parent_path()==parent,"Unsafe cleanup");fs::remove_all(directory);
        std::cout<<"Offset chains, exact spline trim, stale pieces, tangency, symmetry, Undo and persistence passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

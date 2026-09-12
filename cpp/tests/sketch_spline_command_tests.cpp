#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
double distance(kernel::Vec3 a,kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);run(host,"new",{{"type","part"},{"name","spline-properties"}});
    const auto document=live.active_document_id();auto* part=live.open_part(document);std::string sketch;
    const auto create=[&](const char* name){sketch=run(host,"sketch.create",{{"name",name}}).data.at("sketch").get<std::string>();};
    const auto command=[&](const char* name,Json args=Json::object()){args["sketch"]=sketch;return run(host,name,std::move(args)).data;};
    const auto current=[&]{return workspace::document_sketch(live,document,sketch);};
    const auto edit=[&](const auto& mutation){static_cast<void>(workspace::mutate_document_sketch(live,document,sketch,mutation));};
    const auto reject=[&](const std::string& id,Json patch) {
        const auto before=current().serialized();const auto revision=part->session.revision();const auto* cache=part->session.calculated_boundaries().data();
        patch["sketch"]=sketch;patch["geometry"]=id;
        require(!host.execute({{"command","sketch.bspline.set"},{"arguments",patch}}).ok,"Invalid spline edit accepted");
        require(current().serialized()==before && part->session.revision()==revision && part->session.calculated_boundaries().data()==cache,"Rejected spline edit changed document/cache");
    };
    create("Bezier");const auto id=command("sketch.bspline.create",{{"points",{{0,0},{3,8},{7,-3},{10,0}}},{"degree",3}}).at("geometry").get<std::string>();
    const auto original=current();const auto ids=original.bsplines.front().control_point_ids;
    const auto follower=command("sketch.offset.create",{{"source",id},{"distance_mm",.1}}).at("geometry").get<std::string>();
    command("sketch.bspline.set",{{"geometry",id},{"points",{{0,0},{3,10},{7,2},{10,0}}}});
    auto changed=current();require(changed.bsplines.front().control_point_ids==ids,"Spline edit replaced stable pole IDs");
    // Independent cubic Bernstein evaluation at t=1/2: 1/8 P0 + 3/8 P1 + 3/8 P2 + 1/8 P3.
    require(distance(kernel::bspline_value(sketcher::sketch_curve_geometry(changed,id),.5),{5,4.5,0})<1e-10,"Spline edit changed the intended cubic geometry");
    const auto curve=changed.supporting_curve(id);const auto offset=sketcher::sketch_curve_geometry(changed,follower);
    for(int i=0;i<=128;++i)require(distance(kernel::bspline_value(offset,i/128.),sketcher::curve_offset_point(curve,i/128.,.1))<1e-5,"Source edit did not refresh its offset");
    require(command("sketch.bspline.get",{{"geometry",id}}).at("point_ids")==Json(ids),"Spline query changed pole order");
    require(command("sketch.bspline.get",{{"geometry",id},{"limit",1}}).at("geometry_omitted_by_limit")==true,"Spline query ignored output limit");
    const auto revision=part->session.revision();const auto* cache=part->session.calculated_boundaries().data();
    require(command("sketch.bspline.set",{{"geometry",id}}).at("changed")==false && part->session.revision()==revision && part->session.calculated_boundaries().data()==cache,"Spline no-op calculated or committed");
    run(host,"undo");require(current().find_point(ids[1])->y==8,"Spline Undo lost original coordinates");run(host,"redo");require(current().serialized()==changed.serialized(),"Spline Redo changed dependent geometry");
    reject(id,{{"degree",0}});reject(id,{{"degree",4}});reject(id,{{"degree",2.5}});reject(id,{{"points",{{0,0},{3,4}}}});reject(id,{{"points",{{0,0},{0,0},{7,2},{10,0}}}});reject(id,{{"points",{{0,0},{3,"x"},{7,2},{10,0}}}});
    command("sketch.point.fixed",{{"point",ids[0]},{"fixed",true}});reject(id,{{"points",{{1,0},{3,10},{7,2},{10,0}}}});
    command("sketch.bspline.set",{{"geometry",id},{"degree",2},{"closed",true}});require(current().bsplines.front().degree==2 && current().bsplines.front().closed,"Native spline shape properties not applied");
    require(current().find_point(ids[0])->fixed && !current().find_point(ids[1])->fixed,"Transient solver anchors leaked into persistent fixed state");
    reject(follower,{{"degree",1}});
    create("exact");const auto exact=command("sketch.bspline.create",{{"points",{{1,0},{1,1},{0,1}}},{"degree",2}}).at("geometry").get<std::string>();
    edit([&](auto& s){auto& spline=s.bsplines.front();spline.knots={0,0,0,1,1,1};spline.weights={1,std::sqrt(.5),1};});
    const auto exact_before=current().bsplines.front();
    command("sketch.bspline.set",{{"geometry",exact},{"points",{{2,0},{2,2},{0,2}}}});
    changed=current();require(changed.bsplines.front()==exact_before,"Exact spline edit replaced identity, knots or weights");
    const auto circle=sketcher::sketch_curve_geometry(changed,exact);
    for(int i=0;i<=128;++i){const auto p=kernel::bspline_value(circle,i/128.);require(std::abs(p.x*p.x+p.y*p.y-4)<1e-12,"Exact rational circle was approximated during editing");}
    reject(exact,{{"degree",1}});reject(exact,{{"closed",true}});
    std::string block;edit([&](auto& s){block=s.add_import_block("source","external-reference:fixture",{exact},s.bsplines.front().control_point_ids);});
    require(command("sketch.bspline.get",{{"geometry",exact}}).at("read_only")==true,"Linked spline query is editable");
    reject(exact,{{"points",{{3,0},{3,3},{0,3}}}});
    require(command("sketch.bspline.set",{{"geometry",exact}}).at("changed")==false,"Unchanged linked properties failed");
    edit([&](auto& s){s.import_blocks.clear();});command("sketch.curve.retain",{{"geometry",exact},{"intervals",{{.1,.9}}}});reject(exact,{{"degree",1}});
    create("constrained");const auto constrained=command("sketch.bspline.create",{{"points",{{0,0},{3,0},{7,3},{10,0}}},{"degree",3}}).at("geometry").get<std::string>();
    const auto constrained_ids=current().bsplines.front().control_point_ids;
    edit([&](auto& s){static_cast<void>(s.add_point_pair_constraint(constrained_ids[0],constrained_ids[1],sketcher::ConstraintKind::Horizontal));});
    reject(constrained,{{"points",{{0,0},{3,1},{7,3},{10,0}}}});
    command("sketch.bspline.set",{{"geometry",constrained},{"points",{{0,2},{3,2},{7,3},{10,0}}}});
    require(current().constraints.size()==1 && current().find_point(constrained_ids[0])->y==2 && current().find_point(constrained_ids[1])->y==2,"Simultaneous constrained edit failed");
    run(host,"save");const auto restored=document::PartDocument::load(dir/"spline-properties.prtz");
    for(std::size_t i=0;i<restored.sketches.size();++i)require(restored.sketches[i].serialized()==part->session.document().sketches[i].serialized(),"Spline persistence changed geometry or constraints");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-spline-properties-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Spline properties, rational geometry, dependencies, fixed points, constraints, atomicity and persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

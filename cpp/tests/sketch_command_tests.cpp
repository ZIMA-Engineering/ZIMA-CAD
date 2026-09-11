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
commands::Result run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;command_host::Interaction interaction;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","sketches"}});const auto document=live.active_document_id();
    run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}});
    const auto created=run(host,"sketch.create",{{"name","Profil"},{"plane","XY"}}).data;
    const auto sketch=created.at("sketch").get<std::string>(),owner=created.at("owner").get<std::string>();
    auto* state=live.open_part(document);
    require(!owner.empty() && state->session.document().find_container(owner)->feature_kind==document::FeatureKind::Sketch &&
        state->session.document().body_owner_for_object(owner),"Part Sketch lost its owner or Body");
    const auto geometry=[&](const std::string& command,Json args) {args["sketch"]=sketch;return run(host,command,args).data;};
    const auto entity=[&](const std::string& id) {return geometry("sketch.entity.get",{{"entity",id}}).at("entity");};
    const auto current=[&] {return workspace::document_sketch(live,document,sketch);};
    const auto point=geometry("sketch.point.create",{{"position",{10,20}}}).at("point").get<std::string>();
    require(entity(point).at("x")==10 && entity(point).at("y")==20,"Point coordinates changed units");
    geometry("sketch.point.move",{{"point",point},{"position",{15,25}}});
    require(current().find_point(point)->x==15 && current().find_point(point)->y==25,"Point move failed");
    run(host,"undo");require(current().find_point(point)->x==10,"Point move Undo failed");run(host,"redo");
    geometry("sketch.point.fixed",{{"point",point},{"fixed",true}});
    const auto fixed_revision=state->session.revision();
    require(!host.execute({{"command","sketch.point.move"},{"arguments",{{"sketch",sketch},{"point",point},{"position",{16,25}}}}}).ok && state->session.revision()==fixed_revision && current().find_point(point)->x==15,"Fixed point moved or failure partly committed");
    require(geometry("sketch.point.move",{{"point",point},{"position",{15,25}}}).at("changed")==false,"No-op fixed point move committed");
    geometry("sketch.point.fixed",{{"point",point},{"fixed",false}});
    const auto segment=geometry("sketch.segment.create",{{"first",{0,0}},{"second",{10,0}}}).at("geometry").get<std::string>();
    geometry("sketch.segment.centerline",{{"segment",segment},{"centerline",true}});require(entity(segment).at("centerline")==true,"Centerline flag lost");
    geometry("sketch.segment.centerline",{{"segment",segment},{"centerline",false}});
    geometry("sketch.geometry.construction",{{"geometry",segment},{"construction",true}});require(entity(segment).at("construction")==true,"Construction state lost");
    const auto circle=geometry("sketch.circle.create",{{"center",{50,50}},{"radius_mm",7}}).at("geometry").get<std::string>();
    require(entity(circle).at("radius")==7,"Circle replaced by polygon");
    const auto arc=geometry("sketch.arc.create",{{"center",{100,100}},{"start",{110,100}},{"end",{100,110}}}).at("geometry").get<std::string>();
    require(std::abs(entity(arc).at("end_angle").get<double>()-std::acos(-1.0)/2)<1e-8,"Arc sweep changed");
    const auto ellipse=geometry("sketch.ellipse.create",{{"center",{200,200}},{"major",{210,200}},{"minor",{200,205}}}).at("geometry").get<std::string>();
    require(entity(ellipse).at("major_radius")==10 && entity(ellipse).at("minor_radius")==5,"Ellipse axes changed");
    const auto elliptical=geometry("sketch.elliptical_arc.create",{{"center",{300,300}},{"major",{310,300}},{"minor",{300,305}},{"start",{310,300}},{"end",{300,305}}}).at("geometry").get<std::string>();
    require(entity(elliptical).at("major_radius")==10,"Elliptical arc lost exact geometry");
    const auto spline=geometry("sketch.bspline.create",{{"points",{{0,40},{3,45},{6,35},{10,40}}},{"degree",3}}).at("geometry").get<std::string>();
    require(entity(spline).at("degree")==3 && entity(spline).at("control_points").size()==4 && current().supporting_curve(spline).poles.size()==4,"B-spline exact support unavailable");
    const auto rectangle=geometry("sketch.rectangle.create",{{"first",{400,400}},{"second",{420,410}}}).at("geometry");require(rectangle.size()==4,"Rectangle did not create four segments");
    const auto polygon=geometry("sketch.polygon.create",{{"center",{500,500}},{"rim",{510,500}},{"sides",6}}).at("geometry");require(polygon.size()==6,"Polygon side count changed");
    geometry("sketch.translate",{{"delta",{2,3}},{"geometry",{circle}}});
    const auto center=entity(circle).at("center").get<std::string>();require(entity(center).at("x")==52 && entity(center).at("y")==53,"Translation changed circle radius or ignored center");
    require(entity(circle).at("radius")==7,"Circle translation resized it");
    // Queries and no-ops preserve the exact cache allocation and revision.
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    run(host,"sketch.list");geometry("sketch.get",{});const auto page=geometry("sketch.entities",{{"limit",2}});
    require(page.at("items").size()==2 && page.at("more")==true && geometry("sketch.entities",{{"offset",2},{"limit",2}}).at("items")[0]!=page.at("items")[0],"Entity pagination repeated IDs");
    require(geometry("sketch.translate",{{"delta",{0,0}},{"geometry",{circle}}}).at("changed")==false,"Zero translation committed");
    require(state->session.revision()==revision && cache==state->session.calculated_boundaries().data() && !host.change(),"Query/no-op changed cache or revision");
    for(const Json& request:std::vector<Json>{
        {{"command","sketch.point.create"},{"arguments",{{"sketch",sketch},{"position",{1,"bad"}}}}},
        {{"command","sketch.point.create"},{"arguments",{{"sketch",sketch},{"position",{1,2,3}}}}},
        {{"command","sketch.circle.create"},{"arguments",{{"sketch",sketch},{"center",{1,2}},{"radius_mm",-1}}}},
        {{"command","sketch.bspline.create"},{"arguments",{{"sketch",sketch},{"points",{{1,2},{3,4}}},{"degree",-1}}}},
        {{"command","sketch.geometry.delete"},{"arguments",{{"sketch",sketch},{"geometry","missing"}}}},
        {{"command","sketch.point.fixed"},{"arguments",{{"sketch",sketch},{"point",point},{"fixed","true"}}}},
        {{"command","sketch.create"},{"arguments",{{"name","bad"},{"plane","oops"}}}},
        {{"command","sketch.entities"},{"arguments",{{"sketch",sketch},{"limit",0}}}}}) {
        require(!host.execute(request).ok && state->session.revision()==revision && cache==state->session.calculated_boundaries().data(),"Rejected geometry command partly committed");
    }
    geometry("sketch.geometry.delete",{{"geometry",arc}});require(current().arcs.empty(),"Arc deletion failed");run(host,"undo");require(current().arcs.size()==1,"Deletion Undo failed");
    geometry("sketch.point.delete",{{"point",point}});require(!current().find_point(point),"Point deletion failed");run(host,"undo");
    interaction.editing=true;geometry("sketch.get",{});
    require(host.execute({{"command","sketch.point.create"},{"arguments",{{"sketch",sketch},{"position",{1,2}}}}}).code=="editing_in_progress","Command overwrote pending Sketcher draft");interaction.editing=false;
    require(std::abs(state->session.calculated_boundaries().back().volume-1000)<1e-7,"Sketch mutation changed cached solid");
    run(host,"save");std::vector<kernel::BodyResult> loaded_cache;const auto loaded=document::PartDocument::load(directory/"sketches.prtz",&loaded_cache);
    require(loaded.sketches.back().serialized()==current().serialized() && !loaded_cache.empty() && std::abs(loaded_cache.back().volume-1000)<1e-7,"Native save lost exact Sketch geometry or cached body");
    const auto active_body=state->session.document().body_history.active_body_id();run(host,"body.create",{{"name","Other"}});
    require(host.execute({{"command","sketch.point.create"},{"arguments",{{"sketch",sketch},{"position",{1,2}}}}}).code=="inactive_body","Edited Sketch in inactive Body");run(host,"body.activate",{{"body",active_body}});
    for(const auto* plane:{"XZ","YZ"}) {
        const auto result=run(host,"sketch.create",{{"name",plane},{"plane",plane}}).data;
        require(result.at("plane")==plane,"Part Sketch creation changed selected base plane");
    }
    bool borrowed=false;workspace::visit_document_sketches(live,document,[&](const auto& item){borrowed=&item==&state->session.document().sketches.front();return false;});
    require(borrowed,"Sketch list cloned the entire document geometry");
    // Embedded profile edits retain the feature's identity and cached body.
    auto embedded=sketcher::Sketch::create_default();const auto embedded_id=embedded.id;
    auto next=state->session.document();auto sweep=document::PartDocument::create_sweep3d_container();
    sweep.sweep3d.profiles.resize(1);sweep.sweep3d.profiles.front().sketch_serialized=embedded.serialized();const auto sweep_id=sweep.id;
    next.insert_history_entry(document::PartHistoryKind::Feature,sweep_id);next.history.push_back(sweep);state->session.commit(std::move(next),state->session.calculated_boundaries());
    run(host,"sketch.point.create",{{"sketch",embedded_id},{"position",{4,6}}});
    require(sketcher::Sketch::from_serialized(state->session.document().find_container(sweep_id)->sweep3d.profiles.front().sketch_serialized).points.size()==1,"Embedded profile did not update owning container");
    run(host,"undo");require(workspace::document_sketch(live,document,embedded_id).points.empty(),"Embedded profile Undo failed");
    run(host,"new",{{"type","assembly"},{"name","assembly-sketch"}});const auto assembly=live.active_document_id();
    for(const auto* plane:{"XY","XZ","YZ"}) {
        const auto result=run(host,"sketch.create",{{"name",plane},{"plane",plane}}).data;const auto id=result.at("sketch").get<std::string>();
        require(result.at("owner")=="" && result.at("plane")==plane,"Standalone Assembly Sketch gained Part owner or changed plane");
        run(host,"sketch.circle.create",{{"sketch",id},{"center",{0,0}},{"radius_mm",2}});
    }
    run(host,"save");const auto saved=assembly::AssemblyDocument::load(directory/"assembly-sketch.asmz");require(saved.sketches.size()==3 && saved.sketches.back().circles.size()==1,"Assembly Sketch save failed");
    require(run(host,"sketch.get",{{"document",document},{"sketch",sketch}}).data.at("sketch")==sketch && live.active_document_id()==assembly,"Reading inactive Sketch activated its document");
    run(host,"new",{{"type","drawing"},{"name","drawing"}});require(host.execute_text("sketch.create wrong").code=="unsupported_document","Sketch command mutated a Drawing");
}
}
int main() {
    try {kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto directory=parent/("zima-sketch-commands-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);verify(kernel,directory);require(directory.parent_path()==parent,"Unsafe test cleanup");fs::remove_all(directory);
        std::cout<<"Sketch primitives, exact curves, constraints, ownership, no-ops, Undo and native persistence passed without Qt\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

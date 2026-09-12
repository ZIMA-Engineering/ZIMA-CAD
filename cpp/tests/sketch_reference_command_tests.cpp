#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",args}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message+" "+args.dump());return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","references"}});const auto doc=live.active_document_id();
    const auto box=run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();
    const auto sketch=run(host,"sketch.create",{{"name","Projection"}}).data.at("sketch").get<std::string>();
    auto* state=live.open_part(doc);const auto current=[&]{return workspace::document_sketch(live,doc,sketch);};
    const auto command=[&](const char* name,Json args=Json::object()){args["sketch"]=sketch;return run(host,name,std::move(args)).data;};
    const auto source=state->session.calculated_boundaries().back().mesh.original_references;
    const auto edge=*std::ranges::find_if(source.edges,[&](const auto& e){return e.reference.owner_id==box && e.points.size()>=2 && std::hypot(e.points.front().x-e.points.back().x,e.points.front().y-e.points.back().y)>1;});
    Json edge_args={{"kind","edge"},{"owner",box},{"key",edge.reference.semantic_key}};
    const auto cache=state->session.calculated_boundaries().back().volume;
    const auto ref=command("sketch.reference.create",edge_args).at("reference").get<std::string>();
    require(current().external_references.front().source_document_id==doc && state->session.calculated_boundaries().back().volume==cache,"Projection changed source cache or document identity");
    const auto own=command("sketch.reference.project",{{"reference",ref}}).at("geometry").get<std::string>();
    require(current().segments.size()==1 && current().import_blocks.front().source_path=="external-reference:"+ref,"Projected line did not retain its source link");
    for(int missing=0;missing<3;++missing) {
        auto invalid=current();auto& r=invalid.external_references.front();r.context_assembly_document_id="context";r.context_instance_path="dependent";r.source_instance_path="source";
        if(missing==0)r.source_instance_path.clear();if(missing==1)r.context_assembly_document_id.clear();if(missing==2)r.context_instance_path.clear();
        bool rejected=false;try{invalid.validate();}catch(const std::exception&){rejected=true;}require(rejected,"Incomplete in-context Part reference accepted");
    }
    const auto stable=current().serialized();const auto revision=state->session.revision();
    auto duplicate=edge_args;duplicate["sketch"]=sketch;
    require(!host.execute({{"command","sketch.reference.create"},{"arguments",duplicate}}).ok && current().serialized()==stable && state->session.revision()==revision,"Duplicate source partially committed");
    require(!host.execute({{"command","sketch.reference.project"},{"arguments",{{"sketch",sketch},{"reference",ref}}}}).ok,"Source produced duplicate native geometry");
    const auto points=current().points;command("sketch.reference.delete",{{"reference",ref}});
    require(current().external_references.empty() && current().points==points && current().segments.front().id==own,"Detach destroyed projected geometry");run(host,"undo");require(current().external_references.size()==1,"Detach Undo did not restore its dependency");
    const auto face=*std::ranges::find_if(source.triangle_references,[&](const auto& f){return f.owner_id==box && f.surface && std::abs(f.surface->axis.x)>.9;});
    command("sketch.reference.create",{{"kind","face"},{"owner",box},{"key",face.semantic_key}});
    require(current().external_references.back().infinite && current().external_references.back().cached_points.size()==2,"Planar face did not produce its intersection axis");
    const auto source_point=*std::ranges::find_if(source.points,[&](const auto& p){return p.reference.owner_id==box;});
    command("sketch.reference.create",{{"kind","point"},{"owner",box},{"key",source_point.reference.semantic_key}});
    require(current().external_references.back().cached_points.front()==current().local_point(source_point.position),"Point projection used wrong frame");
    const auto origin=state->session.document().find_container(box)->container_origin.id;
    const auto axes=run(host,"reference.list",{{"kind","axis"},{"owner",origin}}).data.at("items");
    bool axis_created=false;for(const auto& axis:axes) {
        const auto data=run(host,"reference.get",axis).data;
        if(std::abs(data.at("direction")[2].get<double>())>.9)continue;
        command("sketch.reference.create",axis);axis_created=true;break;
    }
    require(axis_created && current().external_references.back().infinite,"Original history axis could not be projected");
    auto bad=edge_args;bad["sketch"]=sketch;bad["key"]="result-only";
    require(!host.execute({{"command","sketch.reference.create"},{"arguments",bad}}).ok,"Missing source was guessed");
    const auto later=run(host,"box.create",{{"length_mm","2"},{"width_mm","2"},{"height_mm","2"}}).data.at("container");
    bad["owner"]=later;require(host.execute({{"command","sketch.reference.create"},{"arguments",bad}}).code=="invalid_reference_source","Forward dependency accepted");
    // An exact rational source deliberately has only two display sample points.
    auto cached=state->session.calculated_boundaries();kernel::ViewerEdge spline;
    spline.reference={box,"test-original-quarter-circle",{}};spline.points={{1,0,0},{0,1,0}};
    spline.exact_spline=kernel::BSplineGeometry{2,{{1,0,0},{1,1,0},{0,1,0}},{0,0,0,1,1,1},{1,std::sqrt(.5),1}};
    cached.back().mesh.original_references.edges.push_back(spline);state->session.commit(state->session.document(),std::move(cached));
    auto rational=command("sketch.reference.create",{{"kind","edge"},{"owner",box},{"key",spline.reference.semantic_key},{"profile",true}});
    const auto curve=rational.at("geometry").get<std::string>(),spline_ref=rational.at("reference").get<std::string>();
    require(current().bsplines.size()==1,"Exact source was fitted to display samples");
    for(int i=0;i<=256;++i){const auto p=kernel::bspline_value(current().supporting_curve(curve),i/256.);require(std::abs(p.x*p.x+p.y*p.y-1)<1e-12,"Rational source lost exact weights");}
    const auto offset=command("sketch.offset.create",{{"source",curve},{"distance_mm",.1},{"flipped",true}}).at("geometry").get<std::string>();
    command("sketch.curve.retain",{{"geometry",curve},{"intervals",{{.1,.9}}}});
    cached=state->session.calculated_boundaries();auto& moved=cached.back().mesh.original_references.edges.back();for(auto& p:moved.points)p.x+=.01;for(auto& p:moved.exact_spline->poles)p.x+=.01;
    const auto expected=sketcher::offset_curve_geometry(*moved.exact_spline,-.1);state->session.commit(state->session.document(),std::move(cached));
    command("sketch.reference.refresh");require(!current().external_references.back().broken,"Trimmed source became broken after a small change");
    for(int i=0;i<=256;++i){const auto a=kernel::bspline_value(current().supporting_curve(offset),i/256.),b=kernel::bspline_value(expected,i/256.);require(std::hypot(a.x-b.x,a.y-b.y)<1e-8,"Offset did not follow trimmed exact projection");}
    const auto retained=current().points;cached=state->session.calculated_boundaries();cached.back().mesh.original_references.edges.pop_back();state->session.commit(state->session.document(),std::move(cached));
    require(command("sketch.reference.refresh").at("broken_references").size()==1 && current().points==retained,"Missing source destroyed its last valid geometry");
    command("sketch.reference.delete",{{"reference",spline_ref}});require(current().points==retained,"Detaching broken spline changed its poles");
    run(host,"save");const auto reopened=document::PartDocument::load(directory/"references.prtz");require(reopened.sketches.back().serialized()==current().serialized(),"Native Part lost projected geometry or references");
    // Embedded Helical profiles obey the same preceding-source rule.
    auto embedded=sketcher::Sketch::create_default();auto helix=document::PartDocument::create_helical_sweep_container();embedded.owner_container_id=helix.id;helix.helical.sketches[2]=embedded.serialized();
    auto next=state->session.document();next.insert_history_entry(document::PartHistoryKind::Feature,helix.id);next.history.push_back(helix);state->session.commit(std::move(next),state->session.calculated_boundaries());
    auto embedded_args=edge_args;embedded_args["sketch"]=embedded.id;run(host,"sketch.reference.create",embedded_args);
    require(workspace::document_sketch(live,doc,embedded.id).external_references.front().source_owner_id==box,"Embedded Helical profile did not resolve earlier original source");
    run(host,"undo");run(host,"undo");
    // The same source appears through two nested occurrences. Use persisted
    // snapshots even when the source filenames are deliberately unavailable.
    kernel::BodySnapshot snapshot=state->session.calculated_boundaries().back();
    auto nested=assembly::AssemblyDocument::create_default();auto first=assembly::AssemblyDocument::create_part_occurrence("First",doc,"missing.prtz",snapshot);
    auto second=assembly::AssemblyDocument::create_part_occurrence("Second",doc,"missing.prtz",snapshot);first.placement.x=20;second.placement.y=30;nested.components={first,second};
    auto top=assembly::AssemblyDocument::create_default();auto parent=assembly::AssemblyDocument::create_assembly_occurrence("Nested",nested.document_id,"missing.asmz",nested);parent.placement.x=100;parent.placement.rotation_z=90;top.components={parent};
    live.add_assembly(top,directory/"references.asmz");run(host,"activate",{{"document",top.document_id}});
    const auto top_sketch=run(host,"sketch.create",{{"name","Assembly projection"}}).data.at("sketch").get<std::string>();
    const auto before_snapshot=live.open_assembly(top.document_id)->session.document().components.front().calculated_source;
    for(const auto& occurrence:{first,second}) {
        auto args=edge_args;args["sketch"]=top_sketch;const auto path=assembly::InstancePath{{parent.occurrence_id,occurrence.occurrence_id}};args["instance_path"]=path.encoded();
        run(host,"sketch.reference.create",args);const auto projected=workspace::document_sketch(live,top.document_id,top_sketch);
        const auto world=live.occurrence_point_to_scene(top.document_id,path,edge.points.front());const auto local=projected.local_point(world);
        require(projected.external_references.back().source_instance_path==path.encoded() && std::hypot(projected.external_references.back().cached_points.front()[0]-local[0],projected.external_references.back().cached_points.front()[1]-local[1])<1e-8,"Projection resolved the wrong nested occurrence");
    }
    require(live.size()==2 && live.open_assembly(top.document_id)->session.document().components.front().calculated_source.shares_with(before_snapshot),"Projection loaded dependencies or replaced the Assembly snapshot");
    auto ambiguous=edge_args;ambiguous["sketch"]=top_sketch;require(!host.execute({{"command","sketch.reference.create"},{"arguments",ambiguous}}).ok,"Assembly source without occurrence was accepted");
    run(host,"save");const auto loaded=assembly::AssemblyDocument::load(directory/"references.asmz");require(loaded.sketches.back().external_references.size()==2,"Assembly lost repeated reference occurrences");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-sketch-refs-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Original Sketch references, exact rational projection, trim/offset refresh, detach, atomicity and nested occurrences passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

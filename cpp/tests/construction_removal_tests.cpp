#include <zima/command_host/host.hpp>
#include <zima/workspace/construction_removal.hpp>
#include <zima/document/file_path.hpp>
#include <iostream>
#include <cmath>
namespace fs=std::filesystem;using namespace zima;using commands::Json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
document::ConstructionObject curve() {
    auto value=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
    for(unsigned i=0;i<3;++i) {
        auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
        point.parent_construction_id=value.id;point.origin={double(i*10),double(i*i),0};value.curve_points.push_back(point);
    }
    return value;
}
void part_removal(const fs::path& dir) {
    workspace::Workspace live;kernel::OcctKernel kernel;auto cwd=dir;
    auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={2,3,4};
    const auto point=document::PartDocument::create_construction(document::ConstructionKind::Point),route=curve();
    part.history={box};part.constructions={point,route};document::BodyHistoryGraph graph;
    const auto body=graph.create_body("Editable");graph.insert({document::PartHistoryKind::Feature,box.id});
    graph.insert({document::PartHistoryKind::Construction,point.id});graph.insert({document::PartHistoryKind::Construction,route.id});part.set_body_history(graph);
    part.resolve_constructions();const auto calculated=kernel.evaluate_history(part.kernel_operations());
    live.add_part(part,calculated,dir/"removal.prtz");live.activate(part.document_id);command_host::Host host(live,kernel,cwd);
    const auto generation=live.open_part(part.document_id)->session.data_generation();
    const auto owned=host.execute({{"command","construction.delete"},{"arguments",{{"construction",route.curve_points[1].id}}}});
    if(owned.code!="owned_construction")throw std::runtime_error("Owned Point removal expected owned_construction, got "+owned.code);
    require(live.open_part(part.document_id)->session.data_generation()==generation,"Rejected owned Point removal changed the Part");
    run(host,"construction.delete",{{"construction",point.id}});
    const auto& removed=live.open_part(part.document_id)->session;
    require(!removed.document().find_construction(point.id)&&removed.document().constructions.size()==1&&
        removed.document().body_history.find(body)->entries.size()==2&&std::abs(removed.calculated_boundaries().back().volume-24)<1e-9,
        "Construction removal lost body ownership or changed the solid volume");
    run(host,"undo");require(live.open_part(part.document_id)->session.document().find_construction(point.id),"Part Undo lost the construction");
    run(host,"redo");run(host,"save");
    const auto native=document::PartDocument::load(dir/"removal.prtz");require(!native.find_construction(point.id)&&native.find_construction(route.id),"Native Part removal did not persist");
    const auto other=run(host,"body.create",{{"name","Other"}}).data.at("body");
    require(host.execute({{"command","construction.delete"},{"arguments",{{"construction",route.id}}}}).code=="inactive_body",
        "Construction removal edited an inactive Body");
    static_cast<void>(other);
    auto embedded=document::PartDocument::create_default();auto sweep=document::PartDocument::create_sweep3d_container();embedded.history={sweep};
    live.add_part(embedded);live.activate(embedded.document_id);live.display_top_level(embedded.document_id);
    const auto embedded_generation=live.open_part(embedded.document_id)->session.data_generation();
    require(host.execute({{"command","construction.delete"},{"arguments",{{"construction",sweep.sweep3d.path.id}}}}).code=="embedded_construction"&&
        live.open_part(embedded.document_id)->session.data_generation()==embedded_generation,"Embedded Sweep path removal escaped its owning feature");
}
void assembly_removal(const fs::path& dir) {
    kernel::OcctKernel kernel;workspace::Workspace live;auto cwd=dir;
    auto doc=assembly::AssemblyDocument::create_default();const auto route=curve();doc.constructions={route};
    live.add_assembly(doc,dir/"removal.asmz");live.activate(doc.document_id);command_host::Host host(live,kernel,cwd);
    const auto remove=Json{{"command","construction.delete"},{"arguments",{{"construction",route.id}}}};
    for(unsigned scenario=0;scenario<9;++scenario) {
        auto next=doc;const document::ConstructionReference ref{"",route.curve_points.front().entity_id,"point"};
        if(scenario<2) {
            auto consumer=scenario==0?document::PartDocument::create_construction(document::ConstructionKind::Point):curve();
            if(scenario==0)consumer.references={ref};else consumer.curve_points.back().references={ref};
            next.constructions.push_back(consumer);
        }else if(scenario==2) {
            auto section=document::create_section();section.placement.references={ref};next.sections={section};
        }else if(scenario==3) {
            auto sketch=sketcher::Sketch::create_default();sketch.plane_reference_owner_id=route.container_origin.id;next.insert_sketch(sketch);
        }else if(scenario==4) {
            assembly::AssemblyCut cut;cut.definition=document::PartDocument::create_extrusion_container("owned-profile");
            cut.definition.placement.references={ref};next.cuts={cut};
        }else if(scenario==5) {
            assembly::AssemblyCut cut;cut.definition=document::PartDocument::create_extrusion_container("owned-profile");
            cut.definition.extrusion.end_condition_forward=document::EndCondition::UpTo;
            cut.definition.extrusion.end_targets_forward.emplace_back();
            auto& target=cut.definition.extrusion.end_targets_forward.back().reference;
            target.owner_id=ref.owner_id;target.semantic_key=ref.semantic_key;target.instance_path=ref.instance_path;next.cuts={cut};
        }else if(scenario==6) {
            auto component=assembly::AssemblyDocument::create_part_occurrence("Placed",document::PartDocument::create_default().document_id,"source.prtz",kernel.make_box({1,2,3}));
            assembly::ComponentPlacementReference row;row.mate_type=assembly::MateKind::PointCoincident;
            row.component_reference={assembly::MateReferenceKind::Point,assembly::InstancePath{{component.occurrence_id}},"source-point","point"};
            row.target_reference={assembly::MateReferenceKind::Point,{},ref.owner_id,ref.semantic_key};component.placement_references={row};next.components={component};
        }else {
            auto consumer=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
            auto external=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point);
            external.source_document_id=doc.document_id;external.source_owner_id=route.curve_points.front().id;external.source_semantic_key="point";
            external.broken=true;external.cached_points={{0,0}};sketch.external_references={external};
            if(scenario==7){auto section=document::create_section();section.sketch=sketch;consumer.sections={section};}
            else {
                auto feature=document::PartDocument::create_sweep3d_container();sketch.owner_container_id=feature.id;
                feature.sweep3d.profiles.emplace_back();feature.sweep3d.profiles.back().id=sketch.id;
                feature.sweep3d.profiles.back().sketch_serialized=sketch.serialized();consumer.history={feature};
            }
            live.add_part(consumer,{},dir/("consumer-"+std::to_string(scenario)+".prtz"));
        }
        live.open_assembly(doc.document_id)->session.replace(next);
        const auto generation=live.open_assembly(doc.document_id)->session.data_generation();const auto count=live.size();
        require(host.execute(remove).code=="construction_in_use","A persisted descendant, section or owned-profile reference did not protect the root");
        const auto& unchanged=live.open_assembly(doc.document_id)->session;
        require(unchanged.data_generation()==generation&&unchanged.document().constructions==next.constructions&&!unchanged.can_undo()&&live.size()==count,
            "Rejected Assembly construction removal partially changed its state");
        std::vector<std::string> parts;for(const auto& state:live.documents())if(const auto* p=std::get_if<workspace::PartState>(&state))parts.push_back(p->session.document().document_id);
        for(const auto& id:parts)require(live.remove(id),"Cannot remove owned test consumer");
    }
    // The same owner ID on a nonlocal occurrence is not this Assembly's datum.
    auto consumer=document::PartDocument::create_construction(document::ConstructionKind::Point);
    consumer.references={{"unrelated/occurrence",route.id,"point"}};auto next=doc;next.constructions.push_back(consumer);
    live.open_assembly(doc.document_id)->session.replace(next);
    run(host,"construction.delete",{{"construction",route.id}});run(host,"save");
    require(!assembly::AssemblyDocument::load(dir/"removal.asmz").find_construction(route.id),"Native Assembly still stores removed construction");
    run(host,"undo");require(live.open_assembly(doc.document_id)->session.document().find_construction(route.id),"Assembly Undo lost the removed construction");
    run(host,"redo");require(!live.open_assembly(doc.document_id)->session.document().find_construction(route.id),"Assembly Redo restored the removed construction");
}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-construction-removal-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create owned test directory");part_removal(dir);assembly_removal(dir);
    require(dir.parent_path()==parent,"Unsafe test cleanup");fs::remove_all(dir);std::cout<<"Construction removal commands and dependencies passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

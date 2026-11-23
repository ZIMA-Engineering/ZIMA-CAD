#include "profile_solid_fixture.hpp"
#include <zima/document/document_session.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>

using namespace zima;
namespace {
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected=false;
    try { action(); } catch(const std::exception&) { rejected=true; }
    require(rejected,"Invalid Render input was accepted");
}
document::BodyHistory analysis(const std::string& source) {
    document::BodyHistory value;value.name="Analysis";
    value.render=document::RenderDefinition{{source},false};return value;
}
void verify_graph() {
    document::BodyHistoryGraph graph;
    const auto first=graph.create_body("Source");
    graph.insert({document::PartHistoryKind::Feature,"feature-a"});
    const auto second=graph.create_body("Tool");
    graph.insert({document::PartHistoryKind::Feature,"feature-b"});
    const auto boolean=graph.create_boolean("Union",kernel::BodyCombination::Add,first,second);
    const auto original=graph;
    const auto id=graph.create_render(analysis(boolean));
    require(graph.find(id)->kind()==document::BodyKind::Render,"Render lost its type");
    require(graph.active_body_id()==id,"Render was not activated");
    require(graph.available_before(graph.order().size())==original.available_before(original.order().size()),
        "Render changed the available material operands");
    require(graph.visible_context()==std::vector<std::string>{boolean},"Render source is missing or duplicated");
    const auto compile=[](const document::PartHistoryEntry& entry)->std::optional<kernel::HistoryOperation> {
        kernel::HistoryOperation result;result.owner_id=entry.id;return result;
    };
    const auto before=original.compile(compile),after=graph.compile(compile);
    require(before.size()==after.size(),"Render added a material operation");
    for(std::size_t i=0;i<before.size();++i)
        require(before[i].body==after[i].body&&before[i].owner_id==after[i].owner_id,
            "Render changed a material operation");
    const auto encoded=graph.serialized();
    require(document::BodyHistoryGraph::from_serialized(encoded)==graph,"Render graph round trip changed data");
    document::BodyHistory fem;fem.name="Analysis";fem.fem=document::FemDefinition{id};
    rejects([&]{static_cast<void>(graph.create_fem(fem));});
    fem.fem->source_id=boolean;const auto fem_id=graph.create_fem(fem);
    rejects([&]{static_cast<void>(graph.create_render(analysis(fem_id)));});
    graph.erase_step(fem_id);graph.activate(id);
    const auto saved=graph;
    rejects([&]{graph.move_step(id,0);});require(graph==saved,"Failed reorder changed history");
    rejects([&]{graph.insert({document::PartHistoryKind::Feature,"unexpected"});});
    rejects([&]{static_cast<void>(graph.create_boolean("Invalid",kernel::BodyCombination::Add,boolean,id));});
    rejects([&]{static_cast<void>(graph.create_render(analysis(id)));});
    rejects([&]{static_cast<void>(graph.create_render(analysis("missing")));});
    auto scale=analysis(boolean);scale.render.reset();scale.scale=document::BodyScale{id,2,{}};
    rejects([&]{static_cast<void>(graph.create_scale(scale));});
    auto copy=scale;copy.scale.reset();copy.derived_copy=document::DerivedCopyParameters{{id},{{},"plane","origin:plane:xy"}};
    rejects([&]{static_cast<void>(graph.create_derived_copy(copy));});
    auto changed=*graph.find(id);changed.scope.placement.x=4;
    rejects([&]{graph.update_body(changed);});
    changed=*graph.find(id);changed.render.reset();rejects([&]{graph.update_body(changed);});
    changed=*graph.find(id);changed.render->source_ids.front()="feature-a";
    rejects([&]{graph.update_body(changed);});
    require(graph==saved,"Rejected Render edits changed history");
    auto hidden=*graph.find_boolean(boolean);hidden.visible=false;graph.update_boolean(hidden);
    require(graph.visible_context()==std::vector<std::string>{boolean},"Hidden source cannot be inspected in Render");
    graph.activate({});require(graph.visible_context().empty(),"Leaving Render changed source visibility");
    graph.activate(id);hidden.suppressed=true;graph.update_boolean(hidden);
    require(graph.render_source_state(id)==document::RenderSourceState::Suppressed&&graph.visible_context().empty(),
        "Suppressed Render source was exposed as available");
    graph.erase_step(boolean);
    require(graph.find(id)&&graph.find(id)->render->source_ids.front()==boolean&&
        graph.render_source_state(id)==document::RenderSourceState::Missing,"Source loss discarded or rebound Render");
    require(document::BodyHistoryGraph::from_serialized(graph.serialized())==graph,"Broken source cannot reopen");
    changed=*graph.find(id);changed.render->source_ids.front()=first;graph.update_body(changed);
    require(graph.render_source_state(id)==document::RenderSourceState::Available,"Render source repair failed");
    graph.activate({});const auto later=graph.create_body("Later");
    changed=*graph.find(id);changed.render->source_ids.front()=later;
    rejects([&]{graph.update_body(changed);});
    auto invalid=nlohmann::json::parse(encoded);invalid["bodies"].back()["kind"]="unknown";
    rejects([&]{static_cast<void>(document::BodyHistoryGraph::from_serialized(invalid.dump()));});
    // Existing material rows keep their exact schema, not an old/new adapter.
    const auto ordinary=nlohmann::json::parse(original.serialized());
    const auto with_render=nlohmann::json::parse(encoded);
    for(std::size_t i=0;i<ordinary.at("bodies").size();++i)
        require(ordinary["bodies"][i]==with_render["bodies"][i],"Render altered ordinary Body persistence");
}
void verify_controls() {
    document::RenderDefinition value{{"source"},false};
    document::RenderObject camera;camera.id="camera";camera.name="Camera";
    camera.placement.x=4;camera.placement.rotation_offset_z=20;
    value.objects.push_back(camera);value.active_camera_id=camera.id;
    for(auto kind:{document::RenderObjectKind::PointLight,document::RenderObjectKind::Sun,document::RenderObjectKind::SpotLight}) {
        auto light=camera;light.id=kind==document::RenderObjectKind::Sun?"sun":kind==document::RenderObjectKind::SpotLight?"spot":"light";
        light.cone_angle_degrees=kind==document::RenderObjectKind::SpotLight?62.5:45;
        light.edge_softness=kind==document::RenderObjectKind::SpotLight?.42:.25;
        light.kind=kind;value.objects.push_back(light);
    }
    const nlohmann::json encoded=value;
    require(encoded.get<document::RenderDefinition>()==value,"Render controls changed after reload");
    require(encoded["objects"].back()["kind"]=="spot-light"&&encoded["objects"].back()["spot"]["cone_angle_degrees"]==62.5,
        "Spotlight lost its native kind or cone");
    for(unsigned i=0;i<3;++i)require(!encoded["objects"][i].contains("spot"),"Spotlight changed another Render object's representation");
    for(const auto* key:{"cone_angle_degrees","edge_softness"}) {
        auto broken=encoded;broken["objects"].back()["spot"].erase(key);
        auto destination=value;
        rejects([&]{document::from_json(broken,destination);});require(destination==value,"Invalid spotlight partially replaced the definition");
    }
    for(double angle:{0.,-1.,180.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto bad=value;bad.objects.back().cone_angle_degrees=angle;rejects([&]{document::validate_render_definition(bad);});
    }
    for(double softness:{-0.01,1.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto bad=value;bad.objects.back().edge_softness=softness;rejects([&]{document::validate_render_definition(bad);});
    }
    for(double softness:{0.,1.}) {auto valid=value;valid.objects.back().edge_softness=softness;document::validate_render_definition(valid);}
    for(const auto bad:{nlohmann::json(-1),nlohmann::json(0),nlohmann::json(1.5),
            nlohmann::json(4294967296ULL),nlohmann::json("3")}) {
        auto invalid=encoded;invalid["settings"]["width"]=bad;
        rejects([&]{static_cast<void>(invalid.get<document::RenderDefinition>());});
    }
    auto invalid=encoded;invalid["objects"][0]["kind"]="unknown";
    rejects([&]{static_cast<void>(invalid.get<document::RenderDefinition>());});
    invalid=encoded;invalid["settings"].erase("samples");
    rejects([&]{static_cast<void>(invalid.get<document::RenderDefinition>());});
    value.objects[0].placement.references.push_back({{},"light","origin:point"});
    rejects([&]{document::validate_render_definition(value);});
    value.objects[0].placement.references.clear();value.active_camera_id="sun";
    rejects([&]{document::validate_render_definition(value);});
    value.active_camera_id="deleted-camera";document::validate_render_definition(value);
    for(const auto bad:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        value.settings.roughness=bad;rejects([&]{document::validate_render_definition(value);});
    }
}
void verify_visible_scene() {
    document::BodyHistoryGraph graph;
    const auto a=graph.create_body("A"),b=graph.create_body("B");
    const auto combined=graph.create_boolean("Union",kernel::BodyCombination::Add,a,b);
    const auto separate=graph.create_body("Separate");
    auto render=analysis(a);render.render->visible_bodies=true;render.render->source_ids.clear();
    const auto id=graph.create_render(render);
    require(graph.render_sources(id)==std::vector<std::string>{combined,separate},"Visible scene duplicated consumed operands");
    auto hidden=*graph.find(separate);hidden.visible=false;graph.update_body(hidden);
    require(graph.render_sources(id)==std::vector<std::string>{combined},"Visible scene ignored hide");
    auto selected=*graph.find(id);selected.render->visible_bodies=false;selected.render->source_ids={a,separate};graph.update_body(selected);
    require(graph.render_sources(id)==std::vector<std::string>{a,separate},"Explicit source selection followed visibility");
    require(document::BodyHistoryGraph::from_serialized(graph.serialized())==graph,"Multi-Body selection did not persist");
    selected.render->source_ids={a,a};rejects([&]{graph.update_body(selected);});
    graph.erase_step(separate);
    require(graph.render_sources(id)==std::vector<std::string>{a,separate}&&graph.render_source_state(id)==document::RenderSourceState::Missing,
        "Deleting one selected Body silently rebound the scene");
}
void verify_document() {
    auto part=document::PartDocument::create_default();
    auto feature=test::rectangular_feature(part,{20,10,5});part.history={feature};
    document::BodyHistoryGraph graph;
    const auto source=graph.create_body("Stock");graph.insert({document::PartHistoryKind::Feature,feature.id});
    graph.activate({});part.set_body_history(graph);
    kernel::OcctKernel kernel;
    auto calculated=kernel.evaluate_history(part.kernel_operations());
    require(!calculated.empty()&&calculated.back().solid_present,"Render fixture has no source solid");
    const auto source_snapshot=calculated.back().body_outputs.at(source);
    const auto original_origins=part.body_origin_reference_geometry();
    document::DocumentSession session(part,std::move(calculated));
    const auto* original_packet=&session.calculated_boundaries();
    auto next=session.document();
    auto definition=analysis(source);document::RenderObject spot;spot.id="spot";spot.name="Authored spotlight";
    spot.kind=document::RenderObjectKind::SpotLight;spot.cone_angle_degrees=71;spot.edge_softness=.62;
    spot.placement.x=45;spot.placement.rotation_y=37;definition.render->objects.push_back(spot);
    const auto render=graph.create_render(definition);next.set_body_history(graph);
    require(document::serialize_viewer_reference_geometry(next.body_origin_reference_geometry())==
        document::serialize_viewer_reference_geometry(original_origins),"Render added material Origin geometry");
    require(next.kernel_operations().size()==part.kernel_operations().size(),"Render added a CAD operation");
    session.commit_metadata(next);
    require(&session.calculated_boundaries()==original_packet,"Render metadata copied calculated CAD data");
    require(session.calculated_boundaries().back().body_outputs.at(source).shares_with(source_snapshot),
        "Render duplicated its source snapshot");
    require(session.body_context_mesh().triangles==source_snapshot->mesh.triangles,"Render view changed source connectivity");
    require(session.undo()&&session.document().body_history==part.body_history,"Render Undo failed");
    require(session.redo()&&session.document().body_history==graph,"Render Redo failed");
    require(&session.calculated_boundaries()==original_packet,"Undo/Redo copied Render source geometry");
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent/("zima-render-"+part.document_id);std::filesystem::create_directory(directory);
    const auto file=directory/"analysis.prtz";
    session.document().save(file,session.calculated_boundaries());
    std::vector<kernel::BodyResult> restored;
    auto reopened=document::PartDocument::load(file,&restored);
    require(reopened.body_history==graph&&!restored.empty(),"Native Render Part did not reopen");
    require(restored.back().body_outputs.size()==1&&!restored.back().body_outputs.contains(render),
        "Native Render storage duplicated material geometry");
    require(restored.back().source_fingerprint==session.calculated_boundaries().back().source_fingerprint,
        "Render changed the persisted CAD calculation");
    const auto recalculated=kernel.evaluate_history(reopened.kernel_operations());
    auto measured=recalculated.back();kernel.ensure_body_properties(measured);
    require(std::abs(measured.volume-1000.)<1e-7,"Render changed material volume");
    // Definition-only edits remain native data and do not evaluate a new Body.
    auto changed=*graph.find(render);changed.render->settings.samples=16;
    const auto data=std::make_shared<const document::RenderImageData>(document::RenderImageData{"aW1hZ2U=","dGh1bWI="});
    changed.render->images={{"one","First view","2026-11-23T12:00:00Z",320,200,data},
        {"two","Second view","2026-11-23T12:01:00Z",640,400,data}};
    graph.update_body(changed);reopened.set_body_history(graph);reopened.save(file,restored);
    require(document::PartDocument::load(file).body_history==graph,"Edited Render controls did not persist");
    changed.render->source_ids.front()="removed-source";graph.update_body(changed);
    reopened.set_body_history(graph);reopened.save(file,restored);
    require(document::PartDocument::load(file).body_history.render_source_state(render)==document::RenderSourceState::Missing,
        "Native reload rebound a missing Render source");
    require(std::filesystem::canonical(directory).parent_path()==parent,"Unexpected Render test directory");
    std::filesystem::remove_all(directory);
}
}
int main() {
    try { verify_graph();verify_controls();verify_visible_scene();verify_document();
        std::cout<<"Render native definition, ownership, dependencies, source sharing and persistence passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}

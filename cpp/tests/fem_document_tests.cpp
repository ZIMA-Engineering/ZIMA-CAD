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
    require(rejected,"Invalid FEM input was accepted");
}
document::BodyHistory analysis(const std::string& source) {
    document::BodyHistory value;value.name="Analysis";
    value.fem=document::FemDefinition{source};return value;
}
void verify_graph() {
    document::BodyHistoryGraph graph;
    const auto first=graph.create_body("Source");
    graph.insert({document::PartHistoryKind::Feature,"feature-a"});
    const auto second=graph.create_body("Tool");
    graph.insert({document::PartHistoryKind::Feature,"feature-b"});
    const auto boolean=graph.create_boolean("Union",kernel::BodyCombination::Add,first,second);
    const auto original=graph;
    const auto id=graph.create_fem(analysis(boolean));
    require(graph.find(id)->kind()==document::BodyKind::Fem,"FEM lost its type");
    require(graph.active_body_id()==id,"FEM was not activated");
    require(graph.available_before(graph.order().size())==original.available_before(original.order().size()),
        "FEM changed the available material operands");
    require(graph.visible_context()==std::vector<std::string>{boolean},"FEM source is missing or duplicated");
    const auto compile=[](const document::PartHistoryEntry& entry)->std::optional<kernel::HistoryOperation> {
        kernel::HistoryOperation result;result.owner_id=entry.id;return result;
    };
    const auto before=original.compile(compile),after=graph.compile(compile);
    require(before.size()==after.size(),"FEM added a material operation");
    for(std::size_t i=0;i<before.size();++i)
        require(before[i].body==after[i].body&&before[i].owner_id==after[i].owner_id,
            "FEM changed a material operation");
    const auto encoded=graph.serialized();
    require(document::BodyHistoryGraph::from_serialized(encoded)==graph,"FEM graph round trip changed data");
    const auto saved=graph;
    rejects([&]{graph.move_step(id,0);});require(graph==saved,"Failed reorder changed history");
    rejects([&]{graph.insert({document::PartHistoryKind::Feature,"unexpected"});});
    rejects([&]{static_cast<void>(graph.create_boolean("Invalid",kernel::BodyCombination::Add,boolean,id));});
    rejects([&]{static_cast<void>(graph.create_fem(analysis(id)));});
    rejects([&]{static_cast<void>(graph.create_fem(analysis("missing")));});
    auto scale=analysis(boolean);scale.fem.reset();scale.scale=document::BodyScale{id,2,{}};
    rejects([&]{static_cast<void>(graph.create_scale(scale));});
    auto copy=scale;copy.scale.reset();copy.derived_copy=document::DerivedCopyParameters{{id},{{},"plane","origin:plane:xy"}};
    rejects([&]{static_cast<void>(graph.create_derived_copy(copy));});
    auto changed=*graph.find(id);changed.scope.placement.x=4;
    rejects([&]{graph.update_body(changed);});
    changed=*graph.find(id);changed.fem.reset();rejects([&]{graph.update_body(changed);});
    changed=*graph.find(id);changed.fem->source_id="feature-a";
    rejects([&]{graph.update_body(changed);});
    require(graph==saved,"Rejected FEM edits changed history");
    auto hidden=*graph.find_boolean(boolean);hidden.visible=false;graph.update_boolean(hidden);
    require(graph.visible_context()==std::vector<std::string>{boolean},"Hidden source cannot be inspected in FEM");
    graph.activate({});require(graph.visible_context().empty(),"Leaving FEM changed source visibility");
    graph.activate(id);hidden.suppressed=true;graph.update_boolean(hidden);
    require(graph.fem_source_state(id)==document::FemSourceState::Suppressed&&graph.visible_context().empty(),
        "Suppressed FEM source was exposed as available");
    graph.erase_step(boolean);
    require(graph.find(id)&&graph.find(id)->fem->source_id==boolean&&
        graph.fem_source_state(id)==document::FemSourceState::Missing,"Source loss discarded or rebound FEM");
    require(document::BodyHistoryGraph::from_serialized(graph.serialized())==graph,"Broken source cannot reopen");
    changed=*graph.find(id);changed.fem->source_id=first;graph.update_body(changed);
    require(graph.fem_source_state(id)==document::FemSourceState::Available,"FEM source repair failed");
    graph.activate({});const auto later=graph.create_body("Later");
    changed=*graph.find(id);changed.fem->source_id=later;
    rejects([&]{graph.update_body(changed);});
    auto invalid=nlohmann::json::parse(encoded);invalid["bodies"].back()["kind"]="unknown";
    rejects([&]{static_cast<void>(document::BodyHistoryGraph::from_serialized(invalid.dump()));});
    // Existing material rows keep their exact schema, not an old/new adapter.
    const auto ordinary=nlohmann::json::parse(original.serialized());
    const auto with_fem=nlohmann::json::parse(encoded);
    for(std::size_t i=0;i<ordinary.at("bodies").size();++i)
        require(ordinary["bodies"][i]==with_fem["bodies"][i],"FEM altered ordinary Body persistence");
}
void verify_controls() {
    for(auto order:{document::FemElementOrder::Linear,document::FemElementOrder::Quadratic}) {
        document::FemDefinition value{"source"};value.mesh.order=order;
        const nlohmann::json encoded=value;
        require(encoded.get<document::FemDefinition>()==value,"FEM controls changed after reload");
        for(const auto bad:{nlohmann::json(-1),nlohmann::json(0),nlohmann::json(1.5),
                nlohmann::json(4294967296ULL),nlohmann::json("3")}) {
            auto invalid=encoded;invalid["mesh"]["elements_through_thickness"]=bad;
            rejects([&]{static_cast<void>(invalid.get<document::FemDefinition>());});
        }
        for(const auto bad:{nlohmann::json(0),nlohmann::json(3),nlohmann::json(1.5)}) {
            auto invalid=encoded;invalid["mesh"]["order"]=bad;
            rejects([&]{static_cast<void>(invalid.get<document::FemDefinition>());});
        }
        auto invalid=encoded;invalid["analysis"]="thermal";
        rejects([&]{static_cast<void>(invalid.get<document::FemDefinition>());});
        invalid=encoded;invalid["mesh"].erase("order");
        rejects([&]{static_cast<void>(invalid.get<document::FemDefinition>());});
    }
    for(const auto bad:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        document::FemDefinition value{"source"};value.mesh.minimum_size_mm=bad;
        rejects([&]{document::validate_fem_definition(value);});
        value.mesh.minimum_size_mm=.1;value.mesh.maximum_size_mm=bad;
        rejects([&]{document::validate_fem_definition(value);});
    }
    document::FemDefinition value{"source"};value.mesh.minimum_size_mm=6;
    rejects([&]{document::validate_fem_definition(value);});
}
void verify_document() {
    auto part=document::PartDocument::create_default();
    auto feature=test::rectangular_feature(part,{20,10,5});part.history={feature};
    document::BodyHistoryGraph graph;
    const auto source=graph.create_body("Stock");graph.insert({document::PartHistoryKind::Feature,feature.id});
    graph.activate({});part.set_body_history(graph);
    kernel::OcctKernel kernel;
    auto calculated=kernel.evaluate_history(part.kernel_operations());
    require(!calculated.empty()&&calculated.back().solid_present,"FEM fixture has no source solid");
    const auto source_snapshot=calculated.back().body_outputs.at(source);
    const auto original_origins=part.body_origin_reference_geometry();
    document::DocumentSession session(part,std::move(calculated));
    const auto* original_packet=&session.calculated_boundaries();
    auto next=session.document();
    const auto fem=graph.create_fem(analysis(source));next.set_body_history(graph);
    require(document::serialize_viewer_reference_geometry(next.body_origin_reference_geometry())==
        document::serialize_viewer_reference_geometry(original_origins),"FEM added material Origin geometry");
    require(next.kernel_operations().size()==part.kernel_operations().size(),"FEM added a CAD operation");
    session.commit_metadata(next);
    require(&session.calculated_boundaries()==original_packet,"FEM metadata copied calculated CAD data");
    require(session.calculated_boundaries().back().body_outputs.at(source).shares_with(source_snapshot),
        "FEM duplicated its source snapshot");
    require(session.body_context_mesh().triangles==source_snapshot->mesh.triangles,"FEM view changed source connectivity");
    require(session.undo()&&session.document().body_history==part.body_history,"FEM Undo failed");
    require(session.redo()&&session.document().body_history==graph,"FEM Redo failed");
    require(&session.calculated_boundaries()==original_packet,"Undo/Redo copied FEM source geometry");
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent/("zima-fem-"+part.document_id);std::filesystem::create_directory(directory);
    const auto file=directory/"analysis.prtz";
    session.document().save(file,session.calculated_boundaries());
    std::vector<kernel::BodyResult> restored;
    auto reopened=document::PartDocument::load(file,&restored);
    require(reopened.body_history==graph&&!restored.empty(),"Native FEM Part did not reopen");
    require(restored.back().body_outputs.size()==1&&!restored.back().body_outputs.contains(fem),
        "Native FEM storage duplicated material geometry");
    require(restored.back().source_fingerprint==session.calculated_boundaries().back().source_fingerprint,
        "FEM changed the persisted CAD calculation");
    const auto recalculated=kernel.evaluate_history(reopened.kernel_operations());
    auto measured=recalculated.back();kernel.ensure_body_properties(measured);
    require(std::abs(measured.volume-1000.)<1e-7,"FEM changed material volume");
    // Definition-only edits remain native data and do not evaluate a new Body.
    auto changed=*graph.find(fem);changed.fem->mesh.maximum_size_mm=2;
    graph.update_body(changed);reopened.set_body_history(graph);reopened.save(file,restored);
    require(document::PartDocument::load(file).body_history==graph,"Edited FEM controls did not persist");
    changed.fem->source_id="removed-source";graph.update_body(changed);
    reopened.set_body_history(graph);reopened.save(file,restored);
    require(document::PartDocument::load(file).body_history.fem_source_state(fem)==document::FemSourceState::Missing,
        "Native reload rebound a missing FEM source");
    require(std::filesystem::canonical(directory).parent_path()==parent,"Unexpected FEM test directory");
    std::filesystem::remove_all(directory);
}
}
int main() {
    try { verify_graph();verify_controls();verify_document();
        std::cout<<"FEM native definition, ownership, dependencies, source sharing and persistence passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}

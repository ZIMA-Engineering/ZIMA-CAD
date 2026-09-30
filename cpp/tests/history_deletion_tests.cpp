#include "profile_solid_fixture.hpp"
#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <iostream>
using namespace zima;
static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    try {
        kernel::OcctKernel kernel;
        auto doc=document::PartDocument::create_default();
        auto source=test::rectangular_feature(doc,{10,10,10});
        doc.history={source};
        const auto geometry=workspace::calculate_part(kernel,doc);
        require(!geometry.empty()&&!geometry.back().mesh.edges.empty(),"Fixture has no edges");
        const auto reference=geometry.back().mesh.edges.front().reference;
        auto fillet=document::PartDocument::create_fillet_container({reference});
        fillet.edge_treatment.primary_size=.5;
        doc.history.push_back(fillet);
        const auto rounded=workspace::calculate_part(kernel,doc);
        require(rounded.back().calculation_errors.empty(),"Fillet fixture failed calculation");
        auto chamfer=document::PartDocument::create_chamfer_container({reference});
        chamfer.edge_treatment.primary_size=.1;
        doc.history.push_back(chamfer);
        bool valid_chamfer=false;
        for(const auto& edge:rounded.back().mesh.edges)if(edge.reference.owner_id==fillet.id) {
            chamfer.edge_treatment.routes={{edge.reference}};doc.history.back()=chamfer;
            if(workspace::calculate_part(kernel,doc).back().calculation_errors.empty()){valid_chamfer=true;break;}
        }
        require(valid_chamfer,"Fillet fixture has no usable dependent Chamfer edge");
        document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Body"));
        for(const auto& f:doc.history)graph.insert({document::PartHistoryKind::Feature,f.id});
        doc.set_body_history(graph);
        require(workspace::calculate_part(kernel,doc).back().calculation_errors.empty(),"Dependent Chamfer fixture failed calculation");
        const auto original=doc.serialized();
        const auto plan=workspace::plan_history_deletion(doc,source.id);
        require(plan.affected==std::set<std::string>{fillet.id,chamfer.id},"Dependency closure lost descendants");
        require(doc.serialized()==original,"Preview mutated document");
        workspace::Workspace live;const auto id=doc.document_id;
        live.add_part(doc,workspace::calculate_part(kernel,doc));
        const auto revision=live.open_part(id)->session.revision();
        workspace::delete_part_history(live,id,kernel,source.id);
        auto& session=live.open_part(id)->session;
        require(session.revision()==revision+1,"Deletion did not commit exactly once");
        const auto& deleted=session.document();
        require(!deleted.find_container(source.id)&&deleted.find_container(fillet.id)&&deleted.find_container(chamfer.id),"Deletion removed dependent history");
        require(deleted.find_container(fillet.id)->edge_treatment.flattened_edges().empty(),"Deleted source edge retained");
        require(deleted.find_container(chamfer.id)->edge_treatment.flattened_edges().empty(),"Invalid dependent edge retained");
        require(deleted.removed_reference_states.size()==2,"Missing-reference states disappeared");
        auto loaded=document::PartDocument::from_serialized(deleted.serialized());
        require(loaded.removed_reference_states==deleted.removed_reference_states,"Incomplete state did not round trip");
        auto regenerated=workspace::calculate_part_with_resolved_references(kernel,loaded);
        require(loaded.removed_reference_states.size()==2,"Regeneration acknowledged a missing reference");
        require(regenerated.back().calculation_errors.contains(fillet.id),"Missing reference calculated geometry");
        require(workspace::step_part_document_history(live,id,false),"Undo unavailable");
        require(session.document().serialized()==original,"Undo did not restore all references");
        require(workspace::step_part_document_history(live,id,true),"Redo unavailable");
        auto numeric=session.document();numeric.find_container(fillet.id)->edge_treatment.primary_size=1;
        require(!workspace::refresh_removed_reference_states(numeric),"Numeric edit acknowledged missing references");
        auto repaired=session.document();repaired.find_container(fillet.id)->edge_treatment.routes={{reference}};
        require(workspace::refresh_removed_reference_states(repaired)&&!repaired.removed_reference_states.contains(fillet.id),"Replacement references did not acknowledge repair");
        auto repair_doc=session.document();
        auto replacement=test::rectangular_feature(repair_doc,{12,12,12});
        auto independent=test::rectangular_feature(repair_doc,{3,3,3});
        repair_doc.history.insert(repair_doc.history.begin(),replacement);repair_doc.history.push_back(independent);
        document::BodyHistoryGraph repair_graph;const auto repair_body=repair_graph.create_body("Repair");
        for(const auto& owner:{replacement.id,fillet.id,chamfer.id})repair_graph.insert({document::PartHistoryKind::Feature,owner});
        const auto independent_body=repair_graph.create_body("Independent");
        repair_graph.insert({document::PartHistoryKind::Feature,independent.id});repair_graph.activate(repair_body);
        repair_doc.set_body_history(repair_graph);
        auto repair_geometry=workspace::calculate_part_with_resolved_references(kernel,repair_doc);
        require(repair_geometry.back().body_outputs.at(independent_body)->calculation_errors.empty(),"Broken references blocked an independent Body");
        auto edited=*repair_doc.find_container(fillet.id);
        edited.edge_treatment.routes={{repair_geometry.front().mesh.edges.front().reference}};
        workspace::commit_part_document(live,id,repair_doc,repair_geometry);
        require(workspace::commit_edge_treatment(live,kernel,id,edited,workspace::EdgeTreatmentEditMode::Replace),"Reference repair did not commit");
        require(!session.document().removed_reference_states.contains(fillet.id)&&session.document().removed_reference_states.contains(chamfer.id),"Repair acknowledged other broken features");
        require(!session.calculated_boundaries().back().calculation_errors.contains(fillet.id),"Repaired fillet failed calculation");
        require(workspace::step_part_document_history(live,id,false)&&session.document().removed_reference_states.contains(fillet.id),"Repair Undo lost incomplete state");
        auto pattern_doc=document::PartDocument::create_default();
        auto pattern_source=test::rectangular_feature(pattern_doc,{4,4,4});
        auto pattern=document::PartDocument::create_fillet_container({reference});
        pattern.feature_kind=document::FeatureKind::DerivedCopy;pattern.name="Pattern";
        pattern.derived_copy.source_id=pattern_source.id;pattern.derived_copy.pattern=kernel::PatternRequest{};
        pattern_doc.history={pattern_source,pattern};
        document::BodyHistoryGraph pattern_graph;static_cast<void>(pattern_graph.create_body("Pattern"));
        for(const auto& f:pattern_doc.history)pattern_graph.insert({document::PartHistoryKind::Feature,f.id});
        pattern_doc.set_body_history(pattern_graph);
        workspace::Workspace pattern_live;const auto pattern_id=pattern_doc.document_id;
        pattern_live.add_part(pattern_doc,workspace::calculate_part(kernel,pattern_doc));
        workspace::delete_part_history(pattern_live,pattern_id,kernel,pattern_source.id);
        const auto pattern_roundtrip=document::PartDocument::from_serialized(pattern_live.open_part(pattern_id)->session.document().serialized());
        require(pattern_roundtrip.find_container(pattern.id)&&pattern_roundtrip.find_container(pattern.id)->derived_copy.source_id.empty()&&
            pattern_roundtrip.removed_reference_states.contains(pattern.id),"Pattern without source could not remain in native history");
        std::cout<<"History deletion contracts passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

#include "profile_solid_fixture.hpp"
#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/document/bend.hpp>
#include <iostream>
using namespace zima;
static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
static void verify_sheet_suppression(const kernel::OcctKernel& kernel) {
    auto doc=document::PartDocument::create_default();
    auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=bend.id;
    static_cast<void>(sketch.add_segment(0,0,40,0));bend.bend.sketch_id=sketch.id;
    bend.bend.thickness_override=true;bend.bend.thickness=2;
    bend.bend.radius_follows_thickness=false;bend.bend.radius=8;bend.bend.angle_degrees=77;
    auto unbend=document::PartDocument::create_sketch_container();unbend.feature_kind=document::FeatureKind::Unbend;unbend.sheet_state.all=true;
    auto formed=document::PartDocument::create_sketch_container();formed.feature_kind=document::FeatureKind::BendBack;formed.sheet_state.all=true;
    doc.history={bend,unbend,formed};doc.sketches={sketch};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Sheet"));
    for(const auto& feature:doc.history)graph.insert({document::PartHistoryKind::Feature,feature.id});
    doc.set_body_history(graph);doc.resolve_constructions();const auto original=doc.serialized();
    workspace::Workspace live;const auto id=doc.document_id;
    live.add_part(doc,workspace::calculate_part(kernel,doc));
    require(workspace::set_part_history_suppressed(live,id,kernel,unbend.id,true),"Unbend suppression did not change history");
    auto& session=live.open_part(id)->session;
    require(!session.document().find_container(bend.id)->suppressed&&session.document().find_container(unbend.id)->suppressed&&
        session.document().find_container(formed.id)->suppressed,"Unbend suppression omitted Bend Back or changed its source");
    require(workspace::set_part_history_suppressed(live,id,kernel,formed.id,false)&&session.document().serialized()==original,
        "Bend Back restoration did not restore preceding Unbend");
    require(workspace::set_part_history_suppressed(live,id,kernel,bend.id,true),"Sheet source suppression did not change history");
    for(const auto& feature:doc.history)require(session.document().find_container(feature.id)->suppressed,"Sheet source suppression omitted a state");
    require(workspace::set_part_history_suppressed(live,id,kernel,formed.id,false)&&session.document().serialized()==original&&
        session.calculated_boundaries().back().calculation_errors.empty(),"Bend Back restoration omitted its original sheet source");
}
int main() {
    try {
        kernel::OcctKernel kernel;
        verify_sheet_suppression(kernel);
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
        {
            auto suppression_doc=doc;
            auto independent=test::rectangular_feature(suppression_doc,{3,3,3});
            suppression_doc.history.push_back(independent);
            auto suppression_graph=suppression_doc.body_history;
            const auto source_body=suppression_graph.active_body_id();
            static_cast<void>(suppression_graph.create_body("Independent"));
            suppression_graph.insert({document::PartHistoryKind::Feature,independent.id});
            suppression_graph.activate(source_body);suppression_doc.set_body_history(suppression_graph);
            const auto unchanged=suppression_doc.serialized();
            workspace::Workspace suppression_live;
            const auto suppression_id=suppression_doc.document_id;
            suppression_live.add_part(suppression_doc,workspace::calculate_part(kernel,suppression_doc));
            auto& session=suppression_live.open_part(suppression_id)->session;
            const auto revision=session.revision();
            require(workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,source.id,true),"Source suppression did not change history");
            require(session.revision()==revision+1&&session.document().find_container(source.id)->suppressed&&
                session.document().find_container(fillet.id)->suppressed&&session.document().find_container(chamfer.id)->suppressed&&
                !session.document().find_container(independent.id)->suppressed,"Suppression lost transitive consumers or changed independent history");
            require(session.calculated_boundaries().back().calculation_errors.empty()&&session.document().removed_reference_states.empty(),
                "Suppression reported deleted/missing references");
            auto expected=suppression_doc;
            for(const auto& id:{source.id,fillet.id,chamfer.id})expected.find_container(id)->suppressed=true;
            require(session.document().serialized()==expected.serialized(),"Suppression rewrote authored definitions or references");
            const auto generation=session.data_generation();
            require(!workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,source.id,true)&&
                session.revision()==revision+1&&session.data_generation()==generation,"Repeated suppression recalculated or committed");
            std::vector<kernel::BodyResult> saved;
            const auto reopened=document::PartDocument::from_serialized(session.document().serialized(session.calculated_boundaries()),&saved);
            require(reopened.serialized()==expected.serialized()&&!saved.empty(),"Native reopen lost cascaded suppression");
            require(workspace::step_part_document_history(suppression_live,suppression_id,false)&&session.document().serialized()==unchanged,
                "Suppression Undo did not restore source and all consumers together");
            require(workspace::step_part_document_history(suppression_live,suppression_id,true)&&session.document().serialized()==expected.serialized(),
                "Suppression Redo did not restore all flags together");
            const auto restore_revision=session.revision();
            require(workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,fillet.id,false),"Dependent restoration did not change history");
            auto partially_restored=expected;
            partially_restored.find_container(source.id)->suppressed=false;
            partially_restored.find_container(fillet.id)->suppressed=false;
            require(session.revision()==restore_revision+1&&session.document().serialized()==partially_restored.serialized()&&
                session.calculated_boundaries().back().calculation_errors.empty(),"Restoration omitted prerequisite sources or enabled downstream history");
            require(workspace::step_part_document_history(suppression_live,suppression_id,false)&&session.document().serialized()==expected.serialized(),
                "Restoration Undo did not return all flags together");
            require(workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,source.id,false)&&
                !session.document().find_container(source.id)->suppressed&&session.document().find_container(fillet.id)->suppressed&&
                session.document().find_container(chamfer.id)->suppressed,"Source restoration enabled its downstream consumers");
            require(workspace::set_part_history_suppressed(suppression_live,suppression_id,kernel,chamfer.id,false)&&
                session.document().serialized()==unchanged&&session.calculated_boundaries().back().calculation_errors.empty(),
                "Restoring a final dependent did not enable its transitive prerequisites");
        }
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

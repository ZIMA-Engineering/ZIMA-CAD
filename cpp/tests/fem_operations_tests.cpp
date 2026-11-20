#include "profile_solid_fixture.hpp"
#include <zima/workspace/fem_operations.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <iostream>

using namespace zima;
namespace {
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected=false;try {action();}catch(const std::exception&) {rejected=true;}
    require(rejected,"Invalid FEM transaction accepted");
}
void verify_boolean_and_consumed_source() {
    auto part=document::PartDocument::create_default();
    auto first=test::rectangular_feature(part,{20,10,5});
    auto second=test::rectangular_feature(part,{10,6,3});part.history={first,second};
    document::BodyHistoryGraph graph;const auto a=graph.create_body("A");
    graph.insert({document::PartHistoryKind::Feature,first.id});const auto b=graph.create_body("B");
    graph.insert({document::PartHistoryKind::Feature,second.id});
    const auto joined=graph.create_boolean("Union",kernel::BodyCombination::Add,a,b);part.set_body_history(graph);
    kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto source=calculated.back().body_outputs.at(a),result=calculated.back().body_outputs.at(joined);
    workspace::Workspace live;live.add_part(part,std::move(calculated));auto& session=live.open_part(part.document_id)->session;
    for(const auto& id:{a,joined}) {
        const auto edit=workspace::prepare_fem_edit(session.document());
        require(workspace::fem_source_candidates(session,edit)==std::vector<std::string>{a,b,joined},
            "FEM did not offer a consumed Body and its Boolean result");
        document::BodyHistory value;value.scope.id=edit.object_id;value.name="Analysis";value.fem=document::FemDefinition{id};
        const auto count=kernel::body_calculation_count.load();
        require(workspace::commit_fem(live,edit,value),"FEM source selection failed");
        require(workspace::read_fem_source(session,value.scope.id).shares_with(id==a?source:result),
            "FEM resolved another material history boundary");
        require(kernel::body_calculation_count.load()==count,"Selecting a Boolean or consumed Body recalculated CAD");
    }
    auto measured=session.calculated_boundaries().back();kernel.ensure_body_properties(measured);
    require(std::abs(measured.volume-1000.)<1e-7,"Analyses duplicated Boolean material volume");
}
}
int main() {
    try {
        verify_boolean_and_consumed_source();
        auto part=document::PartDocument::create_default();
        auto feature=test::rectangular_feature(part,{20,10,5});part.history={feature};
        document::BodyHistoryGraph graph;const auto source=graph.create_body("Stock");
        graph.insert({document::PartHistoryKind::Feature,feature.id});graph.activate({});
        const auto empty=graph.create_body("Empty");graph.activate({});part.set_body_history(graph);
        kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
        const auto original=calculated.back().body_outputs.at(source);
        workspace::Workspace live;live.add_part(part,std::move(calculated));
        auto& session=live.open_part(part.document_id)->session;
        const auto* shared=&session.calculated_boundaries();
        const auto before=session.revision();const auto generation=session.data_generation();
        const auto calculations=kernel::body_calculation_count.load();
        auto edit=workspace::prepare_fem_edit(session.document());
        require(workspace::fem_source_candidates(session,edit)==std::vector<std::string>{source},
            "FEM offered an empty or invalid material source");
        require(session.revision()==before&&session.data_generation()==generation&&!session.can_undo(),
            "Opening/cancelling FEM draft changed the document");
        document::BodyHistory value;value.scope.id=edit.object_id;value.name="Static analysis";
        value.fem=document::FemDefinition{empty};
        rejects([&]{static_cast<void>(workspace::commit_fem(live,edit,value));});
        require(session.revision()==before,"Failed FEM creation committed history");
        value.fem->source_id=source;
        require(workspace::commit_fem(live,edit,value),"FEM creation did not commit");
        const auto fem=value.scope.id;
        require(workspace::read_fem_source(session,fem).shares_with(original),"FEM source was copied");
        require(&session.calculated_boundaries()==shared,"FEM creation replaced calculated CAD");
        const auto revision=session.revision();const auto new_generation=session.data_generation();
        edit=workspace::prepare_fem_edit(session.document(),fem);
        require(!workspace::commit_fem(live,edit,*session.document().body_history.find(fem)),"Unchanged FEM OK committed");
        require(session.revision()==revision&&session.data_generation()==new_generation,"Unchanged FEM changed generation");
        value=*session.document().body_history.find(fem);value.fem->mesh.maximum_size_mm=1;
        require(workspace::commit_fem(live,edit,value),"FEM controls did not commit");
        rejects([&]{static_cast<void>(workspace::commit_fem(live,edit,value));});
        require(session.undo()&&session.document().body_history.find(fem)->fem->mesh.maximum_size_mm==5,
            "FEM controls Undo failed");
        require(session.redo()&&session.document().body_history.find(fem)->fem->mesh.maximum_size_mm==1,
            "FEM controls Redo failed");
        require(&session.calculated_boundaries()==shared&&workspace::read_fem_source(session,fem).shares_with(original),
            "FEM editing/Undo/Redo replaced material data");
        require(kernel::body_calculation_count.load()==calculations,"FEM metadata evaluated CAD geometry");
        auto next=session.document();auto suppressed=next.body_history;auto source_body=*suppressed.find(source);
        source_body.suppressed=true;suppressed.update_body(source_body);next.set_body_history(suppressed);
        session.commit_metadata(std::move(next));
        rejects([&]{static_cast<void>(workspace::read_fem_source(session,fem));});
        require(session.undo(),"Source suppression Undo failed");
        require(workspace::read_fem_source(session,fem).shares_with(original),"Source did not return after Undo");
        auto stale=session.calculated_boundaries();stale.back().body_outputs.erase(source);
        session.update_calculated_boundaries(std::move(stale));
        rejects([&]{static_cast<void>(workspace::read_fem_source(session,fem));});
        std::cout<<"FEM draft, source selection, shared snapshot, metadata commit, stale draft and Undo/Redo passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

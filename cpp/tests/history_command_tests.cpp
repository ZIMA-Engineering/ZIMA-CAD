#include <zima/command_host/host.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/reference_index.hpp>
#include <cmath>
#include <algorithm>
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
void volume(const workspace::PartState& state,double expected) {
    const auto& cache=state.session.calculated_boundaries();
    require(!cache.empty() && std::abs(cache.back().volume-expected)<1e-6,"Incorrect history volume");
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;command_host::Interaction interaction;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    options.interaction=[&]{return interaction;};command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","history"}});const auto doc=live.active_document_id();auto* state=live.open_part(doc);
    const auto body=state->session.document().body_history.active_body_id();
    const auto first=run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}}).data.at("container").get<std::string>();
    const auto second=run(host,"box.create",{{"length_mm","4"},{"width_mm","4"},{"height_mm","4"}}).data.at("container").get<std::string>();
    volume(*state,1000);const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    const auto listed=run(host,"history.list").data;
    require(listed.at("items").size()==2 && listed.at("items")[0].at("body")==body,"History query lost order/ownership");
    require(run(host,"history.can_move",{{"object",second},{"before",first}}).data.at("would_change")==true,"Independent move rejected");
    require(run(host,"history.move",{{"object",second}}).data.at("changed")==false,"No-op move committed");
    require(run(host,"history.suppress",{{"object",first},{"suppressed",false}}).data.at("changed")==false,"No-op suppression committed");
    require(state->session.revision()==revision && cache==state->session.calculated_boundaries().data(),"Query/no-op replaced calculated history");
    workspace::ReferenceIndex refs;refs.add_geometry(state->session.calculated_boundaries().back().mesh.original_references);
    run(host,"history.move",{{"object",second},{"before",first}});volume(*state,1000);
    workspace::ReferenceIndex moved;moved.add_geometry(state->session.calculated_boundaries().back().mesh.original_references);
    require(refs.keys==moved.keys && state->session.document().history_order.front().id==second,"Move changed original identities or ignored order");
    run(host,"undo");require(state->session.document().history_order.front().id==first,"History move Undo failed");run(host,"redo");
    run(host,"history.suppress",{{"object",first},{"suppressed",true}});volume(*state,64);
    run(host,"undo");volume(*state,1000);run(host,"redo");volume(*state,64);
    run(host,"history.suppress",{{"object",first},{"suppressed",false}});volume(*state,1000);
    const auto valid_revision=state->session.revision();
    for(const Json& request:std::vector<Json>{
        {{"command","history.delete"},{"arguments",{{"object","missing"}}}},
        {{"command","history.move"},{"arguments",{{"object",first},{"before","missing"}}}},
        {{"command","history.suppress"},{"arguments",{{"object",first},{"suppressed","true"}}}},
        {{"command","history.cursor"},{"arguments",{{"index",-1}}}},
        {{"command","history.cursor"},{"arguments",{{"index",99}}}}}) {
        require(!host.execute(request).ok && state->session.revision()==valid_revision,"Rejected history command partly committed");
    }
    run(host,"history.cursor",{{"index",0}});volume(*state,1000);
    const auto inserted=run(host,"box.create",{{"length_mm","2"},{"width_mm","2"},{"height_mm","2"}}).data.at("container").get<std::string>();
    require(state->session.document().history_order.front().id==inserted && state->session.document().body_history.owner(inserted)->scope.id==body,"Cursor insertion lost history scope");
    run(host,"history.delete",{{"object",inserted}});require(!state->session.document().find_container(inserted),"Delete retained feature");
    run(host,"undo");require(state->session.document().find_container(inserted),"Delete Undo lost feature");run(host,"redo");
    // A persisted geometric dependency must block both the query and mutation.
    auto dependent=state->session.document();const auto* source=dependent.find_container(second);
    document::ConstructionReference ref;ref.owner_id=source->feature_id;ref.semantic_key="z_max";
    dependent.find_container(first)->placement.references.push_back(ref);
    state->session.commit(std::move(dependent),state->session.calculated_boundaries());
    const auto dependency_revision=state->session.revision();const auto* dependency_cache=state->session.calculated_boundaries().data();
    require(host.execute_text("history.can_move "+first+" "+second).code=="history_dependency","Dependency query allowed consumer before source");
    require(host.execute_text("history.move "+first+" "+second).code=="history_dependency","Dependency mutation allowed consumer before source");
    require(state->session.revision()==dependency_revision && dependency_cache==state->session.calculated_boundaries().data(),"Dependency failure changed model/cache");run(host,"undo");
    const auto other=run(host,"body.create",{{"name","Other"}}).data.at("body").get<std::string>();
    require(host.execute_text("history.delete "+first).code=="inactive_body","Deleted history in inactive Body");
    require(host.execute_text("history.suppress "+first+" true").code=="inactive_body","Suppressed history in inactive Body");
    run(host,"box.create",{{"length_mm","3"},{"width_mm","3"},{"height_mm","3"}});
    run(host,"history.move",{{"object",other},{"before",body}});
    require(state->session.document().body_history.order().front()==other,"Body step move failed");
    run(host,"history.delete",{{"object",other}});require(!state->session.document().body_history.find(other),"Body deletion failed");run(host,"undo");
    const auto boolean=run(host,"body.boolean.create",{{"operation","subtract"},{"target",body},{"tool",other}}).data.at("boolean").get<std::string>();
    const auto bool_revision=state->session.revision();
    require(!host.execute_text("history.move "+boolean+" "+body).ok && !host.execute_text("history.delete "+body).ok && state->session.revision()==bool_revision,"Boolean inputs/order were corrupted");
    run(host,"history.delete",{{"object",boolean}});require(state->session.document().body_history.booleans().empty(),"Boolean delete failed");run(host,"undo");
    interaction.editing=true;require(host.execute_text("history.delete "+boolean).code=="editing_in_progress","History bypassed open edit guard");run(host,"history.list");interaction.editing=false;
    run(host,"save");std::vector<kernel::BodyResult> reloaded;const auto loaded=document::PartDocument::load(directory/"history.prtz",&reloaded);
    require(loaded.body_history==state->session.document().body_history && loaded.history_order==state->session.document().history_order && !reloaded.empty(),"History persistence changed graph or dropped calculation");
    // Two independent edge treatments: deleting the earlier one must retain
    // the surviving source edge used by the later treatment.
    run(host,"new",{{"type","part"},{"name","edges"}});auto* edges=live.open_part(live.active_document_id());
    run(host,"box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}});
    const auto vertical=[&](double x,double y) {
        for(const auto& edge:edges->session.calculated_boundaries().back().mesh.edges)
            if(edge.reference.valid() && edge.points.size()>=2 && std::ranges::all_of(edge.points,[&](const auto& p){return std::abs(p.x-x)<1e-7 && std::abs(p.y-y)<1e-7;}))return edge.reference;
        throw std::runtime_error("Missing persisted vertical edge at "+std::to_string(x)+","+std::to_string(y));
    };
    const auto append_treatment=[&](kernel::EdgeReference edge) {
        auto next=edges->session.document();auto chamfer=document::PartDocument::create_chamfer_container({edge});chamfer.edge_treatment.primary_size=1;
        const auto id=chamfer.id;next.insert_history_entry(document::PartHistoryKind::Feature,id);next.history.push_back(std::move(chamfer));
        auto calculated=workspace::calculate_part_with_resolved_references(kernel,next);
        require(!calculated.empty() && calculated.back().calculation_errors.empty(),"Invalid edge treatment fixture");
        edges->session.commit(std::move(next),std::move(calculated));return id;
    };
    const auto treatment_a=append_treatment(vertical(-5,-5));const auto treatment_b=append_treatment(vertical(5,5));volume(*edges,990);
    run(host,"history.delete",{{"object",treatment_a}});volume(*edges,995);
    require(edges->session.document().find_container(treatment_b) && edges->session.calculated_boundaries().back().calculation_errors.empty(),"Delete broke surviving downstream edge treatment");
    run(host,"undo");volume(*edges,990);run(host,"redo");volume(*edges,995);
    run(host,"undo");
    const auto generated_edge=vertical(-4,-5);require(generated_edge.owner_id==treatment_a,"Dependent fixture did not use generated topology");
    const auto dependent_treatment=append_treatment(generated_edge);
    const auto before_delete=edges->session.revision();
    const auto deletion=host.execute({{"command","history.delete"},{"arguments",{{"object",treatment_a}}}});
    require(!deletion.ok && deletion.code=="calculation_errors" && deletion.data.at("changed")==true &&
        deletion.data.at("calculation_errors").contains(dependent_treatment) && host.change() &&
        edges->session.revision()==before_delete+1 && !edges->session.document().find_container(treatment_a),
        "Deletion of generated source topology did not report its committed recovery state");
    run(host,"undo");require(edges->session.document().find_container(treatment_a) && edges->session.calculated_boundaries().back().calculation_errors.empty(),"Recovery Undo lost the source feature");
    run(host,"new",{{"type","assembly"},{"name","other"}});
    require(run(host,"history.list",{{"document",doc}}).data.at("document")==doc && live.active_document_id()!=doc,"History query activated inactive document");
    require(host.execute_text("history.cursor 0").code=="unsupported_document","Part history mutated Assembly");
}
}
int main() {
    try {
        kernel::OcctKernel kernel;const auto directory=fs::canonical(fs::temp_directory_path())/("zima-history-commands-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);verify(kernel,directory);
        require(directory.parent_path()==fs::canonical(fs::temp_directory_path()),"Unsafe test cleanup");fs::remove_all(directory);
        std::cout<<"History order, suppression, deletion, cursors, volumes, references, atomicity and persistence passed without Qt\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

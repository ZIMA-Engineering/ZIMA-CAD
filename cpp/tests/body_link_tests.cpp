#include "profile_solid_fixture.hpp"
#include <zima/workspace/body_link_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <iostream>

using namespace zima;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void near(double value,double expected){require(std::abs(value-expected)<1e-7,"Incorrect linked Body volume");}
template<class F>static void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Invalid link accepted");}
int main(){try{
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto source=document::PartDocument::create_default();auto feature=test::rectangular_feature(source,{10,10,10});source.history={feature};
    document::BodyHistoryGraph graph;const auto source_body=graph.create_body("Source");graph.insert({document::PartHistoryKind::Feature,feature.id});graph.activate({});source.set_body_history(graph);
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent/("zima-link-"+source.document_id);std::filesystem::create_directory(directory);
    const auto source_file=directory/"source.prtz",target_file=directory/"target.prtz";
    auto calculated=workspace::calculate_part_with_resolved_references(kernel,source);source.save(source_file,calculated);
    const auto source_id=source.document_id;live.add_part(source,calculated,source_file);
    auto target=document::PartDocument::create_default();const auto target_id=target.document_id;
    auto target_graph=target.body_history;target_graph.activate({});target.set_body_history(target_graph);live.add_part(target,{},target_file);
    auto edit=workspace::prepare_body_link_edit(target);
    document::BodyHistory link;link.scope.id=edit.object_id;link.name="Linked";link.scope.placement=document::body_origin_attachment(target_id);
    link.link=workspace::body_link_from_source(workspace::read_body_link_source(live,source_file),source_body,link.scope.id);
    require(workspace::commit_body_link(live,kernel,edit,link),"Link was not committed");
    const auto volume=[&]{return live.open_part(target_id)->session.calculated_boundaries().back().volume;};near(volume(),1000);
    auto& session=live.open_part(target_id)->session;
    link=*session.document().body_history.find(link.scope.id);
    require(session.document().body_history.find(link.scope.id)->entries.empty(),"Link copied source history");
    require(session.document().body_history.find(link.scope.id)->scope.placement.references.size()==5,"Link is not attached to Part Origin");
    rejects([&]{auto copy=session.document().body_history;copy.activate(link.scope.id);});
    for(const auto& ref:session.calculated_boundaries().back().mesh.original_references.triangle_references)
        require(ref.owner_id==link.scope.id&&ref.semantic_key.starts_with("link:from:"),"Link did not persist its own ancestry");
    edit=workspace::prepare_body_link_edit(session.document(),link.scope.id);
    const auto generation=session.data_generation();
    require(!workspace::commit_body_link(live,kernel,edit,link)&&session.data_generation()==generation,"Unchanged link created a transaction");
    link.scope.placement.references[2].offset=5;
    require(workspace::commit_body_link(live,kernel,edit,link),"Link placement did not commit");
    near(session.document().body_history.find(link.scope.id)->scope.translation().x,5);
    require(session.calculated_boundaries().back().volume_integrals.has_value(),"Link lost exact mass properties");
    near(session.calculated_boundaries().back().volume_integrals->centroid.x,5);
    link=*session.document().body_history.find(link.scope.id);
    auto* source_state=live.open_part(source_id);auto changed=source_state->session.document();
    changed.find_container(feature.id)->extrusion.length_forward=10;
    calculated=workspace::calculate_part_with_resolved_references(kernel,changed,&source_state->session.calculated_boundaries());
    source_state->session.commit(std::move(changed),std::move(calculated));
    live.activate(source_id);live.activate(target_id);live.refresh_source_geometry();near(volume(),1000);
    static_cast<void>(workspace::regenerate_part(live,kernel,target_id));near(volume(),2000);
    session.undo();near(volume(),1000);session.redo();near(volume(),2000);
    session.document().save(target_file,session.calculated_boundaries());
    std::vector<kernel::BodyResult> restored;auto reopened=document::PartDocument::load(target_file,&restored);near(restored.back().volume,2000);
    require(reopened.body_history==session.document().body_history,"Link changed on reopen");
    const auto copies=live.save_copy(target_id,directory/"copy.prtz");
    require(!copies.empty(),"Save Copy did not write a link document");
    const auto copied=document::PartDocument::load(copies.front(),&restored);near(restored.back().volume,2000);
    require(copied.document_id!=target_id&&copied.body_history.find(link.scope.id)->link->document_id==source_id,"Save Copy rebound the source identity");
    {
        auto cut=reopened;auto stock=test::rectangular_feature(cut,{20,20,20});cut.history.push_back(stock);
        auto bodies=cut.body_history;bodies.set_insertion_cursor(0);const auto stock_id=bodies.create_body("Stock");
        bodies.insert({document::PartHistoryKind::Feature,stock.id});bodies.activate({});bodies.set_insertion_cursor(bodies.order().size());
        static_cast<void>(bodies.create_boolean("Cavity",kernel::BodyCombination::Subtract,stock_id,link.scope.id));cut.set_body_history(bodies);
        const auto cavity=workspace::calculate_part_with_resolved_references(kernel,cut);near(cavity.back().volume,6000);
    }
    auto reverse=workspace::prepare_body_link_edit(live.open_part(source_id)->session.document());
    auto cyclic=link;cyclic.scope.id=reverse.object_id;
    cyclic.link=workspace::body_link_from_source(workspace::read_body_link_source(live,target_file),link.scope.id,cyclic.scope.id);
    rejects([&]{static_cast<void>(workspace::commit_body_link(live,kernel,reverse,cyclic));});
    const auto renamed=directory/"renamed.prtz";const std::array relocations{document::FileRelocation{source_id,source_file,renamed}};
    session.rebase_native_files(relocations);
    require(session.document().body_history.find(link.scope.id)->link->source_path==renamed,"Source rename lost link");
    session.undo();require(session.document().body_history.find(link.scope.id)->link->source_path==renamed,"Undo restored stale source path");session.redo();
    static_cast<void>(live.remove(source_id));std::filesystem::remove(source_file);
    reopened=document::PartDocument::load(target_file,&restored);near(restored.back().volume,2000);
    const auto& remaining=live.open_part(target_id)->session;
    const auto before=remaining.document().serialized(remaining.calculated_boundaries());
    rejects([&]{static_cast<void>(workspace::regenerate_part(live,kernel,target_id));});
    require(remaining.document().serialized(remaining.calculated_boundaries())==before,"Failed refresh changed target");
    document::FamilyTable table;table.columns={"Linked"};table.bindings["Linked"]={"body",link.scope.id,{}};
    table.instances={{"Absent",{{"Linked","no"}}},{"Present",{{"Linked","yes"}}}};
    static_cast<void>(workspace::set_family_table(live,target_id,table));
    const auto absent=workspace::open_family_instance(live,kernel,target_id,"Absent",false);
    const auto present=workspace::open_family_instance(live,kernel,target_id,"Present",false);
    near(live.open_part(absent)->session.calculated_boundaries().back().volume,0);
    near(live.open_part(present)->session.calculated_boundaries().back().volume,2000);
    require(std::filesystem::canonical(directory).parent_path()==parent,"Unexpected test directory");std::filesystem::remove_all(directory);
    std::cout<<"Linked Body geometry, Origin, ancestry, live refresh, no-op, persistence, cycles and Undo/Redo passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

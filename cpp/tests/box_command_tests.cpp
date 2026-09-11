#include <zima/command_host/host.hpp>
#include <zima/workspace/box_operations.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <tuple>

using namespace zima;
namespace fs = std::filesystem;
namespace {
using commands::Json;
void require(bool ok, const char* text) {if(!ok) throw std::runtime_error(text);}
commands::Result run(command_host::Host& host, const char* name, Json args = Json::object()) {
    auto result = host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok) throw std::runtime_error(std::string(name)+": "+result.message);
    return result;
}
auto faces(const kernel::ViewerMesh& mesh) {
    std::set<std::tuple<std::string,std::string,std::string>> ids;
    for(const auto& ref : mesh.original_references.triangle_references)
        ids.emplace(ref.owner_id,ref.semantic_key,ref.instance_path);
    return ids;
}
void near(double value, double expected) {require(std::abs(value-expected)<1e-6,"Unexpected independent Box volume");}
void verify_commands(const kernel::OcctKernel& kernel, fs::path directory) {
    workspace::Workspace live;
    command_host::Interaction interaction;
    command_host::Options options;options.interaction=[&]{return interaction;};
    options.settings=[] {command_host::Settings settings;
        settings.templates={fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body 1"};
        return settings;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","box-command"}});
    const auto document_id=live.active_document_id();auto* state=live.open_part(document_id);
    const auto created=run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}}).data;
    const auto id=created.at("container").get<std::string>();
    require(created.at("changed")==true && host.change()->kind==command_host::ChangeKind::Model,"Creation event missing");
    require(created.at("body")==state->session.document().body_history.active_body_id(),"Box has wrong Body owner");
    const auto original=*state->session.document().find_container(id);
    near(state->session.calculated_boundaries().back().volume,6000);
    const auto face_ids=faces(state->session.calculated_boundaries().back().mesh);
    require(face_ids.size()==6,"Box must expose its six original faces");
    const auto generation=state->session.data_generation();const auto revision=state->session.revision();
    const auto* cached=state->session.calculated_boundaries().data();
    const auto query=run(host,"box.get",{{"container",id}}).data;
    require(query.at("length_mm")==10 && query.at("feature")==original.feature_id,"Incorrect persisted parameter query");
    require(!host.change() && state->session.data_generation()==generation && state->session.calculated_boundaries().data()==cached,"Query changed or calculated model");
    const auto unchanged=run(host,"box.set",{{"container",id},{"length_mm","10"}}).data;
    require(unchanged.at("changed")==false && !host.change() && state->session.revision()==revision && state->session.calculated_boundaries().data()==cached,"No-op recalculated or committed");
    for(const auto* value : {"0","-1","0.0009","1000001","nan","inf","1e309","2,5","12mm",""}) {
        const auto result=host.execute({{"command","box.set"},{"arguments",{{"container",id},{"height_mm",value}}}});
        require(!result.ok && result.code=="invalid_arguments" && !host.change(),"Invalid dimension accepted");
        require(state->session.revision()==revision && state->session.data_generation()==generation && state->session.calculated_boundaries().data()==cached,"Invalid dimension mutated model");
    }
    require(host.execute_text("box.set "+id).code=="invalid_arguments","Empty patch accepted");
    require(host.execute_text("box.get missing").code=="container_not_found","Missing Box accepted");
    require(host.execute_text("box.create 1 2 3 wrong-document").code=="document_changed","Stale document target accepted");
    interaction.editing=true;
    require(host.execute_text("box.create 1 2 3").code=="editing_in_progress","Box command overwrote an open dialog");
    run(host,"box.get",{{"container",id}});interaction.editing=false;
    interaction.active_occurrence="parent/part";
    require(host.execute_text("box.set "+id+" 11").code=="active_occurrence","Command bypassed occurrence editing guard");interaction.active_occurrence.clear();
    run(host,"box.set",{{"container",id},{"width_mm","40"}});
    near(state->session.calculated_boundaries().back().volume,12000);
    require(faces(state->session.calculated_boundaries().back().mesh)==face_ids,"Resize replaced original face identities");
    auto expected=original;expected.box.width=40;
    require(*state->session.document().find_container(id)==expected,"Dimension patch changed placement, locks or other feature data");
    run(host,"undo");near(state->session.calculated_boundaries().back().volume,6000);
    run(host,"redo");near(state->session.calculated_boundaries().back().volume,12000);
    run(host,"save");
    std::vector<kernel::BodyResult> loaded_bodies;const auto loaded=document::PartDocument::load(directory/"box-command.prtz",&loaded_bodies);
    require(*loaded.find_container(id)==expected && faces(loaded_bodies.back().mesh)==face_ids,"Save/load changed Box parameters or original identities");
    near(loaded_bodies.back().volume,12000);
    // A lock is shared with GUI. Explicit unlocking and editing in one OK is valid.
    auto locked=expected;locked.value_locks.insert("width");
    require(workspace::commit_box(live,kernel,document_id,locked,workspace::BoxEditMode::Replace),"Lock transaction ignored");
    const auto locked_revision=state->session.revision();
    require(host.execute({{"command","box.set"},{"arguments",{{"container",id},{"length_mm","15"},{"width_mm","50"}}}}).code=="value_locked","CLI bypassed lock");
    require(state->session.revision()==locked_revision && *state->session.document().find_container(id)==locked,"Rejected patch partly committed");
    locked.value_locks.clear();locked.box.width=50;
    require(workspace::commit_box(live,kernel,document_id,locked,workspace::BoxEditMode::Replace),"Explicit unlock+edit rejected");
    auto forged=locked;forged.feature_id="replacement-identity";
    bool rejected=false;try{static_cast<void>(workspace::commit_box(live,kernel,document_id,forged,workspace::BoxEditMode::Replace));}catch(const workspace::BoxOperationError& error){rejected=std::string(error.code)=="identity_changed";}
    require(rejected && state->session.document().find_container(id)->feature_id==original.feature_id,"Edit replaced topology identity");
    // New features follow the active Body cursor; editing another Body never activates it.
    auto branched=state->session.document();const auto second_body=branched.body_history.create_body("second");
    state->session.commit(branched,workspace::calculate_part(kernel,branched));
    const auto second=run(host,"box.create",{{"length_mm","5"},{"width_mm","6"},{"height_mm","7"}}).data.at("container").get<std::string>();
    require(state->session.document().body_history.owner(second)->scope.id==second_body,"Creation ignored active Body");
    run(host,"box.set",{{"container",id},{"length_mm","12"}});
    require(state->session.document().body_history.active_body_id()==second_body && state->session.document().body_history.owner(id)->scope.id==created.at("body").get<std::string>(),"Edit moved feature or changed active Body");
    auto insertion=state->session.document();insertion.body_history.set_history_cursor(second_body,0);
    state->session.commit(insertion,state->session.calculated_boundaries());
    const auto inserted=run(host,"box.create",{{"length_mm","1"},{"width_mm","2"},{"height_mm","3"}}).data.at("container").get<std::string>();
    const auto& entries=state->session.document().body_history.find(second_body)->entries;
    require(entries.size()==2 && entries[0].id==inserted && entries[1].id==second,"Body insertion cursor lost");
    run(host,"save");
    auto other=document::PartDocument::create_cylinder_container();
    auto with_other=state->session.document();with_other.insert_history_entry(document::PartHistoryKind::Feature,other.id);with_other.history.push_back(other);
    state->session.commit(with_other,workspace::calculate_part(kernel,with_other));
    require(host.execute_text("box.get "+other.id).code=="wrong_feature" && host.execute_text("box.set "+other.id+" 4").code=="wrong_feature","Box command changed a different feature kind");
    run(host,"new",{{"type","assembly"},{"name","other"}});
    require(host.execute_text("box.create 1 2 3").code=="unsupported_document","Box created in Assembly");
    require(run(host,"box.get",{{"container",id},{"document",document_id}}).data.at("length_mm")==12 && live.active_document_id()!=document_id,"Read query activated another document");
}
void verify_calculation_transaction(const kernel::OcctKernel& kernel) {
    auto part=document::PartDocument::create_default();
    auto first=document::PartDocument::create_box_container();first.box={10,20,30};
    auto broken=document::PartDocument::create_cylinder_container();broken.cylinder.radius=-1;
    static_cast<void>(part.body_history.create_body("fixture"));
    part.history={first,broken};
    part.insert_history_entry(document::PartHistoryKind::Feature,first.id);
    part.insert_history_entry(document::PartHistoryKind::Feature,broken.id);
    workspace::Workspace live;live.add_part(part);
    auto* state=live.open_part(part.document_id);
    state->session.update_calculated_boundaries(workspace::calculate_part(kernel,part));
    require(!state->session.calculated_boundaries().back().calculation_errors.empty(),"Broken downstream fixture did not fail calculation");
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    bool failed=false;
    try{static_cast<void>(workspace::commit_box(live,kernel,part.document_id,document::PartDocument::create_box_container(),workspace::BoxEditMode::Create));}catch(const std::exception&){failed=true;}
    require(failed,"Create did not reject the known calculation failure");
    require(state->session.revision()==revision && state->session.document().history==part.history && state->session.calculated_boundaries().data()==cache,"Failed calculation partly committed");
    first.box.length=15;
    require(workspace::commit_box(live,kernel,part.document_id,first,workspace::BoxEditMode::Replace),"Edit could not repair its own boundary with an existing downstream failure");
    require(state->session.document().find_container(first.id)->box.length==15 && !state->session.calculated_boundaries().back().calculation_errors.empty(),"Edit lost downstream error reporting");
}
}
int main() {
    try {
        kernel::OcctKernel kernel;
        const auto root=fs::canonical(fs::temp_directory_path());
        const auto directory=root/("zima-box-command-"+document::PartDocument::create_default().document_id);
        require(fs::create_directory(directory),"Cannot create unique test directory");
        verify_commands(kernel,directory);verify_calculation_transaction(kernel);
        require(directory.parent_path()==root,"Unexpected cleanup path");fs::remove_all(directory);
        std::cout<<"Shared Box: creation/editing, original identities, locks, atomic failure, Body cursor, save/load and Undo/Redo passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

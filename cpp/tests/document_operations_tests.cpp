#include <zima/workspace/document_operations.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class Function> void fails(Function&& function,const char* message) {
    bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,message);
}
}
int main() {
    using namespace zima;
    using namespace zima::workspace;
    namespace fs=std::filesystem;
    try {
        auto part=document::PartDocument::create_default();
        const auto id=part.document_id;
        const auto directory=fs::canonical(fs::temp_directory_path())/("zima-document-operations-"+id);
        fs::create_directory(directory);
        const auto part_path=directory/fs::path(u8"díl s mezerou.prtz");
        auto box=document::PartDocument::create_box_container();part.history.push_back(box);
        kernel::OcctKernel kernel;
        const auto boundaries=kernel.evaluate_history(part.kernel_operations());
        Workspace workspace;workspace.add_part(part,boundaries,part_path);
        workspace.activate(id);workspace.display_top_level(id);
        auto* state=workspace.open_part(id);
        auto edited=state->session.document();edited.name="saved snapshot";
        state->session.commit(edited,boundaries);
        const auto generation=state->session.data_generation();
        auto job=prepare_document_save(workspace,id,part_path);
        require(!fs::exists(part_path) && state->session.is_dirty(),"Preparing save wrote a file or cleared dirty state");
        auto saved=std::async(std::launch::async,[job=std::move(job)]{return job.write();}).get();
        require(state->session.is_dirty(),"Worker changed live document state");
        require(complete_document_save(workspace,saved),"Save completion failed");
        require(!state->session.is_dirty() && state->session.data_generation()==generation,"Save changed calculated data");
        std::vector<kernel::BodyResult> loaded_boundaries;
        auto loaded=document::PartDocument::load(part_path,&loaded_boundaries);
        require(loaded.document_id==id && loaded.name=="saved snapshot" && loaded.history.size()==1,"Part identity/parameters lost");
        require(loaded_boundaries.size()==boundaries.size() && loaded_boundaries.back().volume==boundaries.back().volume &&
            loaded_boundaries.back().mesh.original_references.triangle_references.size()==boundaries.back().mesh.original_references.triangle_references.size(),"Calculated geometry/references lost");
        const auto& original_faces=boundaries.back().mesh.original_references.triangle_references;
        const auto& saved_faces=loaded_boundaries.back().mesh.original_references.triangle_references;
        require(!original_faces.empty(),"Fixture has no persisted face references");
        for(std::size_t i=0;i<original_faces.size();++i)
            require(original_faces[i].owner_id==saved_faces[i].owner_id &&
                original_faces[i].semantic_key==saved_faces[i].semantic_key &&
                original_faces[i].instance_path==saved_faces[i].instance_path,"Original face identity changed during save");
        require(workspace.active_document_id()==id && workspace.displayed_document_id()==id,"Save switched documents");

        job=prepare_document_save(workspace,id,part_path);
        edited=state->session.document();edited.name="newer edit";state->session.commit(edited,boundaries);
        saved=job.write();require(complete_document_save(workspace,saved),"Save of older snapshot failed");
        require(state->session.is_dirty() && state->session.document().name=="newer edit" &&
            document::PartDocument::load(part_path).name=="saved snapshot","Background save lost a newer edit");
        job=prepare_document_save(workspace,id,part_path);
        state->session.update_calculated_boundaries(boundaries);
        require(complete_document_save(workspace,job.write()) && state->session.is_dirty(),"Older save cleared newly calculated data");

        const auto blocked=directory/"blocked.prtz";fs::create_directory(blocked);
        const auto revision=state->session.revision();
        job=prepare_document_save(workspace,id,blocked);
        fails([&]{static_cast<void>(job.write());},"Writing into a directory succeeded");
        require(state->path==part_path && state->session.is_dirty() && state->session.revision()==revision,"Failed write changed live state");
        fails([&]{static_cast<void>(prepare_document_save(workspace,"missing",part_path));},"Missing document accepted");
        fails([&]{static_cast<void>(prepare_document_save(workspace,id,{}));},"Empty path accepted");

        require(can_step_document_history(workspace,id,HistoryDirection::Undo),"Undo unavailable");
        require(step_document_history(workspace,id,HistoryDirection::Undo) && state->session.document().name=="saved snapshot","Core Undo failed");
        require(step_document_history(workspace,id,HistoryDirection::Redo) && state->session.document().name=="newer edit","Core Redo failed");
        require(!step_document_history(workspace,id,HistoryDirection::Redo),"Empty Redo succeeded");
        require(!step_document_history(workspace,"missing",HistoryDirection::Undo),"Unknown history target succeeded");

        auto assembly=assembly::AssemblyDocument::create_default();const auto assembly_id=assembly.document_id;
        auto leaf=assembly::AssemblyDocument::create_part_occurrence("box",id,part_path,boundaries.back());
        assembly.components.push_back(leaf);const auto assembly_path=directory/"assembly.asmz";
        workspace.add_assembly(assembly,assembly_path);
        auto* group=workspace.open_assembly(assembly_id);
        assembly.name="edited assembly";group->session.commit(assembly);
        auto assembly_job=prepare_document_save(workspace,assembly_id,assembly_path);
        require(complete_document_save(workspace,assembly_job.write()) && !group->session.is_dirty(),"Assembly save failed");
        auto loaded_assembly=assembly::AssemblyDocument::load(assembly_path);
        require(loaded_assembly.document_id==assembly_id && loaded_assembly.components.front().source_document_id==id &&
            loaded_assembly.components.front().occurrence_id==leaf.occurrence_id,"Assembly reference identity lost");
        assembly_job=prepare_document_save(workspace,assembly_id,assembly_path);
        assembly=group->session.document();assembly.name="newer assembly";group->session.commit(assembly);
        require(complete_document_save(workspace,assembly_job.write()) && group->session.is_dirty(),"Assembly save cleared a newer edit");
        assembly_job=prepare_document_save(workspace,assembly_id,assembly_path);
        group->session.update_dependency_snapshots(group->session.document());
        require(complete_document_save(workspace,assembly_job.write()) && group->session.is_dirty(),"Assembly save cleared a newer calculation");
        require(step_document_history(workspace,assembly_id,HistoryDirection::Undo) && group->session.document().name=="edited assembly","Assembly Undo failed");
        require(step_document_history(workspace,assembly_id,HistoryDirection::Redo) && group->session.document().name=="newer assembly","Assembly Redo failed");

        auto drawing=drawing::DrawingDocument::create_default();const auto drawing_id=drawing.document_id;
        drawing.source_document_id=id;drawing.source_path=part_path;drawing.name="saved drawing";
        const auto drawing_path=directory/"drawing.drwz";workspace.add_drawing(drawing,drawing_path);
        auto drawing_job=prepare_document_save(workspace,drawing_id,drawing_path);
        { auto edited=workspace.open_drawing(drawing_id)->document(); edited.name="newer drawing"; workspace.open_drawing(drawing_id)->commit(std::move(edited)); }
        require(complete_document_save(workspace,drawing_job.write()),"Drawing save failed");
        auto loaded_drawing=drawing::DrawingDocument::load(drawing_path);
        require(loaded_drawing.document_id==drawing_id && loaded_drawing.source_document_id==id && loaded_drawing.name=="saved drawing" &&
            workspace.open_drawing(drawing_id)->document().name=="newer drawing","Drawing snapshot or source reference lost");
        require(workspace.open_drawing(drawing_id)->is_dirty(),"Old Drawing save cleared newer edit");
        drawing_job=prepare_document_save(workspace,drawing_id,drawing_path);
        require(complete_document_save(workspace,drawing_job.write()) && !workspace.open_drawing(drawing_id)->is_dirty(),"Current Drawing save did not clear dirty state");
        const auto drawing_revision=workspace.open_drawing(drawing_id)->revision();
        auto wrong_drawing=workspace.open_drawing(drawing_id)->document();wrong_drawing.document_id="wrong-id";
        fails([&]{workspace.open_drawing(drawing_id)->commit(wrong_drawing);},"Drawing identity could be overwritten");
        require(workspace.open_drawing(drawing_id)->revision()==drawing_revision && !workspace.open_drawing(drawing_id)->is_dirty(),"Rejected Drawing edit changed revision");
        require(can_step_document_history(workspace,drawing_id,HistoryDirection::Undo),"Drawing edit has no history");
        require(workspace.active_document_id()==id && workspace.displayed_document_id()==id,"Operations activated another document");

        auto retargeted=prepare_document_save(workspace,id,part_path);
        workspace.open_part(id)->path=directory/"retargeted.prtz";
        require(!complete_document_save(workspace,retargeted.write()) && workspace.open_part(id)->path.filename()=="retargeted.prtz","Old save retargeted a document");
        auto closing=prepare_document_save(workspace,id,part_path);
        require(workspace.remove(id),"Close fixture failed");
        require(!complete_document_save(workspace,closing.write()),"Completion resurrected a closed document");
        workspace.add_part(part,boundaries,directory/"retargeted.prtz");
        require(!complete_document_save(workspace,closing.write()),"Old save attached to a reopened document with the same ID");
        // Exact unique test directory, verified under the temporary root before cleanup.
        require(fs::canonical(directory).parent_path()==fs::canonical(fs::temp_directory_path()),"Unsafe test cleanup path");
        fs::remove_all(directory);
        std::cout<<"Native save snapshots, I/O failures, newer edits/calculations, references and history passed without QApplication\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

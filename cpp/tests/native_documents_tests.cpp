#include <zima/workspace/native_documents.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void fails(F&& f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,message);}
std::string bytes(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(){
    using namespace zima;using namespace zima::workspace;namespace fs=std::filesystem;
    try {
        const auto template_root=fs::absolute("config/templates");
        const NativeTemplateSettings settings{template_root,"start_part.prtz","start_assembly.asmz","Těleso 1"};
        const auto part_template_bytes=bytes(template_root/settings.part_template);
        const auto assembly_template_bytes=bytes(template_root/settings.assembly_template);
        const auto template_part=document::PartDocument::load(template_root/settings.part_template);
        const auto template_assembly=assembly::AssemblyDocument::load(template_root/settings.assembly_template);
        const auto directory=fs::canonical(fs::temp_directory_path())/("zima-native-documents-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);
        Workspace workspace;
        auto first=prepare_new_native_document(NativeDocumentType::Part,"díl 1",directory/fs::path(u8"díl 1.prtz"),settings,{{"Length","cm"}});
        const auto first_id=first.id();
        require(first.type()==NativeDocumentType::Part && first_id!=template_part.document_id,"New Part reused template identity");
        require(workspace.size()==0 && !fs::exists(directory/fs::path(u8"díl 1.prtz")),"Preparation wrote a file or changed Workspace");
        require(insert_native_document(workspace,std::move(first))==first_id,"Part insert changed identity");
        const auto& first_document=workspace.open_part(first_id)->session.document();
        require(first_document.name=="díl 1" && first_document.document_units.at("Length")=="cm" &&
            first_document.document_precision==template_part.document_precision,"Part ignored config parameters");
        require(first_document.body_history.bodies().size()==1,"New Part body missing");
        const auto first_body=first_document.body_history.bodies().front();
        require(first_body.name=="Těleso 1" && first_body.scope.placement.references.size()==5,"Body name or origin attachment changed");
        for(const auto& reference:first_body.scope.placement.references)
            require(reference.owner_id==first_id+":origin","Body attached to the template origin");
        auto second=prepare_new_native_document(NativeDocumentType::Part,"second",directory/"second.prtz",settings);
        const auto second_id=insert_native_document(workspace,std::move(second));
        require(second_id!=first_id && workspace.open_part(second_id)->session.document().body_history.bodies().front().scope.id!=first_body.scope.id,"New Parts share persistent object IDs");
        require(workspace.active_document_id()==first_id && workspace.displayed_document_id()==first_id,"Creating another document switched context");

        auto group=prepare_new_native_document(NativeDocumentType::Assembly,"group",directory/"group.asmz",settings,{{"Length","cm"}});
        const auto group_id=insert_native_document(workspace,std::move(group));
        const auto& group_document=workspace.open_assembly(group_id)->session.document();
        require(group_id!=template_assembly.document_id && group_document.document_units.at("Length")=="cm" &&
            group_document.document_precision==template_assembly.document_precision && group_document.components.empty(),"Assembly template/config contract changed");
        const auto drawing_id=insert_native_document(workspace,prepare_new_native_document(NativeDocumentType::Drawing,"drawing",directory/"drawing.drwz",{}));
        require(workspace.open_drawing(drawing_id)->document.name=="drawing","Drawing creation requires a Part template");
        require(bytes(template_root/settings.part_template)==part_template_bytes && bytes(template_root/settings.assembly_template)==assembly_template_bytes,"Creation modified config templates");
        auto missing=settings;missing.part_template="missing.prtz";
        fails([&]{static_cast<void>(part_from_template(missing));},"Missing template accepted");
        workspace.open_part(first_id)->session.document().save(directory/"populated.prtz");
        auto invalid=settings;invalid.part_template=directory/"populated.prtz";
        fails([&]{static_cast<void>(part_from_template(invalid));},"Populated Part template accepted");
        auto populated_assembly=template_assembly;
        populated_assembly.components.push_back(assembly::AssemblyDocument::create_part_occurrence("body",first_id,{},{}));
        populated_assembly.save(directory/"populated.asmz");invalid=settings;invalid.assembly_template=directory/"populated.asmz";
        fails([&]{static_cast<void>(assembly_from_template(invalid));},"Populated Assembly template accepted");
        fails([&]{static_cast<void>(prepare_new_native_document(NativeDocumentType::Part,"bad/name",directory/"invalid.prtz",settings));},"Invalid name accepted");
        fails([&]{static_cast<void>(prepare_new_native_document(NativeDocumentType::Part,"wrong",directory/"wrong.asmz",settings));},"Wrong extension accepted");
        fails([&]{static_cast<void>(prepare_new_native_document(NativeDocumentType::Part,"occupied",directory/"populated.prtz",settings));},"Existing file accepted for New");
        const auto count=workspace.size();
        fails([&]{static_cast<void>(insert_native_document(workspace,prepare_new_native_document(NativeDocumentType::Part,"occupied",directory/"second.prtz",settings)));},"Open path accepted for New");
        auto raced=prepare_new_native_document(NativeDocumentType::Part,"raced",directory/"raced.prtz",settings);
        {std::ofstream file(directory/"raced.prtz");file<<"created by another writer";}
        fails([&]{static_cast<void>(insert_native_document(workspace,std::move(raced)));},"New overwrote a file created after preparation");
        require(workspace.size()==count,"Failed creation inserted a document");

        auto model=document::PartDocument::create_default();model.name="stored model";
        model.history.push_back(document::PartDocument::create_box_container());
        kernel::OcctKernel kernel;const auto boundaries=kernel.evaluate_history(model.kernel_operations());
        const auto model_path=directory/fs::path(u8"uložený model.PRTZ");model.save(model_path,boundaries);
        auto loaded=std::async(std::launch::async,[model_path]{return read_native_document(model_path);}).get();
        const auto model_id=loaded.id();require(model_id==model.document_id,"Read changed document identity");
        require(workspace.size()==count,"Read inserted a document");
        require(insert_native_document(workspace,std::move(loaded))==model_id,"Open insert changed identity");
        const auto& loaded_part=*workspace.open_part(model_id);
        require(!loaded_part.session.is_dirty() && !loaded_part.session.can_undo() && loaded_part.session.calculated_boundaries().size()==boundaries.size(),"Open calculated or edited a Part");
        require(loaded_part.session.calculated_boundaries().back().volume==boundaries.back().volume,"Open lost calculated geometry");
        const auto& expected=boundaries.back().mesh.original_references.triangle_references;
        const auto& actual=loaded_part.session.calculated_boundaries().back().mesh.original_references.triangle_references;
        require(!expected.empty() && expected.size()==actual.size(),"Loaded face references missing");
        for(std::size_t i=0;i<expected.size();++i)
            require(expected[i].owner_id==actual[i].owner_id && expected[i].semantic_key==actual[i].semantic_key && expected[i].instance_path==actual[i].instance_path,"Open changed a source face identity");
        auto stale=read_native_document(model_path);
        auto changed=loaded_part.session.document();changed.name="unsaved edit";
        workspace.open_part(model_id)->session.commit(changed,boundaries);
        require(insert_native_document(workspace,std::move(stale))==model_id && workspace.open_part(model_id)->session.document().name=="unsaved edit" && workspace.open_part(model_id)->session.is_dirty(),"Concurrent open overwrote an unsaved document");
        fs::copy_file(model_path,directory/"duplicate-id.prtz");
        const auto before_failure=workspace.size();
        fails([&]{static_cast<void>(insert_native_document(workspace,read_native_document(directory/"duplicate-id.prtz")));},"Duplicate ID at another path accepted");
        fails([&]{static_cast<void>(read_native_document(directory/"missing.prtz"));},"Missing file accepted");
        fails([&]{static_cast<void>(read_native_document(directory/"raced.prtz"));},"Corrupt file accepted");
        fails([&]{static_cast<void>(read_native_document(directory/"legacy.prt"));},"Legacy extension accepted");
        require(workspace.size()==before_failure && workspace.active_document_id()==first_id && workspace.displayed_document_id()==first_id,"Failed read changed context");
        auto stored_group=assembly::AssemblyDocument::create_default();
        const auto component=assembly::AssemblyDocument::create_part_occurrence("component",model_id,model_path,boundaries.back());
        stored_group.components.push_back(component);stored_group.save(directory/"stored.asmz");
        const auto stored_group_id=insert_native_document(workspace,read_native_document(directory/"stored.asmz"));
        const auto& restored_component=workspace.open_assembly(stored_group_id)->session.document().components.front();
        require(restored_component.source_document_id==model_id && restored_component.occurrence_id==component.occurrence_id,"Assembly open changed occurrence identity");
        auto stored_drawing=drawing::DrawingDocument::create_default();stored_drawing.source_document_id=model_id;stored_drawing.source_path=model_path;
        stored_drawing.save(directory/"stored.drwz");
        const auto stored_drawing_id=insert_native_document(workspace,read_native_document(directory/"stored.drwz"));
        require(workspace.open_drawing(stored_drawing_id)->document.source_document_id==model_id,"Drawing open lost model reference");
        require(fs::canonical(directory).parent_path()==fs::canonical(fs::temp_directory_path()),"Unsafe cleanup path");fs::remove_all(directory);
        std::cout<<"Native read/create, templates, fresh identities, cache, references, collisions and failures passed without QApplication\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

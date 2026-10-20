#include "profile_solid_fixture.hpp"
#include <zima/workspace/native_documents.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/document_copy_json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <future>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <zima/document/versioned_file.hpp>
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void fails(F&& f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,message);}
std::string bytes(const std::filesystem::path& path){std::ifstream in(zima::document::native_file_io_path(path),std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(){
    using namespace zima;using namespace zima::workspace;namespace fs=std::filesystem;
    try {
        {
            nlohmann::json copied={{"document_id","source"},{"links",nlohmann::json::array({
                {{"source_path","external-reference:curve-id"}},
                {{"source_path","external.prtz"}},
                {{"source_path","old.prtz"},{"source_document_id","source"}}
            })}};
            const auto source_directory=fs::absolute("build/copy-source");
            const auto target=fs::absolute("build/copy-target/copied.prtz");
            document::remap_document_identity(copied,"source","copy",{},source_directory,target);
            require(copied["links"][0]["source_path"]=="external-reference:curve-id","Document copy rebased an internal Sketch link as a file");
            require(copied["links"][1]["source_path"]==document::path_to_utf8(source_directory/"external.prtz"),"Document copy lost an external file path");
            require(copied["links"][2]["source_path"]==document::path_to_utf8(target)&&copied["links"][2]["source_document_id"]=="copy","Document copy lost a self reference");
        }
#ifdef _WIN32
        {
            const fs::path short_path=LR"(relative\model.prtz)";
            require(document::native_file_io_path(short_path)==short_path,"Native I/O changed an ordinary short path");
            const fs::path explicit_path=LR"(\\?\C:\data\model.prtz)",device=LR"(\\.\pipe\zima-test)";
            require(document::native_file_io_path(explicit_path)==explicit_path&&document::native_file_io_path(device)==device,"Native I/O changed an explicit Windows namespace");
            const auto unc=fs::path(LR"(\\server\share)")/std::wstring(140,L'a')/std::wstring(140,L'b')/L"model.prtz";
            require(document::native_file_io_path(unc).native().starts_with(LR"(\\?\UNC\server\share\)"),"Native long UNC I/O prefix is wrong");
            const auto relative=fs::path(L"zima-io-prefix-test")/std::wstring(140,L'a')/std::wstring(140,L'b')/L"model.prtz";
            require(document::native_file_io_path(relative).native().starts_with(LR"(\\?\)"),"Native relative long path did not resolve for I/O");
            const auto parent=fs::current_path();
            require(parent.native().size()<247,"Native I/O boundary fixture root is too long");
            const auto boundary=parent/std::wstring(247-parent.native().size(),L'x');
            require(boundary.native().size()==248&&document::native_file_io_path(boundary).native().starts_with(LR"(\\?\)"),
                    "Native I/O missed the Windows directory-creation boundary");
        }
#endif
        const auto template_root=fs::absolute("config/templates");
        const NativeTemplateSettings settings{template_root,"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Těleso 1"};
        const auto part_template_bytes=bytes(template_root/settings.part_template);
        const auto assembly_template_bytes=bytes(template_root/settings.assembly_template);
        const auto template_part=document::PartDocument::load(template_root/settings.part_template);
        const auto template_assembly=assembly::AssemblyDocument::load(template_root/settings.assembly_template);
        const auto check_labels=[](const auto& model) {
            for(const auto& [key,labels]:model.user_parameter_labels)if(labels.contains("cs")) {
                const auto& label=labels.at("cs");
                require(!label.empty()&&std::ranges::all_of(label,[](unsigned char c){return c<128&&!(c>='A'&&c<='Z');}),
                    "Czech template parameter labels must be lowercase without diacritics");
            }
        };
        check_labels(template_part);check_labels(template_assembly);
        const auto directory=fs::canonical(fs::temp_directory_path())/("zima-native-documents-"+document::PartDocument::create_default().document_id);
        fs::create_directory(directory);
        {
            const auto ascii=directory/"native-io-ascii",unicode=directory/fs::path(u8"\u010desk\u00fd adres\u00e1\u0159-\u691c\u67fb");
            fs::create_directories(ascii);fs::create_directories(unicode);
            const auto verify=[&](auto model,const char* extension,bool temporary_write,bool long_name=false) {
                using Model=std::decay_t<decltype(model)>;
                model.name="\xc5\xbd\x6c\x75\xc5\xa5\x6f\x75\xc4\x8d\x6b\xc3\xbd\x20\xd0\x9f\xd1\x80\xd0\xb8\xd0\xbc\xd0\xb5\xd1\x80\x20\xe6\xa4\x9c\xe6\x9f\xbb";
                auto a=ascii/"model";a+=extension;
                auto b=unicode/fs::path(u8"d\u00edl-\u691c\u67fb");b+=extension;
                if(long_name) {
                    const auto prefix=(unicode/"x").native().size()-1;
                    const auto suffix=std::char_traits<char>::length(extension);
                    require(prefix+suffix<259,"Native test temporary root is too long");
                    b=unicode/(std::string(259-prefix-suffix,'x')+extension);
                    require(b.native().size()==259,"Long native fixture does not reach the Windows filename limit");
                }
                model.save(a);model.save(b);
                require(bytes(a)==bytes(b),"Native Unicode path changed persisted document bytes");
                const auto initial=bytes(b);const auto id=model.document_id;
                auto loaded=Model::load(b);require(loaded.document_id==id&&loaded.name==model.name,"Native Unicode document did not reopen with its identity/name");
                model.name="\x53\x65\x63\x6f\x6e\x64\x20\x72\x65\x76\x69\x73\x69\x6f\x6e\x20\xd0\x9f\xd1\x80\xd0\xb8\xd0\xbc\xd0\xb5\xd1\x80\x20\xe6\xa4\x9c\xe6\x9f\xbb";model.save(b);
                auto archive=b;archive+=".1";
                require(fs::is_regular_file(document::native_file_io_path(archive))&&bytes(archive)==initial,"Unicode overwrite did not archive the exact original");
                loaded=Model::load(b);require(loaded.document_id==id&&loaded.name==model.name,"Unicode overwrite lost document identity/name");
                const auto archived=Model::load(archive);require(archived.document_id==id,"Long Unicode archive did not reopen");
                if(temporary_write) {
                    const auto before=bytes(b);auto temporary=b;temporary+=".tmp";fs::create_directory(document::native_file_io_path(temporary));
                    model.name="Rejected write";
                    fails([&]{model.save(b);},"Native write ignored an unavailable temporary target");
                    require(bytes(b)==before&&document::archive_paths(b).size()==1&&fs::is_directory(document::native_file_io_path(temporary)),"Failed Unicode write changed the original, archives or pre-existing temporary directory");
                    fs::remove(document::native_file_io_path(temporary));
                }
                if(long_name) {
                    // Remove over-limit archive names explicitly: the ordinary
                    // recursive cleanup root itself has a short spelling.
                    fs::remove(document::native_file_io_path(archive));
                    fs::remove(document::native_file_io_path(b));
                }
            };
            verify(document::PartDocument::create_default(),".prtz",true);
            verify(assembly::AssemblyDocument::create_default(),".asmz",true);
            verify(drawing::DrawingDocument::create_default(),".drwz",false);
            verify(document::PartDocument::create_default(),".prtz",true,true);
            verify(assembly::AssemblyDocument::create_default(),".asmz",true,true);
            verify(drawing::DrawingDocument::create_default(),".drwz",false,true);
            std::cout<<"Native Unicode/CJK Part, Assembly and Drawing: identical bytes, overwrite archives, reopen and failed-write preservation passed\n";
        }
        {
            const auto fixture=fs::absolute("cpp/tests/fixtures/parity/ventilation-linux.prtz");
            const auto before=bytes(fixture);
            require(before.find("9c5e8e780b30accd")!=std::string::npos,"Linux fixture lost its original runtime fingerprint");
            std::vector<kernel::BodyResult> calculated;
            const auto portable=document::PartDocument::load(fixture,&calculated);
            require(bytes(fixture)==before&&calculated.size()==7,"Opening the Linux calculation changed the source or lost history");
            require(calculated.back().surface_area==2242.5998362849596&&calculated.back().volume==0,
                "Opening the Linux calculation changed stored physical properties");
            const auto snapshot=portable.serialized(calculated);
            portable.save(directory/"linux-roundtrip.prtz",calculated);
            std::vector<kernel::BodyResult> reopened;
            const auto restored=document::PartDocument::load(directory/"linux-roundtrip.prtz",&reopened);
            require(restored.serialized(reopened)==snapshot,"Linux save/reopen changed exact geometry, references or authored parameters");
        }
        Workspace workspace;
        {
            const auto blank=drawing_from_template(settings);
            require(blank.sheets.front().format==drawing::SheetFormat::A4&&blank.sheets.front().frame_lines.empty(),
                "Missing optional Drawing templates must leave a blank A4 sheet");
            auto configured=settings;configured.drawing_format=drawing::SheetFormat::A3;
            configured.drawing_frame_template=fs::absolute("tests/fixtures/drawing-library/ZE-A3.frmz");
            configured.drawing_title_block_template=fs::absolute("tests/fixtures/drawing-library/ZE-TITLE-BLOCK-EN.tblz");
            const auto prepared=prepare_new_native_document(NativeDocumentType::Drawing,"formatted",directory/"formatted.drwz",configured);
            prepared.write(directory/"formatted.drwz");
            const auto formatted=drawing::DrawingDocument::load(directory/"formatted.drwz");
            const auto& sheet=formatted.sheets.front();
            require(sheet.format==drawing::SheetFormat::A3&&!sheet.frame_lines.empty()&&
                !sheet.title_block_lines.empty()&&sheet.title_block_locale=="en","Configured Drawing templates were not embedded");
            configured.drawing_frame_template=directory/"missing.frmz";
            fails([&]{static_cast<void>(drawing_from_template(configured));},"Missing selected Drawing frame was silently ignored");
            require(workspace.size()==0,"Drawing template preparation mutated Workspace");
        }
        auto first=prepare_new_native_document(NativeDocumentType::Part,"díl 1",directory/fs::path(u8"díl 1.prtz"),settings,{{"Length","cm"}});
        const auto first_id=first.id();
        require(first.type()==NativeDocumentType::Part && first_id!=template_part.document_id,"New Part reused template identity");
        require(workspace.size()==0 && !fs::exists(directory/fs::path(u8"díl 1.prtz")),"Preparation wrote a file or changed Workspace");
        require(insert_native_document(workspace,std::move(first))==first_id,"Part insert changed identity");
        const auto& first_document=workspace.open_part(first_id)->session.document();
        require(first_document.name=="díl 1" && first_document.document_units.at("Length")=="cm" &&
            first_document.document_precision==template_part.document_precision,"Part ignored config parameters");
        require(first_document.body_history.bodies().size()==1,"New Part body missing");
        check_labels(first_document);
        const auto first_body=first_document.body_history.bodies().front();
        require(first_body.name=="Těleso 1" && first_body.scope.placement.references.size()==5,"Body name or origin attachment changed");
        for(const auto& reference:first_body.scope.placement.references)
            require(reference.owner_id==first_id+":origin","Body attached to the template origin");
        auto second=prepare_new_native_document(NativeDocumentType::Part,"second",directory/"second.prtz",settings);
        const auto second_id=insert_native_document(workspace,std::move(second));
        require(second_id!=first_id && workspace.open_part(second_id)->session.document().body_history.bodies().front().scope.id!=first_body.scope.id,"New Parts share persistent object IDs");
        require(workspace.active_document_id()==first_id && workspace.displayed_document_id()==first_id,"Creating another document switched context");
        auto custom_settings=settings;custom_settings.sheet_cut_tolerance=.0125;
        const auto skeleton_template=document::PartDocument::load(template_root/"START_SKELETON_mm.prtz");
        require(skeleton_template.body_history.bodies().empty(),"Skeleton template contains persistent Body identities");
        for(const auto* filename:{"layout_skeleton.prtz","reference_SKELETON.PRTZ"}) {
            auto skeleton=prepare_new_native_document(NativeDocumentType::Part,"layout",directory/filename,settings);
            skeleton.write(directory/filename);
            const auto loaded=document::PartDocument::load(directory/filename);
            require(loaded.document_id!=skeleton_template.document_id && loaded.document_id!=first_id &&
                loaded.history.empty() && loaded.sketches.empty() && loaded.constructions.empty() &&
                loaded.body_history.bodies().size()==1 && !loaded.body_history.active_body_id().empty(),
                "Skeleton did not create a clean Part with a fresh active Body");
            require(loaded.appearance.body.color=="#4D000000" && loaded.body_color=="#4D000000" &&
                loaded.appearance.bodies.empty(),"Skeleton did not retain its transparent black inherited appearance");
        }
        require(first_document.appearance.body==template_part.appearance.body,"Skeleton changed the ordinary Part appearance");
        auto custom_part=part_from_template(custom_settings);custom_part.save(directory/"custom-tolerance.prtz");
        require(std::stod(custom_part.document_precision.at("sheet_cut_tolerance"))==.0125,
            "New Part did not snapshot the configured Sheet Cut tolerance");
        custom_settings.sheet_cut_tolerance=.1;
        require(std::stod(part_from_template(custom_settings).document_precision.at("sheet_cut_tolerance"))==.1,
            "Changed global default did not affect the next new Part");
        const auto custom_id=insert_native_document(workspace,read_native_document(directory/"custom-tolerance.prtz"));
        require(std::stod(workspace.open_part(custom_id)->session.document().document_precision.at("sheet_cut_tolerance"))==.0125&&
            std::stod(workspace.open_part(first_id)->session.document().document_precision.at("sheet_cut_tolerance"))==.05,
            "A changed global default altered an existing or reopened Part");
        custom_settings.sheet_cut_tolerance=0;
        fails([&]{static_cast<void>(part_from_template(custom_settings));},"Invalid new-Part Sheet Cut tolerance accepted");

        auto group=prepare_new_native_document(NativeDocumentType::Assembly,"group",directory/"group.asmz",settings,{{"Length","cm"}});
        const auto group_id=insert_native_document(workspace,std::move(group));
        const auto& group_document=workspace.open_assembly(group_id)->session.document();
        check_labels(group_document);
        require(group_id!=template_assembly.document_id && group_document.document_units.at("Length")=="cm" &&
            group_document.document_precision==template_assembly.document_precision && group_document.components.empty(),"Assembly template/config contract changed");
        const auto drawing_id=insert_native_document(workspace,prepare_new_native_document(NativeDocumentType::Drawing,"drawing",directory/"drawing.drwz",{}));
        require(workspace.open_drawing(drawing_id)->document().name=="drawing","Drawing creation requires a Part template");
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
        model.history.push_back(zima::test::rectangular_feature(model));
        kernel::OcctKernel kernel;const auto boundaries=kernel.evaluate_history(model.kernel_operations());
        {
            auto stored=model.serialized(boundaries);
            // A different platform may reconstruct temporary arc points with
            // different last bits. Its runtime reuse keys are not persistence
            // authority; the exact native definition is.
            for(auto& boundary:stored["calculated_boundaries"])
                boundary["source_fingerprint"]="other-platform-runtime-key";
            std::vector<kernel::BodyResult> rebound;
            const auto portable=document::PartDocument::from_serialized(stored,&rebound);
            require(portable.serialized(rebound)==model.serialized(boundaries),
                "Portable calculation changed its definition, geometry, properties or references");
            auto renamed=stored;renamed["name"]="Renamed without calculation";
            require(document::PartDocument::from_serialized(renamed).name=="Renamed without calculation",
                "A document rename invalidated unchanged geometry");
            auto altered=stored;altered["document_precision"]["linear_tolerance"]="0.0005";
            fails([&]{document::PartDocument::from_serialized(altered);},"Stale calculation accepted changed precision");
            altered=stored;altered["sketches"][0]["points"][0]["x"]=123.0;
            fails([&]{document::PartDocument::from_serialized(altered);},"Stale calculation accepted changed Sketch geometry");
            altered=stored;altered["sketches"][0]["points"][0]["x"]=std::nextafter(
                stored["sketches"][0]["points"][0]["x"].get<double>(),std::numeric_limits<double>::infinity());
            fails([&]{document::PartDocument::from_serialized(altered);},"Stale calculation accepted a one-bit authored change");
            altered=stored;altered.erase("calculation_definition_fingerprint");
            fails([&]{document::PartDocument::from_serialized(altered);},"Calculation without an authored-definition proof accepted");
            auto changed=model;zima::test::resize_rectangular_feature(changed,changed.history.front(),{21,10,6});
            fails([&]{changed.serialized(boundaries);},"Save accepted stale geometry after a parameter change");
        }
        const auto model_path=directory/fs::path(u8"uložený model.PRTZ");model.save(model_path,boundaries);
        auto loaded=std::async(std::launch::async,[model_path]{return read_native_document(model_path);}).get();
        const auto model_id=loaded.id();require(model_id==model.document_id,"Read changed document identity");
        require(workspace.size()==count,"Read inserted a document");
        require(insert_native_document(workspace,std::move(loaded))==model_id,"Open insert changed identity");
        auto linked_drawing=prepare_new_native_document(
            NativeDocumentType::Drawing,"linked drawing",directory/"linked-drawing.drwz",{});
        linked_drawing.set_drawing_source(model_id,model_path,model.name);
        const auto linked_drawing_id=insert_native_document(workspace,std::move(linked_drawing));
        const auto& linked=workspace.open_drawing(linked_drawing_id)->document();
        require(linked.source_document_id==model_id&&!linked.sheets.empty()&&
            linked.sheets.front().bom_source_document_id==model_id&&
            linked.sheets.front().bom_rows.size()==1&&
            linked.sheets.front().bom_rows.front().source_document_id==model_id,
            "New Drawing did not initialize its independent sheet BOM source");
        auto independent=linked;
        independent.add_data_source({group_id,directory/"group.asmz","Group"});
        independent.sheets.front().selected_source_document_id=group_id;
        independent.sheets.front().bom_source_document_id.clear();independent.sheets.front().bom_rows.clear();
        independent.save(directory/"independent.drwz");
        Workspace reopened;insert_native_document(reopened,read_native_document(directory/"independent.drwz"));
        const auto& checked=reopened.open_drawing(independent.document_id)->document();
        require(checked.sheets.front().selected_source_document_id==group_id&&checked.sheets.front().bom_source_document_id.empty()&&checked.data_sources().size()==2,
            "Opening Drawing reset its chooser or rebound an explicitly disconnected title");
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
        require(workspace.open_drawing(stored_drawing_id)->document().source_document_id==model_id,"Drawing open lost model reference");
        {
            const auto unicode=directory/fs::path(u8"\u010desk\u00fd adres\u00e1\u0159-\u691c\u67fb");
            const auto unicode_part=unicode/fs::path(u8"solid-\u691c\u67fb.prtz");model.save(unicode_part,boundaries);
            require(bytes(unicode_part)==bytes(model_path),"Unicode cached Part changed native bytes");
            std::vector<kernel::BodyResult> restored_cache;const auto restored=document::PartDocument::load(unicode_part,&restored_cache);
            require(restored.serialized(restored_cache)==model.serialized(boundaries),"Unicode cached Part lost geometry or references");
            auto group=assembly::AssemblyDocument::create_default();
            const auto occurrence=assembly::AssemblyDocument::create_part_occurrence("component",model_id,unicode_part,boundaries.back());
            group.components={occurrence};const auto unicode_group=unicode/fs::path(u8"assembly-\u691c\u67fb.asmz");group.save(unicode_group);
            const auto reopened=assembly::AssemblyDocument::load(unicode_group);
            require(reopened.components.front().source_path==unicode_part&&reopened.components.front().source_document_id==model_id&&reopened.components.front().occurrence_id==occurrence.occurrence_id,"Unicode Assembly lost its native dependency/occurrence identity");
            auto drawing=drawing::DrawingDocument::create_default();drawing.source_document_id=model_id;drawing.source_path=unicode_part;
            const auto unicode_drawing=unicode/fs::path(u8"drawing-\u691c\u67fb.drwz");drawing.save(unicode_drawing);
            const auto loaded=drawing::DrawingDocument::load(unicode_drawing);
            require(loaded.source_path==unicode_part&&loaded.source_document_id==model_id,"Unicode Drawing lost its native source reference");
        }
        require(fs::canonical(directory).parent_path()==fs::canonical(fs::temp_directory_path()),"Unsafe cleanup path");fs::remove_all(directory);
        std::cout<<"Native read/create, templates, fresh identities, cache, references, collisions and failures passed without QApplication\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

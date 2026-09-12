#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_title_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);
    const auto part=[&](const char* file,const char* name) {
        run(host,"new",{{"type","part"},{"name",file}});const auto id=live.active_document_id();
        run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
        auto data=workspace::user_parameters(live,id);
        for(const auto* key:{"name","revision"})if(std::ranges::find(data.order,key)==data.order.end())data.order.push_back(key);
        data.values["name"][""]=name;data.values["revision"][""]="A";data.labels["name"]["cs"]="Název";
        static_cast<void>(workspace::set_user_parameters(live,id,std::move(data)));run(host,"save");return id;
    };
    const auto first=part("title-first","First"),second=part("title-second","Second");
    run(host,"new",{{"type","assembly"},{"name","title-owner"}});const auto owner=live.active_document_id();
    run(host,"component.insert",{{"source",first}});run(host,"component.insert",{{"source",first}});run(host,"component.insert",{{"source",second}});run(host,"save");
    const auto parent_revision=live.open_assembly(owner)->session.revision();
    auto doc=drawing::DrawingDocument::create_default();doc.source_document_id=owner;doc.source_path=directory/"title-owner.asmz";
    auto& sheet=doc.sheets.front();const auto sheet_id=sheet.id;
    const auto field=[](std::string id,std::string text,bool back){drawing::TitleBlockField f;f.id=std::move(id);f.expression=std::move(text);f.editable=true;f.write_back=back;return f;};
    sheet.title_block_fields={field("NAME","&Název",true),field("LOCAL","&drawing.note",false),field("LITERAL","Plain",false),field("SYSTEM","&sheet.number",false),field("COMPOUND","&name / &revision",true)};
    drawing::TemplateText raw;raw.text="&revision";sheet.title_block_texts.push_back(raw);sheet.local_parameters["note"]="Before";
    sheet.bom_rows=workspace::build_bom_rows_for_source(owner,doc.source_path,&live);
    require(sheet.bom_rows.size()==2&&sheet.bom_rows[0].quantity==2,"BOM fixture did not group occurrences");
    const auto row=sheet.bom_rows[1].designation;auto another=sheet;another.id=drawing::DrawingDocument::create_default().sheets.front().id;doc.sheets.push_back(another);
    live.add_drawing(doc,directory/"title.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    run(host,"close",{{"document",second}});require(!live.open_part(second),"Source did not close");
    const auto count=live.size(),revision=live.open_drawing(doc.document_id)->revision();
    const auto bom=run(host,"drawing.bom.list",{{"sheet",sheet_id}}).data;
    require(bom.at("total")==2&&bom.at("items")[1].at("row")==row&&bom.at("items")[1].at("source_document")==second,"Stored BOM query lost exact source");
    const auto title=run(host,"drawing.title.get",{{"sheet",sheet_id},{"bom_row",row}}).data;
    require(live.size()==count&&live.open_drawing(doc.document_id)->revision()==revision,"Title query opened documents or changed history");
    bool name=false,raw_parameter=false,system=false;
    for(const auto& f:title.at("fields")){if(f.at("field")=="NAME")name=f.at("value")=="Second"&&f.at("writable")==true;if(f.at("field")=="parameter:revision")raw_parameter=true;if(f.at("field")=="SYSTEM")system=f.at("writable")==false;}
    require(name&&raw_parameter&&system,"Title query omitted alias, raw parameter or read-only policy");
    const auto rejected=[&](Json values,const char* code) {
        const auto result=host.execute({{"command","drawing.title.set"},{"arguments",{{"sheet",sheet_id},{"bom_row",row},{"values",values}}}});
        require(!result.ok&&result.code==code,"Invalid title write was accepted or misclassified");
        require(live.size()==count&&!live.open_part(second)&&live.open_drawing(doc.document_id)->revision()==revision&&live.open_drawing(doc.document_id)->document().find_sheet(sheet_id)->local_parameters.at("note")=="Before","Rejected write partially mutated documents or opened source");
    };
    rejected({{"LOCAL","Partial"},{"ZZZ","Unknown"}},"field_not_found");
    rejected({{"NAME","Partial"},{"SYSTEM","123"}},"read_only");
    rejected({{"COMPOUND","Partial"}},"read_only");
    const auto changed=run(host,"drawing.title.set",{{"sheet",sheet_id},{"bom_row",row},{"values",{{"NAME","Second revised"},{"LOCAL","After"}}},{"expected_values",{{"NAME","Second"}}}}).data;
    require(changed.at("source_changed")==true&&live.open_part(second)&&live.active_document_id()==doc.document_id,"Title write did not open authoritative source or changed active document");
    require(live.open_part(second)->session.document().user_parameters.at("name")=="Second revised"&&live.open_part(first)->session.document().user_parameters.at("name")=="First"&&live.open_assembly(owner)->session.revision()==parent_revision,"BOM edit changed wrong Part or regenerated its parent");
    require(std::abs(live.open_part(second)->session.calculated_boundaries().back().volume-6000)<1e-6,"Metadata edit changed calculated body");
    for(const auto& s:live.open_drawing(doc.document_id)->document().sheets)require(s.bom_rows[1].parameters.at("name")=="Second revised"&&s.bom_rows[0].quantity==2,"Source metadata mirrors or quantities are wrong");
    const auto updated_revision=live.open_drawing(doc.document_id)->revision();
    require(!host.execute({{"command","drawing.title.set"},{"arguments",{{"sheet",sheet_id},{"bom_row",row},{"values",{{"NAME","Overwrite"}}},{"expected_values",{{"NAME","Second"}}}}}}).ok&&live.open_drawing(doc.document_id)->revision()==updated_revision,"Stale expected value overwrote source");
    require(run(host,"drawing.title.set",{{"sheet",sheet_id},{"bom_row",row},{"values",{{"NAME","Second revised"}}}}).data.at("changed")==false,"Unchanged title added history");
    require(document::PartDocument::load(directory/"title-second.prtz").user_parameters.at("name")=="Second","Title write saved the source implicitly");
    run(host,"save");const auto saved=drawing::DrawingDocument::load(directory/"title.drwz");require(saved.find_sheet(sheet_id)->local_parameters.at("note")=="After"&&saved.find_sheet(sheet_id)->bom_rows[1].parameters.at("name")=="Second revised","Drawing metadata persistence failed");
    run(host,"undo");require(live.open_drawing(doc.document_id)->document().find_sheet(sheet_id)->local_parameters.at("note")=="Before"&&live.open_part(second)->session.document().user_parameters.at("name")=="Second revised","Drawing Undo changed source ownership");
    run(host,"redo");
    auto pending=live.open_drawing(doc.document_id)->document();const auto edit=workspace::prepare_drawing_title_edit(pending,sheet_id,&live,directory/"title.drwz",row);
    pending.find_sheet(sheet_id)->local_parameters["note"]="Concurrent";
    try{workspace::edit_drawing_title(pending,&live,edit,{{"LOCAL","Stale"}});throw std::logic_error("Stale dialog accepted");}catch(const workspace::DrawingOperationError& error){require(error.code=="source_changed","Wrong stale dialog error");}
    require(pending.find_sheet(sheet_id)->local_parameters.at("note")=="Concurrent","Stale dialog overwrote drawing value");

    auto duplicate=pending;duplicate.find_sheet(sheet_id)->title_block_fields.push_back(field("LOCAL_ALIAS","&drawing.note",false));
    auto duplicate_edit=workspace::prepare_drawing_title_edit(duplicate,sheet_id,&live,directory/"title.drwz",row);
    require(workspace::edit_drawing_title(duplicate,&live,duplicate_edit,{{"LOCAL","Together"},{"LOCAL_ALIAS","Together"}}).changed,"Identical local aliases conflicted");
    duplicate_edit=workspace::prepare_drawing_title_edit(duplicate,sheet_id,&live,directory/"title.drwz",row);
    try{workspace::edit_drawing_title(duplicate,&live,duplicate_edit,{{"LOCAL","One"},{"LOCAL_ALIAS","Two"}});throw std::logic_error("Conflicting local aliases accepted");}catch(const workspace::DrawingOperationError& error){require(error.code=="invalid_arguments","Wrong alias conflict error");}
    require(duplicate.find_sheet(sheet_id)->local_parameters.at("note")=="Together","Conflicting aliases partially changed local parameters");
    auto values=workspace::user_parameters(live,second);values.order.push_back("DIVISOR");values.values["DIVISOR"][""]="2";values.order.push_back("VOLUME");values.values["VOLUME"][""]="3000";
    workspace::set_user_parameters(live,second,std::move(values));
    auto physical=live.open_part(second)->session.document();physical.relations.push_back({"VOLUME","model.volume / DIVISOR"});live.open_part(second)->session.commit(std::move(physical),live.open_part(second)->session.calculated_boundaries());
    auto with_field=live.open_drawing(doc.document_id)->document();with_field.find_sheet(sheet_id)->title_block_fields.push_back(field("DIVISOR","&DIVISOR",true));live.open_drawing(doc.document_id)->commit(std::move(with_field));
    run(host,"drawing.title.set",{{"sheet",sheet_id},{"bom_row",row},{"values",{{"DIVISOR","4"}}}});
    for(const auto& s:live.open_drawing(doc.document_id)->document().sheets)require(std::stod(s.bom_rows[1].parameters.at("VOLUME"))==1500,"BOM mirror used parameters before relation evaluation");
    auto* source=live.open_part(second);auto related=source->session.document();related.relations.push_back({"revision","1"});source->session.commit(std::move(related),source->session.calculated_boundaries());
    const auto protected_revision=source->session.revision(),drawing_revision=live.open_drawing(doc.document_id)->revision();
    const auto readonly=run(host,"drawing.title.get",{{"sheet",sheet_id},{"bom_row",row}}).data;
    for(const auto& f:readonly.at("fields"))if(f.at("field")=="parameter:revision")require(f.at("writable")==false,"Calculated parameter was writable");
    const auto calculated=host.execute({{"command","drawing.title.set"},{"arguments",{{"sheet",sheet_id},{"bom_row",row},{"values",{{"NAME","Partial"},{"parameter:revision","2"}}}}}});
    require(!calculated.ok&&calculated.code=="read_only"&&live.open_part(second)->session.revision()==protected_revision&&live.open_drawing(doc.document_id)->revision()==drawing_revision,"Calculated parameter batch partially committed");
    auto changed_owner=live.open_assembly(owner)->session.document();changed_owner.components.pop_back();live.open_assembly(owner)->session.commit(std::move(changed_owner));
    const auto missing=host.execute({{"command","drawing.title.set"},{"arguments",{{"sheet",sheet_id},{"bom_row",row},{"values",{{"NAME","Wrong source"}}}}}});
    require(!missing.ok&&missing.code=="source_changed"&&live.open_part(second)->session.revision()==protected_revision,"Changed BOM row retargeted another source");

}
}
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-title-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"BOM identities, title fields, source writes, atomic validation and history passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

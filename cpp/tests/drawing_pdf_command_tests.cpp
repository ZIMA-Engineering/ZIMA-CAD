#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <QGuiApplication>
#include <QFile>
#include <QRegularExpression>
#include <iostream>
#include <chrono>
#include "../modules/drawing_render/src/sheet_export_context.hpp"
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()){
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
QByteArray read(const fs::path& path){QFile file(QString::fromStdString(document::path_to_utf8(path)));require(file.open(QIODevice::ReadOnly),"Cannot read PDF");return file.readAll();}
void verify_export_contexts(const fs::path& directory) {
    auto part=document::PartDocument::create_default();
    for(int i=0;i<128;++i)part.user_parameters["property"+std::to_string(i)]=std::string(120,'a'+i%26);
    const auto source=directory/"metadata-source.prtz";part.save(source);
    auto doc=drawing::DrawingDocument::create_default();doc.source_document_id=part.document_id;doc.source_path=source;
    while(doc.sheets.size()<32)doc.sheets.push_back(drawing::DrawingDocument::create_default().sheets.front());
    std::vector<drawing::TitleBlockContext> baseline;
    const auto start=std::chrono::steady_clock::now();
    for(std::size_t i=0;i<doc.sheets.size();++i)baseline.push_back(drawing_render::sheet_export_context(doc,i,{},nullptr));
    const auto middle=std::chrono::steady_clock::now();
    drawing_render::ExportSourceContexts sources;
    for(std::size_t i=0;i<doc.sheets.size();++i) {
        const auto context=drawing_render::sheet_export_context(doc,i,{},nullptr,&sources);
        const auto& expected=baseline[i];
        require(context.parameters==expected.parameters&&context.parameter_values==expected.parameter_values&&
            context.parameter_labels==expected.parameter_labels&&context.parameter_order==expected.parameter_order&&
            context.parameter_aliases==expected.parameter_aliases&&context.file_stem==expected.file_stem&&
            context.mass_unit==expected.mass_unit&&context.sheet_index==expected.sheet_index&&context.sheet_count==32,
            "Export context reuse changed source fields or per-page numbering");
    }
    const auto end=std::chrono::steady_clock::now();
    require(sources.size()==1,"Export reread one source for every page");
    std::cout<<"32 sheet metadata: uncached_ms="<<std::chrono::duration<double,std::milli>(middle-start).count()
        <<" cached_ms="<<std::chrono::duration<double,std::milli>(end-middle).count()<<" sources="<<sources.size()<<"\n";
    part.user_parameters["property0"]="Changed between exports";part.save(source);
    drawing_render::ExportSourceContexts next;
    require(drawing_render::sheet_export_context(doc,0,{},nullptr,&next).parameters.at("property0")=="Changed between exports",
        "A later export reused stale source metadata");
}
void verify(){
    auto directory=fs::absolute("Projects/test/drawing-pdf-command");fs::create_directories(directory);verify_export_contexts(directory);
    workspace::Workspace live;kernel::OcctKernel kernel;command_host::Host host(live,kernel,directory);
    auto part=document::PartDocument::create_default();part.user_parameters["name"]="Neuložený díl";part.user_parameter_labels["name"]["cs"]="Název";part.user_parameter_values["name"][""]="Neuložený díl";live.add_part(part,{},directory/"unsaved.prtz");
    auto doc=drawing::DrawingDocument::create_default();doc.name="PDF drawing";doc.source_document_id=part.document_id;doc.source_path=directory/"unsaved.prtz";
    drawing::TemplateText heading;heading.text="PDF ONE";heading.position={170,270};heading.height=8;heading.flipped=true;doc.sheets.front().frame_texts={heading};
    auto second=drawing::DrawingDocument::create_default().sheets.front();second.format=drawing::SheetFormat::A3;heading.text="PDF TWO";second.frame_texts={heading};doc.sheets.push_back(second);
    live.add_drawing(doc,directory/"drawing.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    for(const auto& sheet:doc.sheets)run(host,"drawing.title_block.load",{{"sheet",sheet.id},{"path",document::path_to_utf8(fs::absolute("tests/fixtures/drawing-library/ZE-TITLE-BLOCK-CS.tblz"))}});
    const auto revision=live.open_drawing(doc.document_id)->revision(),generation=live.open_drawing(doc.document_id)->data_generation();const auto count=live.size();
    const auto output=directory/fs::path(u8"výkres.pdf");
    const auto exported=run(host,"export.pdf",{{"path",document::path_to_utf8(output)},{"overwrite",true}}).data;
    const auto bytes=read(output);require(bytes.startsWith("%PDF-")&&bytes.size()>1000&&exported.at("pages")==2&&exported.at("bytes")==bytes.size(),"PDF export report or header invalid");
    require(live.open_drawing(doc.document_id)->revision()==revision&&live.open_drawing(doc.document_id)->data_generation()==generation&&live.open_part(part.document_id)->session.revision()==0&&live.size()==count,"PDF export changed native data or opened sources");
    const auto reject=[&](Json args,const char* code){const auto result=host.execute({{"command","export.pdf"},{"arguments",args}});require(!result.ok&&result.code==code,"Invalid PDF request accepted or misclassified");require(read(output)==bytes,"Failed PDF export replaced the existing file");};
    reject({{"path",document::path_to_utf8(output)}},"file_exists");
    reject({{"path","wrong.prtz"}},"unsupported_format");
    reject({{"path","missing/output.pdf"}},"invalid_directory");
    auto broken=live.open_drawing(doc.document_id)->document();auto view=drawing::DrawingDocument::create_view("missing",directory/"missing.asmz",{});broken.sheets[1].views={view};live.open_drawing(doc.document_id)->commit(std::move(broken));
    reject({{"path",document::path_to_utf8(output)},{"overwrite",true}},"export_failed");
    for(const auto& entry:fs::directory_iterator(directory))require(!entry.path().filename().string().starts_with(".zima-export-"),"Failed PDF export left a staging directory");
    live.activate(part.document_id);require(!host.execute({{"command","export.pdf"},{"arguments",{{"path","part.pdf"}}}}).ok,"PDF accepted a Part as Drawing");
    std::cout<<"Two PDF sheets, UTF-8, source metadata, unchanged history and atomic publication passed\n";
}
}
int main(int argc,char** argv){QGuiApplication app(argc,argv);try{verify();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

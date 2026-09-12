#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <QGuiApplication>
#include <QFile>
#include <QRegularExpression>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()){
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
QByteArray read(const fs::path& path){QFile file(QString::fromStdString(document::path_to_utf8(path)));require(file.open(QIODevice::ReadOnly),"Cannot read PDF");return file.readAll();}
void verify(){
    auto directory=fs::absolute("Projects/test/drawing-pdf-command");fs::create_directories(directory);
    workspace::Workspace live;kernel::OcctKernel kernel;command_host::Host host(live,kernel,directory);
    auto part=document::PartDocument::create_default();part.user_parameters["name"]="Neuložený díl";part.user_parameter_labels["name"]["cs"]="Název";part.user_parameter_values["name"][""]="Neuložený díl";live.add_part(part,{},directory/"unsaved.prtz");
    auto doc=drawing::DrawingDocument::create_default();doc.name="PDF drawing";doc.source_document_id=part.document_id;doc.source_path=directory/"unsaved.prtz";
    drawing::TemplateText heading;heading.text="PDF ONE";heading.position={170,270};heading.height=8;heading.flipped=true;doc.sheets.front().frame_texts={heading};
    auto second=drawing::DrawingDocument::create_default().sheets.front();second.format=drawing::SheetFormat::A3;heading.text="PDF TWO";second.frame_texts={heading};doc.sheets.push_back(second);
    live.add_drawing(doc,directory/"drawing.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    for(const auto& sheet:doc.sheets)run(host,"drawing.title_block.load",{{"sheet",sheet.id},{"path",document::path_to_utf8(fs::absolute("config/formats/ZE-RAZITKO.tblz"))}});
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

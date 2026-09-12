#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <QGuiApplication>
#include <QFile>
#include <QImage>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
QByteArray read(const fs::path& path){QFile file(QString::fromStdString(document::path_to_utf8(path)));require(file.open(QIODevice::ReadOnly),"Cannot read image");return file.readAll();}
void verify() {
    auto directory=fs::absolute("Projects/test/drawing-image-command");fs::create_directories(directory);
    workspace::Workspace live;kernel::OcctKernel kernel;command_host::Host host(live,kernel,directory);
    auto doc=drawing::DrawingDocument::create_default();
    doc.sheets.front().frame_lines={{{170,277},{140,277},drawing::DrawingPen::Red}};
    drawing::TemplateText text;text.text="Výkres č. 1";text.position={160,247};text.height=4;doc.sheets.front().frame_texts={text};
    live.add_drawing(doc,directory/"drawing.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    const auto revision=live.open_drawing(doc.document_id)->revision(),generation=live.open_drawing(doc.document_id)->data_generation();
    const auto output=directory/fs::path(u8"výkres.png"),cropped=directory/fs::path(u8"výřez.png"),jpeg=directory/fs::path(u8"výřez.JPEG");
    const Json args={{"path",document::path_to_utf8(output)},{"sheet",doc.sheets.front().id},{"dpi",254},{"overwrite",true}};
    const auto report=run(host,"export.image",args).data;const auto bytes=read(output);const auto full=QImage::fromData(bytes).convertToFormat(QImage::Format_RGB32);
    require(full.size()==QSize(2100,2970)&&report.at("width_px")==2100&&report.at("height_px")==2970&&report.at("bytes")==bytes.size()&&report.at("model_changed")==false,"Full-sheet dimensions or receipt invalid");
    require(bytes.size()<2*1024*1024,"PNG disabled lossless compression for a sparse sheet");
    require(full.dotsPerMeterX()==10000&&full.dotsPerMeterY()==10000,"PNG lost physical resolution");
    require(qGray(full.pixel(550,200))<10&&qGray(full.pixel(550,250))>245,"Raster changed millimetre position or background");
    auto crop_args=args;crop_args["path"]=document::path_to_utf8(cropped);crop_args["crop_mm"]={30,10,60,40};
    run(host,"export.image",crop_args);const auto crop=QImage::fromData(read(cropped)).convertToFormat(QImage::Format_RGB32);
    require(crop.size()==QSize(600,400)&&crop==full.copy(300,100,600,400),"Crop moved, scaled or changed sheet content");
    crop_args["path"]=document::path_to_utf8(jpeg);run(host,"export.image",crop_args);
    const auto jpg=QImage::fromData(read(jpeg));require(jpg.size()==crop.size()&&qGray(jpg.pixel(250,100))<20,"JPEG codec unavailable or image geometry changed");
    require(live.open_drawing(doc.document_id)->revision()==revision&&live.open_drawing(doc.document_id)->data_generation()==generation&&live.size()==1,"Image export mutated the document or loaded sources");
    const auto reject=[&](Json request,const char* code){const auto result=host.execute({{"command","export.image"},{"arguments",request}});if(result.ok||result.code!=code)throw std::runtime_error(std::string("Image expected ")+code+" got "+result.code+": "+result.message);require(read(output)==bytes,"Failed image export replaced the existing file");};
    auto bad=args;bad.erase("overwrite");reject(bad,"file_exists");
    bad=args;bad["sheet"]="missing";reject(bad,"sheet_not_found");
    bad=args;bad["path"]="wrong.pdf";reject(bad,"unsupported_format");
    bad=args;bad["path"]="missing/out.png";reject(bad,"invalid_directory");
    for(const double dpi:{0.0,-1.0,2401.0}){bad=args;bad["dpi"]=dpi;reject(bad,"invalid_arguments");}
    bad=args;bad["dpi"]=2400;reject(bad,"image_too_large");
    for(const Json value:{Json::array({0,0,-1,2}),Json::array({-1,0,1,1}),Json::array({0,0,500,500}),Json::array({0,0,1}),Json::array({0,0,"x",1})}){bad=args;bad["crop_mm"]=value;reject(bad,"invalid_arguments");}
    bad=args;bad["quality"]=101;reject(bad,"invalid_arguments");
    auto broken=doc;broken.source_document_id="missing";broken.source_path="missing.asmz";live.open_drawing(doc.document_id)->commit(std::move(broken));reject(args,"export_failed");
    for(const auto& entry:fs::directory_iterator(directory))require(!entry.path().filename().string().starts_with(".zima-export-"),"Failed image export left staging data");
    auto part=document::PartDocument::create_default();live.add_part(part,{},directory/"part.prtz");live.activate(part.document_id);live.display_top_level(part.document_id);reject(args,"unsupported_document");
    std::cout<<"PNG/JPEG sheet dimensions, DPI, exact crop, memory limits, unchanged model and atomic errors passed\n";
}
}
int main(int argc,char** argv){QGuiApplication app(argc,argv);try{verify();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

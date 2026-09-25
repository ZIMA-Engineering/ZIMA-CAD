#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
std::string bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;command_host::Interaction interaction;
    options.settings=[]{return command_host::Settings{{fs::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body"},{}};};
    interaction.camera=Json::array({1,2,3});options.interaction=[&]{return interaction;};
    const auto owner_thread=std::this_thread::get_id();int captures{},writes{};bool empty{},fail_capture{},fail_io{};
    QImage expected(37,23,QImage::Format_ARGB32);expected.fill(qRgb(24,67,140));expected.setPixel(7,11,qRgb(210,100,19));
    options.capture_view=[&]{require(std::this_thread::get_id()==owner_thread,"Capture left the View thread");++captures;
        if(fail_capture)throw std::runtime_error("capture failed");return empty?QImage{}:expected;};
    command_host::Host* current{};
    options.run_io=[&](auto task){require(current->execute_text("documents").code=="busy","Export permitted reentrancy");
        if(fail_io)throw std::runtime_error("writer failed");
        std::async(std::launch::async,[&]{require(std::this_thread::get_id()!=owner_thread,"Encoding did not use worker");++writes;task();}).get();};
    command_host::Host host(live,kernel,directory,options);current=&host;
    const auto call=[&](const char* command,Json args=Json::object()){return host.execute({{"command",command},{"arguments",std::move(args)}});};
    const auto run=[&](const char* command,Json args=Json::object()){const auto r=call(command,std::move(args));if(!r.ok)throw std::runtime_error(r.code+": "+r.message);return r.data;};
    require(call("export.view",{{"path","empty.png"}}).code=="no_document"&&captures==0,"Empty host attempted a capture");
    run("new",{{"type","part"},{"name","view-source"}});const auto part=live.active_document_id();
    zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(n,std::move(a));},{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});run("save");
    const auto before=run("documents");const auto generation=live.open_part(part)->session.data_generation();
    const auto* cache=live.open_part(part)->session.calculated_boundaries().data();
    const auto png=directory/fs::path(u8"pohled žluťoučký.PNG");
    const auto result=run("export.view",{{"path",document::path_to_utf8(png.filename())}});
    require(result.at("document")==part&&result.at("displayed_document")==part&&result.at("camera")==interaction.camera&&result.at("model_changed")==false,"Capture context is wrong");
    require(result.at("width_px")==37&&result.at("height_px")==23&&result.at("bytes")==fs::file_size(png),"Capture report differs from file");
    require(QImage(QString::fromStdString(document::path_to_utf8(png))).convertToFormat(QImage::Format_ARGB32)==expected,"PNG changed pixels");
    const auto original=bytes(png);require(original.starts_with("\x89PNG"),"Not a PNG file");
    require(call("export.view",{{"path",document::path_to_utf8(png)}}).code=="file_exists"&&bytes(png)==original,"Overwrite guard failed");
    expected.fill(qRgb(90,40,15));run("export.view",{{"path",document::path_to_utf8(png)},{"overwrite",true}});
    require(bytes(png)!=original,"Explicit overwrite failed");
    run("export.view",{{"path","view.jpeg"},{"quality",85}});const QImage jpeg(QString::fromStdString(document::path_to_utf8(directory/"view.jpeg")));
    require(!jpeg.isNull()&&jpeg.size()==expected.size()&&bytes(directory/"view.jpeg").starts_with("\xff\xd8"),"Not a complete JPEG");
    const auto saved=bytes(png);const auto reject=[&](Json args,const char* code){const auto r=call("export.view",std::move(args));
        if(r.ok||r.code!=code)throw std::runtime_error(std::string("Expected ")+code+", got "+r.code);
        require(bytes(png)==saved&&!host.change()&&run("documents")==before,"Rejected capture changed model or existing file");};
    const auto count=captures;
    reject({{"path","view.prtz"}},"unsupported_format");reject({{"path","bad.png"},{"quality",101}},"invalid_arguments");
    reject({{"path","bad.png"},{"quality",1.5}},"invalid_arguments");reject({{"path","bad.png"},{"document","stale"}},"document_changed");
    require(captures==count,"Invalid parameters captured the View");
    interaction.editing=true;reject({{"path","bad.png"}},"editing_in_progress");interaction.editing=false;
    empty=true;reject({{"path",document::path_to_utf8(png)},{"overwrite",true}},"view_unavailable");empty=false;
    fail_capture=true;reject({{"path",document::path_to_utf8(png)},{"overwrite",true}},"export_failed");fail_capture=false;
    fail_io=true;reject({{"path",document::path_to_utf8(png)},{"overwrite",true}},"export_failed");fail_io=false;
    reject({{"path","missing/view.png"}},"invalid_directory");
    require(run("documents")==before&&live.open_part(part)->session.data_generation()==generation&&live.open_part(part)->session.calculated_boundaries().data()==cache,"Capture regenerated or modified source");
    command_host::Host headless(live,kernel,directory);
    require(headless.execute({{"command","export.view"},{"arguments",{{"path","headless.png"}}}}).code=="view_unavailable"&&!fs::exists(directory/"headless.png"),"Headless host fabricated a View");
    run("new",{{"type","assembly"},{"name","view-assembly"}});const auto assembly=live.active_document_id();
    const auto occurrence=run("component.insert",{{"source",part}}).at("occurrence").get<std::string>();
    run("component.activate",{{"instance_path",assembly::InstancePath{}.child(occurrence).encoded()}});const auto active=run("documents");
    const auto assembly_image=run("export.view",{{"path","assembly.png"},{"document",part}});
    require(assembly_image.at("document")==part&&assembly_image.at("displayed_document")==assembly&&run("documents")==active,"Capture lost top-level scene or changed activation");
    run("component.deactivate");run("new",{{"type","drawing"},{"name","view-drawing"}});
    require(call("export.view",{{"path","drawing.png"}}).code=="unsupported_document"&&!fs::exists(directory/"drawing.png"),"Drawing captured a stale 3D View");
    for(const auto& entry:fs::directory_iterator(directory))require(!entry.path().filename().string().starts_with(".zima-export-"),"Export left a temporary stage");
    require(writes>=4,"Image encoding never ran");
}
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);try{QTemporaryDir temporary;require(temporary.isValid(),"No test directory");kernel::OcctKernel kernel;
    verify(kernel,fs::u8path(temporary.path().toStdString()));std::cout<<"View image pixels, thread boundary, active occurrence, atomic files and unchanged geometry passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

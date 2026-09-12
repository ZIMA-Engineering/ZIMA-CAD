#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <QGuiApplication>
#include <QFile>
#include <cmath>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
QByteArray read(const fs::path& path){QFile file(QString::fromStdString(document::path_to_utf8(path)));require(file.open(QIODevice::ReadOnly),"Cannot read DXF");return file.readAll();}
using Entity=std::map<int,QByteArray>;
std::vector<Entity> entities(const QByteArray& data) {
    const auto lines=data.split('\n');std::vector<Entity> result;bool section=false;
    for(int i=0;i+1<lines.size();i+=2) {
        const auto code=lines[i].toInt();const auto value=lines[i+1].trimmed();
        if(code==2&&value=="ENTITIES"){section=true;continue;}
        if(!section)continue;if(code==0&&value=="ENDSEC")break;
        if(code==0)result.push_back({});if(!result.empty())result.back()[code]=value;
    }
    return result;
}
bool near(double a,double b){return std::abs(a-b)<1e-7;}
void verify() {
    auto directory=fs::absolute("Projects/test/drawing-dxf-command");fs::create_directories(directory);
    workspace::Workspace live;kernel::OcctKernel kernel;command_host::Host host(live,kernel,directory);
    auto part=document::PartDocument::create_default();part.user_parameters["name"]="Šroub";part.user_parameter_values["name"][""]="Šroub";
    live.add_part(part,{},directory/"unsaved.prtz");
    auto doc=drawing::DrawingDocument::create_default();doc.source_document_id=part.document_id;doc.source_path="unsaved.prtz";
    drawing::TemplateText first_text;first_text.text="EXCLUDED";first_text.position={100,100};doc.sheets.front().frame_texts={first_text};
    auto sheet=drawing::DrawingDocument::create_default().sheets.front();sheet.format=drawing::SheetFormat::A3;
    sheet.frame_lines={{{50,30},{70,45},drawing::DrawingPen::Red}};
    sheet.frame_circles={{{100,50},5,drawing::DrawingPen::Green}};
    drawing::TemplateText text;text.text="VÝKRES";text.position={160,40};text.height=4;sheet.frame_texts={text};
    drawing::TitleBlockField field;field.id="NAME";field.expression="&name";field.position={160,50};sheet.title_block_fields={field};
    drawing::DrawingView view;view.id="view";view.x=100;view.y=100;view.scale=2;view.use_sheet_scale=false;
    view.source_document_id=part.document_id;view.source_path="unsaved.prtz";view.show_caption=false;view.display_style=drawing::DisplayStyle::HiddenEdges;
    drawing::ProjectedEdge edge;edge.points={{0,0},{10,0}};view.projected_edges.push_back(edge);
    edge.hidden=true;edge.points={{0,2},{10,2}};view.projected_edges.push_back(edge);sheet.views={view};
    doc.sheets.push_back(sheet);live.add_drawing(doc,directory/"drawing.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    const auto revision=live.open_drawing(doc.document_id)->revision(),generation=live.open_drawing(doc.document_id)->data_generation();const auto count=live.size();
    const auto output=directory/fs::path(u8"výkres.DXF");
    const auto args=Json{{"path",document::path_to_utf8(output)},{"sheet",sheet.id},{"overwrite",true}};
    const auto report=run(host,"export.dxf",args).data;const auto bytes=read(output);const auto parsed=entities(bytes);
    require(report.at("sheet")==sheet.id&&report.at("bytes")==bytes.size()&&report.at("model_changed")==false,"DXF receipt is invalid");
    require(bytes.contains("$INSUNITS\n70\n4\n")&&bytes.endsWith("0\nEOF\n"),"DXF units or termination invalid");
    require(bytes.contains("VÝKRES")&&bytes.contains("Šroub")&&!bytes.contains("EXCLUDED"),"DXF lost Unicode/live metadata or exported the wrong sheet");
    bool frame=false,visible=false,hidden=false;int circle_points=0;
    for(const auto& e:parsed)if(e.at(0)=="LINE") {
        const auto x=e.at(10).toDouble(),y=e.at(20).toDouble(),xx=e.at(11).toDouble(),yy=e.at(21).toDouble();
        if(near(x,370)&&near(y,30)&&near(xx,350)&&near(yy,45))frame=e.at(370)=="70";
        if(near(x,320)&&near(y,100)&&near(xx,340)&&near(yy,100))visible=e.at(370)=="50"&&e.at(6)=="CONTINUOUS";
        if(near(x,320)&&near(y,104)&&near(xx,340)&&near(yy,104))hidden=e.at(370)=="25"&&e.at(6)=="DASHED";
        if(std::abs(std::hypot(x-320,y-50)-5)<.01)++circle_points;
    }
    require(frame&&visible&&hidden&&circle_points>20,"DXF changed paper scale, axes, lineweights, hidden edges or circular outline");
    require(live.open_drawing(doc.document_id)->revision()==revision&&live.open_drawing(doc.document_id)->data_generation()==generation&&live.open_part(part.document_id)->session.revision()==0&&live.size()==count,"DXF mutated the model or opened sources");
    const auto reject=[&](Json request,const char* code){const auto result=host.execute({{"command","export.dxf"},{"arguments",request}});if(result.ok||result.code!=code)throw std::runtime_error(std::string("DXF expected ")+code+" got "+result.code+": "+result.message+" for "+request.dump());require(read(output)==bytes,"Rejected DXF replaced the existing file");};
    auto bad=args;bad.erase("overwrite");reject(bad,"file_exists");
    bad=args;bad.erase("sheet");reject(bad,"invalid_arguments");
    bad=args;bad["sketch"]="ambiguous";reject(bad,"invalid_arguments");
    bad=args;bad["sheet"]="missing";reject(bad,"sheet_not_found");
    bad=args;bad["path"]="wrong.pdf";reject(bad,"unsupported_format");
    bad=args;bad["path"]="missing/output.dxf";reject(bad,"invalid_directory");
    auto broken=doc;broken.sheets.back().views.front().source_document_id="missing";broken.sheets.back().views.front().source_path="missing.asmz";live.open_drawing(doc.document_id)->commit(std::move(broken));
    reject(args,"export_failed");
    for(const auto& entry:fs::directory_iterator(directory))require(!entry.path().filename().string().starts_with(".zima-export-"),"Failed DXF left staging data");
    live.activate(part.document_id);reject(args,"active_occurrence");
    live.display_top_level(part.document_id);reject(args,"invalid_arguments");
    std::cout<<"Drawing DXF sheet selection, millimetres, styles, UTF-8, live metadata and atomic errors passed\n";
}
}
int main(int argc,char** argv){QGuiApplication app(argc,argv);try{verify();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

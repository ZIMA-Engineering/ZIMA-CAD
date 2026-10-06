#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <QGuiApplication>
#include <QFile>
#include <QTemporaryDir>
#include <zima/drawing_render/dxf_export.hpp>
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
void verify_view_descriptions() {
    QTemporaryDir temp;require(temp.isValid(),"Description test directory unavailable");const auto dir=fs::u8path(temp.path().toStdString());
    workspace::Workspace live;auto part=document::PartDocument::create_default();part.user_parameters["REV"]="B";
    live.add_part(part,{},dir/"bracket.prtz");
    auto other=document::PartDocument::create_default();other.user_parameters["REV"]="X";live.add_part(other,{},dir/"other.prtz");
    auto doc=drawing::DrawingDocument::create_default();doc.source_document_id=part.document_id;doc.source_path="bracket.prtz";
    auto& sheet=doc.sheets.front();drawing::DrawingView view;view.id="description-view";view.name="CUTTING VIEW";view.source_document_id=part.document_id;view.source_path="bracket.prtz";
    view.show_caption=true;view.scale=.5;view.use_sheet_scale=false;
    drawing::ProjectedEdge edge;edge.points={{-10,0},{10,0}};edge.source={"profile","edge",{}};view.projected_edges={edge};
    view.description_rows={{drawing::ViewDescriptionKind::Text,true,"&document.file_stem.&REV",2.5,"#00ff00"},
        {drawing::ViewDescriptionKind::Scale,true,"",3.5,"#00ff00"},{drawing::ViewDescriptionKind::Name,false,"",5,"#ffffff"}};
    auto independent=view;independent.id="independent-view";independent.source_document_id=other.document_id;independent.source_path="other.prtz";independent.x+=80;independent.description_rows[1].visible=false;
    sheet.views={view,independent};doc.save(dir/"description.drwz");auto reopened=drawing::DrawingDocument::load(dir/"description.drwz");
    const auto export_text=[&](const auto& model,const char* file){static_cast<void>(drawing_render::export_dxf(model,sheet.id,dir/file,dir/"description.drwz",&live,true));return entities(read(dir/file));};
    const auto output=export_text(reopened,"description.dxf");std::vector<Entity> texts;
    for(const auto& e:output)if(e.at(0)=="TEXT")texts.push_back(e);
    require(texts.size()==3&&texts[0].at(1)=="bracket.B"&&texts[1].at(1)=="1:2"&&texts[2].at(1)=="other.X","Description visibility, independent parameter source or row order lost in DXF");
    require(near(texts[0].at(40).toDouble(),2.5)&&near(texts[1].at(40).toDouble(),3.5)&&texts[0].at(20).toDouble()>texts[1].at(20).toDouble(),"Description paper heights or vertical stacking changed in DXF");
    auto parameters=workspace::user_parameters(live,part.document_id);parameters.flat["REV"]="C";
    parameters.values["REV"][""]="C";require(workspace::set_user_parameters(live,part.document_id,std::move(parameters)),"Source revision parameter did not change");
    std::vector<Entity> current_texts;for(const auto& e:export_text(reopened,"description-current.dxf"))if(e.at(0)=="TEXT")current_texts.push_back(e);
    require(current_texts[0].at(1)=="bracket.C","Description did not resolve the current source parameter");
    require(current_texts[2].at(1)=="other.X","Description source change leaked into an independent view");
    auto moved=reopened;moved.sheets.front().views.front().x+=20;moved.sheets.front().views.front().y+=10;
    const auto shifted=export_text(moved,"description-moved.dxf");std::vector<Entity> moved_texts;
    for(const auto& e:shifted)if(e.at(0)=="TEXT")moved_texts.push_back(e);
    require(moved_texts[0].at(1)=="bracket.C"&&near(moved_texts[0].at(10).toDouble(),current_texts[0].at(10).toDouble()-20)&&near(moved_texts[0].at(20).toDouble(),current_texts[0].at(20).toDouble()+10),"Description failed view attachment");
}
// Exercise the complete annotation -> native file -> renderer -> export boundary.
// These are original curve references; paper scale must not scale measured values.
void verify_manufacturing_output(fs::path directory) {
    workspace::Workspace live;kernel::OcctKernel kernel;command_host::Host host(live,kernel,directory);
    auto part=document::PartDocument::create_default();live.add_part(part,{},directory/"annotation-source.prtz");
    kernel::ViewerMesh mesh;
    const kernel::EdgeReference inch{"profile","inch",{}},metric{"profile","metric",{}},vertical{"profile","vertical",{}};
    mesh.edges={{{{0,0,0},{25.4,0,0}},inch},{{{0,20,0},{10,20,0}},metric},{{{0,0,0},{0,25.4,0}},vertical}};
    auto view=drawing::DrawingDocument::create_view(part.document_id,"annotation-source.prtz",mesh,drawing::ViewOrientation::Top);
    view.camera={{1,0,0},{0,1,0},{0,0,1}};view.x=100;view.y=100;view.scale=2;view.use_sheet_scale=false;view.show_caption=false;
    auto doc=drawing::DrawingDocument::create_default();doc.source_document_id=part.document_id;doc.source_path="annotation-source.prtz";
    auto& sheet=doc.sheets.front();sheet.views={view};
    const auto length=[&](kernel::EdgeReference reference,const char* unit,int decimals,const char* tolerance,double offset) {
        auto d=drawing::make_drawing_dimension(view.id);
        d.attachments={{drawing::DimensionAttachmentKind::CurvePoint,reference,{},0},{drawing::DimensionAttachmentKind::CurvePoint,reference,{},1}};
        d.style.value_unit=unit;d.style.suffix=unit;d.style.decimals=decimals;d.style.keep_trailing_zeros=true;
        d.style.tolerance_mode="symmetric";d.style.symmetric_tolerance=tolerance;
        drawing::refresh_drawing_dimension(view,d);drawing::place_drawing_dimension(view,d,0,{20,offset});return d;
    };
    sheet.dimensions.push_back(length(inch,"in",4,"0.0005",-15));
    sheet.dimensions.push_back(length(metric,"mm",2,"0.01",55));
    auto angular=drawing::make_drawing_dimension(view.id,drawing::DrawingDimensionKind::Angular);
    angular.attachments={{drawing::DimensionAttachmentKind::Line,inch,{},.5},{drawing::DimensionAttachmentKind::Line,vertical,{},.5}};
    angular.style.value_unit="deg";angular.style.suffix="°";angular.style.decimals=2;angular.style.keep_trailing_zeros=true;
    angular.style.tolerance_mode="deviations";angular.style.upper_tolerance="0.10";angular.style.lower_tolerance="0.05";
    drawing::refresh_drawing_dimension(view,angular);drawing::place_drawing_dimension(view,angular,0,{30,30});sheet.dimensions.push_back(angular);
    // Inch Drawing annotations use a decimal point and omit a leading zero in
    // fractional tolerances; source unit switching must retain that convention.
    const std::vector<std::string> expected{"1.0000in ±.0005","10,00 ±0,01","90,00° +0,10 /-0,05"};
    const std::vector<double> values{25.4,10,90};
    const auto path=directory/"manufacturing.drwz";doc.save(path);
    auto loaded=drawing::DrawingDocument::load(path);
    require(loaded.sheets.front().dimensions==sheet.dimensions,"Native drawing changed manufacturing specifications or reference identities");
    live.add_drawing(loaded,path);live.activate(doc.document_id);live.display_top_level(doc.document_id);
    const auto revision=live.open_drawing(doc.document_id)->revision(),generation=live.open_drawing(doc.document_id)->data_generation();
    QByteArray baseline;
    for(const auto unit:{"mm","cm","m","in"})for(const auto angle:{"deg","rad"}) {
        auto settings=workspace::file_settings(live,part.document_id);settings.units["Length"]=unit;settings.units["Angle"]=angle;
        require(!workspace::set_file_settings(live,kernel,part.document_id,settings).calculated,"Source units recalculated geometry");
        const auto source_revision=live.open_part(part.document_id)->session.revision();
        for(std::size_t i=0;i<expected.size();++i) {
            const auto data=run(host,"drawing.dimension.get",{{"dimension",sheet.dimensions[i].id}}).data;
            if(!(data.at("state")=="resolved"&&near(data.at("measurements")[0].at("value").get<double>(),values[i])&&data.at("measurements")[0].at("text")==expected[i]))
                throw std::runtime_error("Source units changed the authoritative drawing nominal or tolerance: "+data.dump());
        }
        run(host,"export.dxf",{{"path","manufacturing.dxf"},{"sheet",sheet.id},{"overwrite",true}});
        const auto bytes=read(directory/"manufacturing.dxf");
        QByteArray text;
        for(const auto& e:entities(bytes))if(e.at(0)=="TEXT")text+=e.at(1);
        for(const auto& label:expected)require(text.contains(QByteArray::fromStdString(label)),"Exported DXF lost an authoritative nominal or tolerance");
        if(baseline.isEmpty())baseline=bytes;else require(bytes==baseline,"Source units changed exported annotation geometry or paper scale");
        run(host,"export.pdf",{{"path",std::string("manufacturing-")+unit+"-"+angle+".pdf"},{"overwrite",true}});
        require(live.open_drawing(doc.document_id)->revision()==revision&&live.open_drawing(doc.document_id)->data_generation()==generation&&
            live.open_drawing(doc.document_id)->document().sheets.front().dimensions==sheet.dimensions&&
            live.open_part(part.document_id)->session.revision()==source_revision&&live.size()==2,"Manufacturing output changed history, references or source ownership");
    }
}
void verify() {
    auto directory=fs::absolute("Projects/test/drawing-dxf-command");fs::create_directories(directory);verify_manufacturing_output(directory);
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
    for(const auto length:{"mm","cm","m","in"})for(const auto angle:{"deg","rad"}) {
        auto settings=workspace::file_settings(live,part.document_id);settings.units["Length"]=length;settings.units["Angle"]=angle;
        require(!workspace::set_file_settings(live,kernel,part.document_id,settings).calculated,"Drawing source-unit selection calculated geometry");
        const auto source_revision=live.open_part(part.document_id)->session.revision();
        run(host,"export.dxf",args);
        require(read(output)==bytes,"Source document units changed paper scale, sheet geometry or DXF units");
        require(live.open_drawing(doc.document_id)->revision()==revision&&live.open_drawing(doc.document_id)->data_generation()==generation&&
            live.open_part(part.document_id)->session.revision()==source_revision&&live.size()==count,
            "Drawing export changed history or opened source tabs after unit selection");
    }
    const auto reject=[&](Json request,const char* code){const auto result=host.execute({{"command","export.dxf"},{"arguments",request}});if(result.ok||result.code!=code)throw std::runtime_error(std::string("DXF expected ")+code+" got "+result.code+": "+result.message+" for "+request.dump());require(read(output)==bytes,"Rejected DXF replaced the existing file");};
    auto bad=args;bad.erase("overwrite");reject(bad,"file_exists");
    bad=args;bad.erase("sheet");reject(bad,"invalid_arguments");
    bad=args;bad["sketch"]="ambiguous";reject(bad,"invalid_arguments");
    bad=args;bad["sheet"]="missing";reject(bad,"sheet_not_found");
    bad=args;bad["path"]="wrong.pdf";reject(bad,"unsupported_format");
    bad=args;bad["path"]="missing/output.dxf";reject(bad,"invalid_directory");
    // A cropped view must export shortened geometry, not merely a visible outline.
    auto cropped=doc;auto& cv=cropped.sheets.back().views.front();
    cv.crop=drawing::ViewCrop{drawing::ViewCropShape::Circle,{5,0},{{2,2}}};
    auto second_crop=cv;second_crop.id="second-crop";second_crop.x=150;cropped.sheets.back().views.push_back(second_crop);
    live.open_drawing(doc.document_id)->commit(cropped);
    auto crop_args=args;crop_args["path"]="crop.dxf";run(host,"export.dxf",crop_args);
    bool clipped_line=false,second_clipped_line=false;
    for(const auto& e:entities(read(directory/"crop.dxf")))if(e.at(0)=="LINE"){
        const auto x=e.at(10).toDouble(),y=e.at(20).toDouble(),xx=e.at(11).toDouble(),yy=e.at(21).toDouble();
        if(near(y,100)&&near(yy,100)&&x>319&&xx<341&&std::abs(xx-x)>1){
            require(x>=326-.01&&xx<=334+.01,"DXF retained geometry outside the circular crop");clipped_line=true;
        }
        if(near(y,100)&&near(yy,100)&&near(x,276)&&near(xx,284))second_clipped_line=true;
    }
    require(clipped_line&&second_clipped_line,"DXF lost a cropped segment or retained another view clip");
    auto broken=doc;broken.sheets.back().views.front().source_document_id="missing";broken.sheets.back().views.front().source_path="missing.asmz";live.open_drawing(doc.document_id)->commit(std::move(broken));
    reject(args,"export_failed");
    for(const auto& entry:fs::directory_iterator(directory))require(!entry.path().filename().string().starts_with(".zima-export-"),"Failed DXF left staging data");
    live.activate(part.document_id);reject(args,"active_occurrence");
    live.display_top_level(part.document_id);reject(args,"invalid_arguments");
    std::cout<<"Drawing DXF sheet selection, millimetres, styles, UTF-8, live metadata and atomic errors passed\n";
}
}
int main(int argc,char** argv){QGuiApplication app(argc,argv);try{verify();verify_view_descriptions();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

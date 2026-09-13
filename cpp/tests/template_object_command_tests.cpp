#include <zima/command_host/host.hpp>
#include <zima/workspace/template_object_operations.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <QCoreApplication>
#include <QImage>
#include <QSvgRenderer>
#include <fstream>
#include <iostream>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string text(const fs::path& p){const auto raw=p.generic_u8string();return {raw.begin(),raw.end()};}
Json request(const char* command,Json args=Json::object()){return {{"command",command},{"arguments",std::move(args)}};}
void near(double a,double b){require(std::abs(a-b)<1e-9,"Incorrect template numeric result");}
void verify(fs::path directory) {
    QImage raster(4,2,QImage::Format_ARGB32);raster.fill(Qt::red);
    const auto png=directory/"logo.png",svg=directory/"logo.svg",bad=directory/"invalid.png";
    require(raster.save(QString::fromStdU16String(png.u16string())),"Cannot create PNG fixture");
    {std::ofstream f(svg);f<<R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 4 2"><rect width="4" height="2" fill="red"/></svg>)";}
    {std::ofstream f(bad);f<<"not an image";}
    workspace::Workspace live;kernel::OcctKernel kernel;command_host::Interaction interaction;command_host::Options options;options.interaction=[&]{return interaction;};
    command_host::Host host(live,kernel,directory,options);
    const auto run=[&](const char* command,Json args=Json::object()){auto result=host.execute(request(command,std::move(args)));if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result.data;};
    const auto reject=[&](const char* command,Json args,const char* code){const auto id=live.active_document_id();const auto before=workspace::drawing_template_sketch(live,id).serialized();const auto rev=live.open_part(id)->session.revision();
        const auto result=host.execute(request(command,std::move(args)));if(result.ok||result.code!=code)throw std::runtime_error(std::string(command)+" expected "+code+", got "+result.code+": "+result.message);
        require(workspace::drawing_template_sketch(live,id).serialized()==before&&live.open_part(id)->session.revision()==rev&&!host.change(),"Rejected template operation mutated the model");};
    run("template.new",{{"kind","drawing_format"},{"name","Frame"}});
    reject("template.image.create",{{"path",text(png)},{"x_mm",0},{"y_mm",0}},"unsupported_document");
    reject("template.region.create",{{"x_mm",0},{"y_mm",0},{"width_mm",20},{"height_mm",4}},"unsupported_document");
    run("close",{{"discard",true}});
    const auto created=run("template.new",{{"kind","title_block"},{"name","Title"}});const std::string id=created.at("document");
    auto* state=live.open_part(id);const auto sketch=[&](){return workspace::drawing_template_sketch(live,live.active_document_id());};
    require(run("template.image.list").at("total")==0&&run("template.region.list").at("total")==0,"New template has objects");
    auto image=run("template.image.create",{{"path",text(png)},{"x_mm",10},{"y_mm",20}});const std::string image_id=image.at("image");
    require(image.at("format")=="png"&&!image.contains("data_base64")&&image.at("body_calculated")==false,"Image result lost format or leaked encoded content");
    near(image.at("width_mm"),30);near(image.at("height_mm"),15);require(image.at("corners_mm")==Json::array({Json::array({10,35}),Json::array({-20,35}),Json::array({-20,20}),Json::array({10,20})}),"Image corners use incorrect template coordinates");
    const auto original=sketch().serialized();run("undo");require(sketch().drawing_template->images.empty(),"Image Create Undo failed");run("redo");require(sketch().serialized()==original,"Image Redo changed content or ID");
    image=run("template.image.set",{{"image",image_id},{"height_mm",12},{"horizontal","center"},{"vertical","middle"},{"value_locks",{"x"}}});
    near(image.at("width_mm"),24);near(image.at("height_mm"),12);require(image.at("corners_mm")[0]==Json::array({22,26}),"Image alignment changed wrong corner");
    const auto revision=state->session.revision();require(run("template.image.set",{{"image",image_id},{"height_mm",12}}).at("changed")==false&&state->session.revision()==revision&&!host.change(),"Image no-op added Undo");
    reject("template.image.set",{{"image",image_id},{"x_mm",11}},"parameter_locked");
    run("template.image.set",{{"image",image_id},{"value_locks",{"width"}},{"x_mm",11.123456789}});
    reject("template.image.set",{{"image",image_id},{"height_mm",14}},"parameter_locked");
    reject("template.image.set",{{"image",image_id},{"width_mm",40}},"parameter_locked");
    reject("template.image.set",{{"image",image_id},{"value_locks",{"bogus"}}},"unknown_parameter");
    reject("template.image.set",{{"image",image_id},{"value_locks",{1}}},"invalid_arguments");
    reject("template.image.set",{{"image",image_id},{"value_locks",Json::array()},{"width_mm",20},{"height_mm",20}},"invalid_arguments");
    run("template.image.set",{{"image",image_id},{"value_locks",Json::array()},{"width_mm",20},{"height_mm",20},{"lock_aspect",false}});
    image=run("template.image.set",{{"image",image_id},{"path",text(svg)},{"lock_aspect",true}});near(image.at("height_mm"),10);
    require(image.at("image")==image_id&&image.at("format")=="svg"&&run("template.image.list").at("items").front().at("image")==image_id,"Image replacement lost identity");
    reject("template.image.set",{{"image",image_id},{"path",text(bad)}},"template_rejected");
    reject("template.image.set",{{"image",image_id},{"width_mm",-1}},"template_rejected");
    reject("template.image.get",{{"image","missing"}},"object_not_found");
    auto region=run("template.region.create",{{"x_mm",-10},{"y_mm",2},{"width_mm",20},{"height_mm",4}});const std::string region_id=region.at("region");near(region.at("step_mm"),4);
    run("undo");require(sketch().drawing_template->repeat_regions.empty(),"Region Create Undo failed");run("redo");
    region=run("template.region.set",{{"region",region_id},{"step_mm",7.123456789},{"direction","left"},{"value_locks",{"step"}}});
    near(region.at("step_mm"),7.123456789);require(region.at("direction")=="left","Region direction was lost");
    reject("template.region.set",{{"region",region_id},{"step_mm",8}},"parameter_locked");
    reject("template.region.set",{{"region",region_id},{"direction","diagonal"}},"template_rejected");
    reject("template.region.set",{{"region",region_id},{"width_mm",0}},"template_rejected");
    reject("template.region.set",{{"region",region_id},{"value_locks",{"z"}}},"unknown_parameter");
    const auto rev=state->session.revision();require(run("template.region.set",{{"region",region_id},{"direction","left"}}).at("changed")==false&&state->session.revision()==rev&&!host.change(),"Region no-op added history");
    require(run("template.region.list").at("items").front().at("region")==region_id,"Region list lost identity");
    reject("template.region.get",{{"region","missing"}},"object_not_found");
    interaction.editing=true;reject("template.image.remove",{{"image",image_id}},"editing_in_progress");interaction.template_editor_ready=true;
    const auto snapshot=sketch();require(run("template.image.remove",{{"image",image_id}}).at("changed")==true,"Image removal did nothing");
    require(run("template.image.remove",{{"image",image_id}}).at("changed")==false&&!host.change(),"Missing image removal mutated");run("undo");
    require(sketch().drawing_template->images==snapshot.drawing_template->images,"Image removal Undo lost data");
    run("template.region.remove",{{"region",region_id}});require(run("template.region.remove",{{"region",region_id}}).at("changed")==false&&!host.change(),"Missing region removal mutated");run("undo");
    require(sketch().drawing_template->repeat_regions==snapshot.drawing_template->repeat_regions,"Region removal Undo lost data");
    require(state->session.calculated_boundaries().empty(),"Template objects calculated a body");
    run("template.save");run("close");interaction={};fs::remove(png);fs::remove(svg);
    run("template.open",{{"path","Title.tblz"}});const auto loaded=sketch();
    require(loaded.drawing_template->images==snapshot.drawing_template->images&&loaded.drawing_template->repeat_regions==snapshot.drawing_template->repeat_regions,"Native reopen lost objects, content, locks or geometry");
    const auto& content=loaded.drawing_template->images.front().data_base64;
    QSvgRenderer renderer(QByteArray::fromBase64(QByteArray::fromStdString(content)));require(renderer.isValid(),"Embedded SVG requires source file after reopening");
    require(run("template.image.get",{{"image",image_id}}).at("format")=="svg"&&run("template.region.get",{{"region",region_id}}).at("value_locks")==Json::array({"step"}),"Read reopened objects lost metadata");
}
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);try{const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-template-objects-"+document::PartDocument::create_default().document_id);require(fs::create_directory(dir),"Cannot create fixture directory");verify(dir);require(dir.parent_path()==root,"Invalid cleanup root");fs::remove_all(dir);std::cout<<"Template images and regions: geometry, locks, replacement, errors, native files and Undo/Redo passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

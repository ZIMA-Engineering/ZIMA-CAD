#include <zima/sketcher/template_image_json.hpp>
#include <zima/sketcher/template_image.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace zima::sketcher {
void TemplateImage::validate() const {
    if((format!="png" && format!="svg") || id.empty() || data_base64.empty() || data_base64.size()>32*1024*1024 ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(rotation_degrees) || !std::isfinite(opacity) || opacity<0 || opacity>1 ||
        width<=0 || height<=0 || pixel_width<=0 || pixel_height<=0 ||
        (format=="png" && pixel_width*pixel_height>32'000'000) || !std::isfinite(pixel_width) || !std::isfinite(pixel_height) ||
        (horizontal!="left" && horizontal!="center" && horizontal!="right") ||
        (vertical!="bottom" && vertical!="middle" && vertical!="top"))
        throw std::runtime_error("Neplatný obrázek razítka nebo jeho rozměry.");
}
std::array<std::array<double,2>,4> TemplateImage::corners() const {
    const double left=template_coordinates
        ?x+(horizontal=="right"?width:horizontal=="center"?width/2:0)
        :x-(horizontal=="right"?width:horizontal=="center"?width/2:0);
    const double bottom=y-(vertical=="top"?height:vertical=="middle"?height/2:0);
    std::array<std::array<double,2>,4> result=template_coordinates
        ?std::array<std::array<double,2>,4>{{{left,bottom+height},{left-width,bottom+height},{left-width,bottom},{left,bottom}}}
        :std::array<std::array<double,2>,4>{{{left,bottom+height},{left+width,bottom+height},{left+width,bottom},{left,bottom}}};
    const double angle=rotation_degrees*std::numbers::pi/180.0;
    const double cosine=std::cos(angle),sine=std::sin(angle);
    for(auto& point:result) {
        const double dx=point[0]-x,dy=point[1]-y;
        point={x+dx*cosine-dy*sine,y+dx*sine+dy*cosine};
    }
    return result;
}
void to_json(nlohmann::json& j,const TemplateImage& i) {
    i.validate();j={{"id",i.id},{"name",i.name},{"data_base64",i.data_base64},{"format",i.format},
        {"x",i.x},{"y",i.y},{"width",i.width},{"height",i.height},
        {"pixel_width",i.pixel_width},{"pixel_height",i.pixel_height},
        {"horizontal",i.horizontal},{"vertical",i.vertical},{"lock_aspect",i.lock_aspect},{"value_locks",i.value_locks}};
    if(i.rotation_degrees!=0)j["rotation_degrees"]=i.rotation_degrees;
    if(i.opacity!=1)j["opacity"]=i.opacity;
    if(!i.visible)j["visible"]=false;
    if(!i.template_coordinates)j["template_coordinates"]=false;
    if(i.selection_locked)j["selection_locked"]=true;
}
void from_json(const nlohmann::json& j,TemplateImage& i) {
    i.id=j.at("id");i.name=j.at("name");i.data_base64=j.at("data_base64");i.format=j.at("format");
    i.x=j.at("x");i.y=j.at("y");i.width=j.at("width");i.height=j.at("height");
    i.pixel_width=j.at("pixel_width");i.pixel_height=j.at("pixel_height");
    i.horizontal=j.at("horizontal");i.vertical=j.at("vertical");i.lock_aspect=j.at("lock_aspect");i.value_locks=j.value("value_locks",std::set<std::string>{});
    i.rotation_degrees=j.value("rotation_degrees",0.0);i.opacity=j.value("opacity",1.0);
    i.visible=j.value("visible",true);i.template_coordinates=j.value("template_coordinates",true);
    i.selection_locked=j.value("selection_locked",false);i.validate();
}
} // namespace zima::sketcher

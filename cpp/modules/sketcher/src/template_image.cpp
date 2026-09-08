#include <zima/sketcher/template_image_json.hpp>
#include <zima/sketcher/template_image.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
namespace zima::sketcher {
void TemplateImage::validate() const {
    if((format!="png" && format!="svg") || id.empty() || data_base64.empty() || data_base64.size()>32*1024*1024 ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        width<=0 || height<=0 || pixel_width<=0 || pixel_height<=0 ||
        (format=="png" && pixel_width*pixel_height>32'000'000) || !std::isfinite(pixel_width) || !std::isfinite(pixel_height) ||
        (horizontal!="left" && horizontal!="center" && horizontal!="right") ||
        (vertical!="bottom" && vertical!="middle" && vertical!="top"))
        throw std::runtime_error("Neplatný obrázek razítka nebo jeho rozměry.");
}
std::array<std::array<double,2>,4> TemplateImage::corners() const {
    const double left=x+(horizontal=="right"?width:horizontal=="center"?width/2:0);
    const double bottom=y-(vertical=="top"?height:vertical=="middle"?height/2:0);
    return {{{left,bottom+height},{left-width,bottom+height},{left-width,bottom},{left,bottom}}};
}
void to_json(nlohmann::json& j,const TemplateImage& i) {
    i.validate();j={{"id",i.id},{"name",i.name},{"data_base64",i.data_base64},{"format",i.format},
        {"x",i.x},{"y",i.y},{"width",i.width},{"height",i.height},
        {"pixel_width",i.pixel_width},{"pixel_height",i.pixel_height},
        {"horizontal",i.horizontal},{"vertical",i.vertical},{"lock_aspect",i.lock_aspect},{"value_locks",i.value_locks}};
}
void from_json(const nlohmann::json& j,TemplateImage& i) {
    i.id=j.at("id");i.name=j.at("name");i.data_base64=j.at("data_base64");i.format=j.at("format");
    i.x=j.at("x");i.y=j.at("y");i.width=j.at("width");i.height=j.at("height");
    i.pixel_width=j.at("pixel_width");i.pixel_height=j.at("pixel_height");
    i.horizontal=j.at("horizontal");i.vertical=j.at("vertical");i.lock_aspect=j.at("lock_aspect");i.value_locks=j.value("value_locks",std::set<std::string>{});i.validate();
}
} // namespace zima::sketcher

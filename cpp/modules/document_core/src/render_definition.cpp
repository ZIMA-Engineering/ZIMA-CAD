#include <zima/document/render_definition.hpp>
#include <zima/document/placement_json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace zima::document {
namespace {
[[noreturn]] void invalid() { throw std::invalid_argument("Invalid Render definition."); }
bool range(double value,double low,double high) {
    return std::isfinite(value)&&value>=low&&value<=high;
}
bool color(const std::array<double,3>& value) {
    return std::ranges::all_of(value,[](double v){return range(v,0,1);});
}
unsigned integer(const nlohmann::json& j,unsigned low,unsigned high) {
    if(!j.is_number_integer()||!range(j.get<double>(),low,high))invalid();
    return j.get<unsigned>();
}
const char* kind_name(RenderObjectKind kind) {
    switch(kind) {
    case RenderObjectKind::Camera:return "camera";
    case RenderObjectKind::PointLight:return "point-light";
    case RenderObjectKind::Sun:return "sun";
    }
    invalid();
}
}

void validate_render_object(const RenderObject& value) {
    if(value.id.empty()||value.name.empty())throw std::invalid_argument("Specify a nonempty object name.");
    static_cast<void>(kind_name(value.kind));
    const auto& p=value.placement;
    for(double n:{p.x,p.y,p.z,p.rotation_x,p.rotation_y,p.rotation_z,
            p.absolute_rotation_x,p.absolute_rotation_y,p.absolute_rotation_z,
            p.rotation_offset_x,p.rotation_offset_y,p.rotation_offset_z})
        if(!std::isfinite(n))invalid();
    for(const auto& r:p.references)
        if(r.owner_id.empty()||r.semantic_key.empty()||!r.instance_path.empty()||
                r.owner_id==value.id||r.owner_id==value.id+":origin"||!std::isfinite(r.offset))invalid();
    if(!range(value.field_of_view_degrees,1,175)||!range(value.orthographic_height_mm,1e-6,1e12)||
            !color(value.color)||!range(value.intensity,0,1e15)||
            !range(value.radius_mm,0,1e12)||!range(value.angular_radius_degrees,0,45))invalid();
}

bool same_render_scene(const RenderDefinition& a,const RenderDefinition& b) {
    return a.source_ids==b.source_ids&&a.visible_bodies==b.visible_bodies&&
        a.active_camera_id==b.active_camera_id&&a.objects==b.objects&&a.settings==b.settings;
}
void validate_render_images(const std::vector<RenderImage>& images) {
    std::set<std::string> ids;
    for(const auto& image:images) {
        if(image.id.empty()||image.name.empty()||image.created_utc.empty()||!ids.insert(image.id).second||
            !image.width||!image.height||image.width>8192||image.height>8192||
            std::uint64_t(image.width)*image.height>33554432||!image.data||
            image.data->png_base64.empty()||image.data->png_base64.size()>180*1024*1024||
            image.data->thumbnail_base64.empty()||image.data->thumbnail_base64.size()>1024*1024)
            throw std::invalid_argument("Invalid retained Render image.");
    }
}
void validate_render_definition(const RenderDefinition& value) {
    validate_render_images(value.images);
    if(!value.visible_bodies&&value.source_ids.empty())throw std::invalid_argument("Select at least one Render source Body.");
    std::set<std::string> sources;
    for(const auto& id:value.source_ids)if(id.empty()||!sources.insert(id).second)invalid();
    std::set<std::string> ids;
    for(const auto& object:value.objects) {
        validate_render_object(object);
        if(!ids.insert(object.id).second)invalid();
        if(object.id==value.active_camera_id) {
            if(object.kind!=RenderObjectKind::Camera)invalid();
        }
    }
    // An empty active camera is a repairable scene, never an implicit camera
    // switch. Removing the active camera preserves that explicit missing ID.
    for(const auto& object:value.objects)for(const auto& reference:object.placement.references)
        if(ids.contains(reference.owner_id)||std::ranges::any_of(value.objects,[&](const auto& other){
            return reference.owner_id==other.id+":origin";
        }))invalid();
    const auto& s=value.settings;
    if(s.width<16||s.height<16||s.width>8192||s.height>8192||
            static_cast<std::uint64_t>(s.width)*s.height>33554432||
            s.samples<1||s.samples>4096||s.bounces<1||s.bounces>12||
            !range(s.exposure_ev,-24,24)||!range(s.environment_strength,0,100)||
            !range(s.roughness,0,1)||!range(s.metallic,0,1)||
            !color(s.background_top)||!color(s.background_bottom))invalid();
}

void to_json(nlohmann::json& j,const RenderDefinition& value) {
    validate_render_definition(value);
    const auto& s=value.settings;
    j={{"sources",value.source_ids},{"visible_bodies",value.visible_bodies},{"active_camera",value.active_camera_id},
        {"objects",nlohmann::json::array()},{"images",nlohmann::json::array()},
        {"settings",{{"width",s.width},{"height",s.height},{"samples",s.samples},{"bounces",s.bounces},
            {"exposure_ev",s.exposure_ev},{"environment_strength",s.environment_strength},
            {"use_model_finish",s.use_model_finish},{"roughness",s.roughness},{"metallic",s.metallic},
            {"background_top",s.background_top},{"background_bottom",s.background_bottom}}}};
    for(const auto& image:value.images)j["images"].push_back({{"id",image.id},{"name",image.name},
        {"created_utc",image.created_utc},{"width",image.width},{"height",image.height},
        {"png_base64",image.data->png_base64},{"thumbnail_base64",image.data->thumbnail_base64}});
    for(const auto& o:value.objects)j["objects"].push_back({{"id",o.id},{"name",o.name},
        {"kind",kind_name(o.kind)},{"placement",o.placement},{"visible",o.visible},{"enabled",o.enabled},
        {"field_of_view_degrees",o.field_of_view_degrees},{"orthographic",o.orthographic},
        {"orthographic_height_mm",o.orthographic_height_mm},{"color",o.color},{"intensity",o.intensity},
        {"radius_mm",o.radius_mm},{"angular_radius_degrees",o.angular_radius_degrees}});
}

void from_json(const nlohmann::json& j,RenderDefinition& value) {
    RenderDefinition next;
    next.source_ids=j.at("sources").get<std::vector<std::string>>();next.visible_bodies=j.at("visible_bodies");next.active_camera_id=j.at("active_camera");
    const auto& s=j.at("settings");auto& v=next.settings;
    v.width=integer(s.at("width"),16,8192);v.height=integer(s.at("height"),16,8192);
    v.samples=integer(s.at("samples"),1,4096);v.bounces=integer(s.at("bounces"),1,12);
    v.exposure_ev=s.at("exposure_ev");v.environment_strength=s.at("environment_strength");
    v.use_model_finish=s.at("use_model_finish");v.roughness=s.at("roughness");v.metallic=s.at("metallic");
    v.background_top=s.at("background_top").get<std::array<double,3>>();
    v.background_bottom=s.at("background_bottom").get<std::array<double,3>>();
    if(!j.at("objects").is_array())invalid();
    for(const auto& item:j.at("objects")) {
        RenderObject o;o.id=item.at("id");o.name=item.at("name");
        const auto kind=item.at("kind").get<std::string>();
        if(kind=="camera")o.kind=RenderObjectKind::Camera;
        else if(kind=="point-light")o.kind=RenderObjectKind::PointLight;
        else if(kind=="sun")o.kind=RenderObjectKind::Sun;
        else invalid();
        o.placement=item.at("placement").get<Placement>();o.visible=item.at("visible");o.enabled=item.at("enabled");
        o.field_of_view_degrees=item.at("field_of_view_degrees");o.orthographic=item.at("orthographic");
        o.orthographic_height_mm=item.at("orthographic_height_mm");o.color=item.at("color").get<std::array<double,3>>();
        o.intensity=item.at("intensity");o.radius_mm=item.at("radius_mm");o.angular_radius_degrees=item.at("angular_radius_degrees");
        next.objects.push_back(std::move(o));
    }
    if(!j.at("images").is_array())invalid();
    for(const auto& item:j.at("images")) {
        RenderImage image;image.id=item.at("id");image.name=item.at("name");image.created_utc=item.at("created_utc");
        image.width=integer(item.at("width"),1,8192);image.height=integer(item.at("height"),1,8192);
        auto data=std::make_shared<RenderImageData>();data->png_base64=item.at("png_base64");
        data->thumbnail_base64=item.at("thumbnail_base64");image.data=std::move(data);next.images.push_back(std::move(image));
    }
    validate_render_definition(next);value=std::move(next);
}
} // namespace zima::document

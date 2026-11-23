#pragma once
#include <zima/document/placement_types.hpp>
#include <nlohmann/json_fwd.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace zima::document {

enum class RenderObjectKind { Camera, PointLight, Sun };

// Camera/sun forward is the placed local X axis; local Z is camera up.
// Placements use the ordinary FRONT/TOP and retained-reference-frame contract.
struct RenderObject {
    std::string id, name;
    RenderObjectKind kind{RenderObjectKind::Camera};
    Placement placement;
    bool visible{true};
    bool enabled{true};
    double field_of_view_degrees{45};
    bool orthographic{};
    double orthographic_height_mm{100};
    std::array<double,3> color{{1,1,1}};
    // Point light: luminous power in lm. Sun: normal illuminance in lx.
    double intensity{1000};
    double radius_mm{5};
    double angular_radius_degrees{0.266};
    bool operator==(const RenderObject&) const = default;
};

struct RenderSettings {
    unsigned width{1280}, height{720}, samples{64}, bounces{3};
    double exposure_ev{};
    double environment_strength{1};
    bool use_model_finish{true};
    double roughness{0.4}, metallic{};
    std::array<double,3> background_top{{0.22,0.27,0.32}};
    std::array<double,3> background_bottom{{0.07,0.09,0.11}};
    bool operator==(const RenderSettings&) const = default;
};

// Compressed, immutable image bytes are shared across document copies and Undo.
// Only explicitly retained images belong to the Part; live previews are disposable.
struct RenderImageData {
    std::string png_base64, thumbnail_base64;
    bool operator==(const RenderImageData&) const = default;
};
struct RenderImage {
    std::string id, name, created_utc;
    unsigned width{}, height{};
    std::shared_ptr<const RenderImageData> data;
    bool operator==(const RenderImage& other) const {
        return id==other.id&&name==other.name&&created_utc==other.created_utc&&
            width==other.width&&height==other.height&&
            (data==other.data||(data&&other.data&&*data==*other.data));
    }
};
struct RenderDefinition {
    std::vector<std::string> source_ids;
    bool visible_bodies{true};
    std::string active_camera_id;
    std::vector<RenderObject> objects;
    RenderSettings settings;
    std::vector<RenderImage> images;
    bool operator==(const RenderDefinition&) const = default;
};

// Gallery edits do not change a scene or invalidate an in-flight snapshot.
[[nodiscard]] bool same_render_scene(const RenderDefinition&,const RenderDefinition&);
void validate_render_images(const std::vector<RenderImage>&);

void validate_render_object(const RenderObject&);
void validate_render_definition(const RenderDefinition&);
void to_json(nlohmann::json&,const RenderDefinition&);
void from_json(const nlohmann::json&,RenderDefinition&);

} // namespace zima::document

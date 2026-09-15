#pragma once
#include <zima/kernel/inertia.hpp>
#include <string>
#include <vector>
namespace zima::document {
struct PartDocument;
struct BodyProperties {
    std::string id,name,body_id,after_object_id;
    kernel::Vec3 rotation_degrees;
    bool visible{true};
    double volume{},area{};
    std::optional<double> density_kg_mm3;
    std::optional<kernel::VolumeIntegrals> integrals;
    std::string error;
    bool operator==(const BodyProperties&) const = default;
};
// All records use current cached solid results at their own stable boundary.
void refresh_body_properties(PartDocument&,const std::vector<kernel::BodyResult>&);
[[nodiscard]] BodyProperties evaluate_body_properties(const PartDocument&,const std::vector<kernel::BodyResult>&,BodyProperties);
struct BodyPropertiesInput { const kernel::BodyResult* body{}; kernel::Vec3 translation,rotation; };
[[nodiscard]] std::vector<BodyPropertiesInput> body_properties_inputs(const PartDocument&,const std::vector<kernel::BodyResult>&,const BodyProperties&);
[[nodiscard]] kernel::ViewerMesh body_properties_origin(const BodyProperties&,const std::string& label = "Center of gravity");
[[nodiscard]] kernel::ViewerMesh body_properties_origins(const PartDocument&,const std::string& label = "Center of gravity");
[[nodiscard]] std::string serialize_body_properties(const std::vector<BodyProperties>&);
[[nodiscard]] std::vector<BodyProperties> parse_body_properties(const std::string&);
} // namespace zima::document

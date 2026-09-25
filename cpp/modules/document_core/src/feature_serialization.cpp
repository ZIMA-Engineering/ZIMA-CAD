#include <zima/document/feature_serialization.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <array>
#include <string_view>

namespace zima::document {
namespace {
[[noreturn]] void invalid() { throw std::invalid_argument("Invalid Feature definition."); }
// Explicit mappings do not depend on enum ordinals or native geometry order.
template<class Enum, std::size_t N>
std::string_view name(Enum value, const std::array<std::pair<Enum,std::string_view>,N>& names) {
    for(const auto& [key,text]:names)if(value==key)return text;
    invalid();
}
template<class Enum, std::size_t N>
Enum parse(const nlohmann::json& value,const std::array<std::pair<Enum,std::string_view>,N>& names) {
    const auto text=value.get<std::string>();
    for(const auto& [key,candidate]:names)if(text==candidate)return key;
    invalid();
}
constexpr std::array feature_types{
    std::pair{FeatureType::Point,std::string_view{"point"}},
    std::pair{FeatureType::Axis,std::string_view{"axis"}},
    std::pair{FeatureType::Plane,std::string_view{"plane"}},
    std::pair{FeatureType::Sketch,std::string_view{"sketch"}},
    std::pair{FeatureType::Modeling,std::string_view{"modeling"}}};
constexpr std::array operations{
    std::pair{FeatureSideOperation::None,std::string_view{"none"}},
    std::pair{FeatureSideOperation::Extrusion,std::string_view{"extrusion"}},
    std::pair{FeatureSideOperation::Revolution,std::string_view{"revolution"}}};
constexpr std::array rotations{
    std::pair{FeatureRotationExtent::Angle,std::string_view{"angle"}},
    std::pair{FeatureRotationExtent::Full,std::string_view{"full"}},
    std::pair{FeatureRotationExtent::UpTo,std::string_view{"up_to"}}};
constexpr std::array sources{
    std::pair{ProfileSource::Internal,std::string_view{"internal"}},
    std::pair{ProfileSource::External,std::string_view{"external"}}};
constexpr std::array results{
    std::pair{ProfileResultType::Solid,std::string_view{"solid"}},
    std::pair{ProfileResultType::Thin,std::string_view{"thin"}},
    std::pair{ProfileResultType::Surface,std::string_view{"surface"}}};
constexpr std::array thin_modes{
    std::pair{ThinMode::OneSide,std::string_view{"one_side"}},
    std::pair{ThinMode::OtherSide,std::string_view{"other_side"}},
    std::pair{ThinMode::Symmetric,std::string_view{"symmetric"}}};
constexpr std::array conditions{
    std::pair{EndCondition::Length,std::string_view{"length"}},
    std::pair{EndCondition::UpTo,std::string_view{"up_to"}},
    std::pair{EndCondition::ThroughAll,std::string_view{"through_all"}}};
constexpr std::array target_kinds{
    std::pair{EndTargetKind::Point,std::string_view{"point"}},
    std::pair{EndTargetKind::Plane,std::string_view{"plane"}},
    std::pair{EndTargetKind::Face,std::string_view{"face"}}};
bool finite(kernel::Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
bool positive(double value) { return std::isfinite(value)&&value>0; }
nlohmann::json vector(kernel::Vec3 p) { return {p.x,p.y,p.z}; }
kernel::Vec3 vector(const nlohmann::json& p) {
    if(!p.is_array()||p.size()!=3)invalid();
    return {p.at(0).get<double>(),p.at(1).get<double>(),p.at(2).get<double>()};
}
}

void validate_feature_parameters(const FeatureParameters& p) {
    static_cast<void>(name(p.type,feature_types));
    static_cast<void>(name(p.profile_source,sources));
    static_cast<void>(name(p.result_type,results));
    static_cast<void>(name(p.thin_mode,thin_modes));
    if(!std::isfinite(p.profile_plane_offset)||!positive(p.thin_thickness))invalid();
    for(const auto& side:p.sides) {
        static_cast<void>(name(side.operation,operations));
        static_cast<void>(name(side.extrusion_extent,conditions));
        static_cast<void>(name(side.rotation_extent,rotations));
        if(!positive(side.length)||!positive(side.angle_degrees)||side.angle_degrees>360)invalid();
        for(const auto& target:side.targets) {
            static_cast<void>(name(target.kind,target_kinds));
            if(!target.reference.valid()||!finite(target.fallback_origin)||!finite(target.fallback_normal)||
               target.fallback_triangles.size()%3!=0)invalid();
            for(const auto& point:target.fallback_triangles)if(!finite(point))invalid();
        }
    }
}

nlohmann::json serialize_feature_parameters(const FeatureParameters& p) {
    validate_feature_parameters(p);
    nlohmann::json sides=nlohmann::json::array();
    for(const auto& side:p.sides) {
        nlohmann::json targets=nlohmann::json::array();
        for(const auto& t:side.targets) {
            nlohmann::json triangles=nlohmann::json::array();
            for(const auto& point:t.fallback_triangles)triangles.push_back(vector(point));
            targets.push_back({{"kind",name(t.kind,target_kinds)},
                {"owner",t.reference.owner_id},{"key",t.reference.semantic_key},
                {"instance_path",t.reference.instance_path},{"label",t.label},
                {"origin",vector(t.fallback_origin)},{"normal",vector(t.fallback_normal)},
                {"triangles",std::move(triangles)}});
        }
        sides.push_back({{"operation",name(side.operation,operations)},{"length",side.length},
            {"extrusion_extent",name(side.extrusion_extent,conditions)},
            {"angle_degrees",side.angle_degrees},{"rotation_extent",name(side.rotation_extent,rotations)},
            {"targets",std::move(targets)}});
    }
    return {{"automatic_name",p.automatic_name},{"type",name(p.type,feature_types)},{"sketch_id",p.sketch_id},{"axis_segment_id",p.axis_segment_id},
        {"profile_source",name(p.profile_source,sources)},{"profile_plane_offset",p.profile_plane_offset},
        {"result_type",name(p.result_type,results)},{"thin_thickness",p.thin_thickness},
        {"thin_mode",name(p.thin_mode,thin_modes)},{"symmetric",p.symmetric},
        {"origin_centerline",p.origin_centerline},{"centroid_centerline",p.centroid_centerline},
        {"sides",std::move(sides)}};
}

FeatureParameters load_feature_parameters(const nlohmann::json& source) {
    try {
        FeatureParameters p;
        p.automatic_name=source.value("automatic_name",std::string{});
        p.type=parse(source.at("type"),feature_types);
        p.sketch_id=source.at("sketch_id").get<std::string>();
        p.axis_segment_id=source.at("axis_segment_id").get<std::string>();
        p.profile_source=parse(source.at("profile_source"),sources);
        p.profile_plane_offset=source.at("profile_plane_offset").get<double>();
        p.result_type=parse(source.at("result_type"),results);
        p.thin_thickness=source.at("thin_thickness").get<double>();
        p.thin_mode=parse(source.at("thin_mode"),thin_modes);
        p.symmetric=source.at("symmetric").get<bool>();
        p.origin_centerline=source.at("origin_centerline").get<bool>();
        p.centroid_centerline=source.at("centroid_centerline").get<bool>();
        const auto& sides=source.at("sides");
        if(!sides.is_array()||sides.size()!=2)invalid();
        for(std::size_t i=0;i<2;++i) {
            const auto& value=sides.at(i);auto& side=p.sides[i];
            side.operation=parse(value.at("operation"),operations);
            side.length=value.at("length").get<double>();
            side.extrusion_extent=parse(value.at("extrusion_extent"),conditions);
            side.angle_degrees=value.at("angle_degrees").get<double>();
            side.rotation_extent=parse(value.at("rotation_extent"),rotations);
            const auto& targets=value.at("targets");if(!targets.is_array())invalid();
            for(const auto& t:targets) {
                ExtrusionParameters::EndTarget target;
                target.kind=parse(t.at("kind"),target_kinds);
                target.reference={t.at("owner").get<std::string>(),t.at("key").get<std::string>(),t.at("instance_path").get<std::string>()};
                target.label=t.at("label").get<std::string>();
                target.fallback_origin=vector(t.at("origin"));target.fallback_normal=vector(t.at("normal"));
                const auto& triangles=t.at("triangles");if(!triangles.is_array())invalid();
                for(const auto& point:triangles)target.fallback_triangles.push_back(vector(point));
                side.targets.push_back(std::move(target));
            }
        }
        validate_feature_parameters(p);
        return p;
    } catch(const nlohmann::json::exception&) { invalid(); }
}
}

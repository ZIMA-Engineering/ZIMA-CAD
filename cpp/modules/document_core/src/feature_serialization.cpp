#include <zima/document/feature_serialization.hpp>
#include <zima/document/feature_rotation_span.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <array>
#include <string_view>
#include <algorithm>
#include <set>
#include <tuple>

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
    std::pair{FeatureSideOperation::Revolution,std::string_view{"revolution"}},
    std::pair{FeatureSideOperation::Twist,std::string_view{"twist"}}};
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
nlohmann::json reference(const kernel::FaceReference& ref) {
    return {{"owner",ref.owner_id},{"key",ref.semantic_key},{"instance_path",ref.instance_path}};
}
kernel::FaceReference reference(const nlohmann::json& value) {
    return {value.at("owner").get<std::string>(),value.at("key").get<std::string>(),value.at("instance_path").get<std::string>()};
}
}

void validate_feature_parameters(const FeatureParameters& p) {
    static_cast<void>(name(p.type,feature_types));
    static_cast<void>(name(p.profile_source,sources));
    static_cast<void>(name(p.result_type,results));
    static_cast<void>(name(p.thin_mode,thin_modes));
    if(!std::isfinite(p.profile_plane_offset)||!positive(p.thin_thickness))invalid();
    const auto first=p.effective_side(0),second=p.effective_side(1);
    if(p.type==FeatureType::Axis && !first.axis_enabled && !second.axis_enabled)
        throw std::invalid_argument("Osa musí mít alespoň jeden nenulový směr.");
    if(first.operation==FeatureSideOperation::Revolution&&second.operation==FeatureSideOperation::Revolution&&
        first.rotation_extent!=FeatureRotationExtent::UpTo&&second.rotation_extent!=FeatureRotationExtent::UpTo)
        validate_feature_rotation_span(first.rotation_extent==FeatureRotationExtent::Full?360:first.angle_degrees,
            second.rotation_extent==FeatureRotationExtent::Full?360:second.angle_degrees);
    for(const auto& side:p.sides) {
        static_cast<void>(name(side.operation,operations));
        static_cast<void>(name(side.extrusion_extent,conditions));
        static_cast<void>(name(side.rotation_extent,rotations));
        if(!positive(side.length)||!positive(side.angle_degrees)||side.angle_degrees>(side.operation==FeatureSideOperation::Twist?36000:360))invalid();
        if(side.operation==FeatureSideOperation::Twist&&
           (p.result_type!=ProfileResultType::Solid||p.symmetric||p.sides[1].operation!=FeatureSideOperation::None||
            &side!=&p.sides[0]||side.draft_angle_degrees!=0||!side.targets.empty()))invalid();
        if(!std::isfinite(side.draft_angle_degrees)||std::abs(side.draft_angle_degrees)>=90)
            throw std::invalid_argument("Draft angle must be between -90 and 90 degrees.");
        if(!std::isfinite(side.up_to.length_correction)||!std::isfinite(side.up_to.angle_correction_degrees)||
                std::abs(side.up_to.angle_correction_degrees)>=360)invalid();
        if(side.up_to.primary_cap_source&&!side.up_to.primary_cap_source->valid())invalid();
        std::set<std::tuple<std::string,std::string,std::string>> target_ids;
        for(const auto& target:side.targets) {
            static_cast<void>(name(target.kind,target_kinds));
            if(!target.reference.valid()||!finite(target.fallback_origin)||!finite(target.fallback_normal)||
               target.fallback_triangles.size()%3!=0)invalid();
            if(!target_ids.emplace(target.reference.owner_id,target.reference.semantic_key,target.reference.instance_path).second||
                (p.type==FeatureType::Modeling&&side.targets.size()>1&&target.kind==EndTargetKind::Point))invalid();
            for(const auto& point:target.fallback_triangles)if(!finite(point))invalid();
        }
        if(!side.up_to.retained_patches.empty()) {
            std::set<std::tuple<std::string,std::string,std::string,std::string,std::string,std::string>> seen;
            for(const auto& choice:side.up_to.retained_patches) {
                const auto source=std::ranges::find_if(side.targets,[&](const auto& t){return t.reference==choice.source;});
                const auto adjacent=std::ranges::find_if(side.targets,[&](const auto& t){return t.reference==choice.adjacent;});
                if(source==side.targets.end()||adjacent==side.targets.end()||
                    std::abs(source-adjacent)!=1||!seen.emplace(choice.source.owner_id,choice.source.semantic_key,
                        choice.source.instance_path,choice.adjacent.owner_id,choice.adjacent.semantic_key,choice.adjacent.instance_path).second)invalid();
            }
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
            if(t.external_source)targets.back()["external_source"]=serialize_external_profile_target(*t.external_source);
        }
        sides.push_back({{"operation",name(side.operation,operations)},{"length",side.length},
            {"extrusion_extent",name(side.extrusion_extent,conditions)},
            {"angle_degrees",side.angle_degrees},{"rotation_extent",name(side.rotation_extent,rotations)},
            {"draft_angle_degrees",side.draft_angle_degrees},
            {"targets",std::move(targets)}});
        if(!side.axis_enabled)sides.back()["axis_enabled"]=false;
        if(!side.up_to.neutral()) {
            nlohmann::json choices=nlohmann::json::array();
            for(const auto& choice:side.up_to.retained_patches)
                choices.push_back({{"source",reference(choice.source)},{"adjacent",reference(choice.adjacent)},{"positive",choice.positive}});
            sides.back()["up_to"]={{"length_correction",side.up_to.length_correction},
                {"angle_correction_degrees",side.up_to.angle_correction_degrees},{"retained_patches",std::move(choices)}};
            if(side.up_to.primary_cap_source)sides.back()["up_to"]["primary_cap_source"]=reference(*side.up_to.primary_cap_source);
        }
        if(side.operation==FeatureSideOperation::Twist) {
            sides.back()["twist_reverse"]=side.twist_reverse;
            sides.back()["twist_smooth"]=side.twist_smooth;
        }
    }
    nlohmann::json result={{"automatic_name",p.automatic_name},{"type",name(p.type,feature_types)},{"sketch_id",p.sketch_id},{"axis_segment_id",p.axis_segment_id},
        {"profile_source",name(p.profile_source,sources)},{"profile_plane_offset",p.profile_plane_offset},
        {"result_type",name(p.result_type,results)},{"thin_thickness",p.thin_thickness},
        {"thin_mode",name(p.thin_mode,thin_modes)},{"symmetric",p.symmetric},
        {"origin_centerline",p.origin_centerline},{"centroid_centerline",p.centroid_centerline},
        {"show_point",p.show_point},{"show_text",p.show_text},
        {"sides",std::move(sides)}};
    // Neutral defaults need no stored override, just like axis_enabled above.
    if(p.reversed_sides)result["reversed_sides"]=true;
    if(p.axis_visible_in_3d)result["axis_visible_in_3d"]=true;
    return result;
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
        p.reversed_sides=source.value("reversed_sides",false);
        p.origin_centerline=source.at("origin_centerline").get<bool>();
        p.centroid_centerline=source.at("centroid_centerline").get<bool>();
        p.show_point=source.at("show_point").get<bool>();
        p.show_text=source.at("show_text").get<bool>();
        p.axis_visible_in_3d=source.value("axis_visible_in_3d",false);
        const auto& sides=source.at("sides");
        if(!sides.is_array()||sides.size()!=2)invalid();
        for(std::size_t i=0;i<2;++i) {
            const auto& value=sides.at(i);auto& side=p.sides[i];
            side.operation=parse(value.at("operation"),operations);
            if(side.operation==FeatureSideOperation::Twist) {
                side.twist_reverse=value.at("twist_reverse").get<bool>();
                side.twist_smooth=value.at("twist_smooth").get<bool>();
            }
            side.length=value.at("length").get<double>();
            side.axis_enabled=value.value("axis_enabled",true);
            side.extrusion_extent=parse(value.at("extrusion_extent"),conditions);
            side.angle_degrees=value.at("angle_degrees").get<double>();
            side.draft_angle_degrees=value.at("draft_angle_degrees").get<double>();
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
                if(t.contains("external_source"))target.external_source=load_external_profile_target(t.at("external_source"));
                side.targets.push_back(std::move(target));
            }
            if(value.contains("up_to")) {
                const auto& up_to=value.at("up_to");
                side.up_to.length_correction=up_to.at("length_correction").get<double>();
                side.up_to.angle_correction_degrees=up_to.at("angle_correction_degrees").get<double>();
                if(up_to.contains("primary_cap_source"))side.up_to.primary_cap_source=reference(up_to.at("primary_cap_source"));
                const auto& choices=up_to.at("retained_patches");if(!choices.is_array())invalid();
                for(const auto& choice:choices)side.up_to.retained_patches.push_back({reference(choice.at("source")),
                    reference(choice.at("adjacent")),choice.at("positive").get<bool>()});
            }
        }
        validate_feature_parameters(p);
        return p;
    } catch(const nlohmann::json::exception&) { invalid(); }
}
}

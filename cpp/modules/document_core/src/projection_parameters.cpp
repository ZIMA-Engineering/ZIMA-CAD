#include <zima/document/projection_parameters.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace zima::document {
namespace {
[[noreturn]] void invalid() {throw std::invalid_argument("Invalid Projection definition.");}
bool finite(kernel::Vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
nlohmann::json vector(kernel::Vec3 v) {return {v.x,v.y,v.z};}
kernel::Vec3 vector(const nlohmann::json& value) {
    if(!value.is_array()||value.size()!=3)invalid();
    return {value.at(0).get<double>(),value.at(1).get<double>(),value.at(2).get<double>()};
}
template<class Reference> nlohmann::json reference(const Reference& r) {
    return {{"owner",r.owner_id},{"key",r.semantic_key},{"instance_path",r.instance_path}};
}
template<class Reference> Reference reference(const nlohmann::json& value) {
    return {value.at("owner").get<std::string>(),value.at("key").get<std::string>(),value.at("instance_path").get<std::string>()};
}
std::string output_name(ProjectionOutput value) {
    switch(value) {
    case ProjectionOutput::Curves:return "curves";
    case ProjectionOutput::Split:return "split";
    case ProjectionOutput::Surface:return "surface";
    case ProjectionOutput::Relief:return "relief";
    }
    invalid();
}
}

void validate_projection_parameters(const ProjectionParameters& p) {
    static_cast<void>(output_name(p.output));
    if(p.output==ProjectionOutput::Relief&&(!std::isfinite(p.height)||p.height<=0))invalid();
    if(p.relief_direction!=ProjectionReliefDirection::Projection&&p.relief_direction!=ProjectionReliefDirection::SurfaceNormal)invalid();
    if(p.direction!=ProjectionDirection::SketchNormal&&p.direction!=ProjectionDirection::Reference)invalid();
    if(p.direction_reference.owner_id.empty()!=p.direction_reference.semantic_key.empty())invalid();
    std::set<std::tuple<std::string,std::string,std::string>> seen;
    for(const auto& target:p.targets) {
        const auto& r=target.reference;
        if(target.kind!=EndTargetKind::Face||!r.valid()||!finite(target.fallback_origin)||!finite(target.fallback_normal)||
            target.fallback_triangles.size()%3||!seen.emplace(r.owner_id,r.semantic_key,r.instance_path).second)invalid();
        for(const auto& point:target.fallback_triangles)if(!finite(point))invalid();
    }
    std::set<std::tuple<std::string,std::string,std::string,std::string>> regions;
    for(const auto& region:p.regions) {
        if(!region.target.valid()||!finite(region.seed)||
            std::ranges::none_of(p.targets,[&](const auto& target){return target.reference==region.target;}))invalid();
        if(!region.boundary_key.empty()&&!regions.emplace(region.target.owner_id,region.target.semantic_key,
            region.target.instance_path,region.boundary_key).second)invalid();
    }
    // Empty target/direction entries can belong to a pending or deliberately
    // retained unresolved definition. Calculation, not serialization, requires
    // complete inputs. Retained region choices survive a temporary mode switch.
}

nlohmann::json serialize_projection_parameters(const ProjectionParameters& p) {
    validate_projection_parameters(p);
    auto targets=nlohmann::json::array(),regions=nlohmann::json::array();
    for(const auto& target:p.targets) {
        auto item=reference(target.reference);item["label"]=target.label;
        item["origin"]=vector(target.fallback_origin);item["normal"]=vector(target.fallback_normal);
        item["triangles"]=nlohmann::json::array();
        for(const auto& point:target.fallback_triangles)item["triangles"].push_back(vector(point));
        if(target.external_source)item["external_source"]=serialize_external_profile_target(*target.external_source);
        targets.push_back(std::move(item));
    }
    for(const auto& region:p.regions)regions.push_back({{"target",reference(region.target)},
        {"seed",vector(region.seed)},{"boundary_key",region.boundary_key}});
    nlohmann::json result={{"output",output_name(p.output)},{"targets",std::move(targets)},
        {"direction",p.direction==ProjectionDirection::SketchNormal?"sketch_normal":"reference"},
        {"direction_reference",reference(p.direction_reference)},{"reverse",p.reverse},{"regions",std::move(regions)}};
    if(p.output==ProjectionOutput::Relief) {
        result["height"]=p.height;
        result["relief_direction"]=p.relief_direction==ProjectionReliefDirection::Projection?"projection":"surface_normal";
    }
    return result;
}

ProjectionParameters load_projection_parameters(const nlohmann::json& value) {
    try {
        ProjectionParameters p;
        const auto output=value.at("output").get<std::string>();
        if(output=="curves")p.output=ProjectionOutput::Curves;
        else if(output=="split")p.output=ProjectionOutput::Split;
        else if(output=="surface")p.output=ProjectionOutput::Surface;
        else if(output=="relief") {
            p.output=ProjectionOutput::Relief;p.height=value.at("height").get<double>();
            const auto relief_direction=value.value("relief_direction",std::string("surface_normal"));
            if(relief_direction=="projection")p.relief_direction=ProjectionReliefDirection::Projection;
            else if(relief_direction!="surface_normal")invalid();
        }
        else invalid();
        const auto direction=value.at("direction").get<std::string>();
        if(direction=="sketch_normal")p.direction=ProjectionDirection::SketchNormal;
        else if(direction=="reference")p.direction=ProjectionDirection::Reference;
        else invalid();
        p.direction_reference=reference<kernel::EdgeReference>(value.at("direction_reference"));
        p.reverse=value.at("reverse").get<bool>();
        if(!value.at("targets").is_array()||!value.at("regions").is_array())invalid();
        for(const auto& item:value.at("targets")) {
            ExtrusionParameters::EndTarget target;
            target.reference=reference<kernel::FaceReference>(item);target.label=item.at("label").get<std::string>();
            target.fallback_origin=vector(item.at("origin"));target.fallback_normal=vector(item.at("normal"));
            if(!item.at("triangles").is_array())invalid();
            for(const auto& point:item.at("triangles"))target.fallback_triangles.push_back(vector(point));
            if(item.contains("external_source"))target.external_source=load_external_profile_target(item.at("external_source"));
            p.targets.push_back(std::move(target));
        }
        for(const auto& item:value.at("regions"))p.regions.push_back({reference<kernel::FaceReference>(item.at("target")),
            vector(item.at("seed")),item.at("boundary_key").get<std::string>()});
        validate_projection_parameters(p);return p;
    } catch(const nlohmann::json::exception&) {invalid();}
}
}

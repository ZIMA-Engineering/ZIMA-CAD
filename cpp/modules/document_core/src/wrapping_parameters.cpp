#include <zima/document/wrapping_parameters.hpp>
#include <nlohmann/json.hpp>
#include <cmath>

namespace zima::document {
namespace {
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid Wrap definition.");}
bool finite(kernel::Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
nlohmann::json vector(kernel::Vec3 p){return {p.x,p.y,p.z};}
kernel::Vec3 vector(const nlohmann::json& j){if(!j.is_array()||j.size()!=3)invalid();return {j.at(0),j.at(1),j.at(2)};}
const char* output_name(WrappingOutput value){switch(value){case WrappingOutput::Curves:return "curves";case WrappingOutput::Surface:return "surface";case WrappingOutput::Relief:return "relief";}invalid();}
}
void validate_wrapping_parameters(const WrappingParameters& p) {
    static_cast<void>(output_name(p.output));
    if(!std::isfinite(p.height)||p.height<=0)invalid();
    if(p.target) {
        const auto& t=*p.target;
        if(t.kind!=EndTargetKind::Face||!t.reference.valid()||!finite(t.fallback_origin)||
                !finite(t.fallback_normal)||t.fallback_triangles.size()%3)invalid();
        for(const auto& point:t.fallback_triangles)if(!finite(point))invalid();
    }
}
nlohmann::json serialize_wrapping_parameters(const WrappingParameters& p) {
    validate_wrapping_parameters(p);
    nlohmann::json result={{"output",output_name(p.output)},{"height",p.height},{"reverse_u",p.reverse_u},{"reverse_v",p.reverse_v}};
    if(p.target) {
        const auto& t=*p.target;auto triangles=nlohmann::json::array();
        for(const auto& point:t.fallback_triangles)triangles.push_back(vector(point));
        result["target"]={{"owner",t.reference.owner_id},{"key",t.reference.semantic_key},{"instance_path",t.reference.instance_path},
            {"label",t.label},{"origin",vector(t.fallback_origin)},{"normal",vector(t.fallback_normal)},{"triangles",std::move(triangles)}};
        if(t.external_source)result["target"]["external_source"]=serialize_external_profile_target(*t.external_source);
    }
    return result;
}
WrappingParameters load_wrapping_parameters(const nlohmann::json& value) {
    try {
        WrappingParameters p;const auto output=value.at("output").get<std::string>();
        if(output=="curves")p.output=WrappingOutput::Curves;
        else if(output=="surface")p.output=WrappingOutput::Surface;
        else if(output=="relief")p.output=WrappingOutput::Relief;else invalid();
        p.height=value.at("height");p.reverse_u=value.at("reverse_u");p.reverse_v=value.at("reverse_v");
        if(value.contains("target")) {
            const auto& t=value.at("target");p.target.emplace();auto& target=*p.target;
            target.reference={t.at("owner").get<std::string>(),t.at("key").get<std::string>(),t.at("instance_path").get<std::string>()};
            target.label=t.at("label");target.fallback_origin=vector(t.at("origin"));target.fallback_normal=vector(t.at("normal"));
            if(!t.at("triangles").is_array())invalid();
            for(const auto& point:t.at("triangles"))target.fallback_triangles.push_back(vector(point));
            if(t.contains("external_source"))target.external_source=load_external_profile_target(t.at("external_source"));
        }
        validate_wrapping_parameters(p);return p;
    }catch(const nlohmann::json::exception&){invalid();}
}
}

#include <zima/document/fem_condition.hpp>
#include <zima/document/placement_json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <stdexcept>

namespace zima::document {
void validate_fem_condition(const FemCondition& value) {
    if(value.id.empty()||value.name.empty())throw std::invalid_argument("Specify a nonempty object name.");
    if(value.kind!=FemConditionKind::Constraint&&value.kind!=FemConditionKind::Force&&value.kind!=FemConditionKind::Pressure)
        throw std::invalid_argument("Invalid FEM condition definition.");
    if(value.region!=FemRegionKind::Point&&value.region!=FemRegionKind::Curve&&value.region!=FemRegionKind::Surface)
        throw std::invalid_argument("Invalid FEM condition definition.");
    if(value.kind==FemConditionKind::Pressure&&value.region!=FemRegionKind::Surface)
        throw std::invalid_argument("Pressure requires a surface region.");
    if(value.region!=FemRegionKind::Point&&value.references.empty())
        throw std::invalid_argument("Select the condition geometry.");
    if(!std::isfinite(value.force_N)||!std::isfinite(value.pressure_MPa)||value.pressure_MPa<0)
        throw std::invalid_argument("Invalid FEM condition value.");
    if(value.kind==FemConditionKind::Constraint&&std::ranges::none_of(value.restrained,[](bool v){return v;}))
        throw std::invalid_argument("Restrain at least one translation direction.");
    const auto& p=value.placement;
    for(double n:{p.x,p.y,p.z,p.rotation_x,p.rotation_y,p.rotation_z,
            p.absolute_rotation_x,p.absolute_rotation_y,p.absolute_rotation_z,
            p.rotation_offset_x,p.rotation_offset_y,p.rotation_offset_z})
        if(!std::isfinite(n))throw std::invalid_argument("Invalid FEM condition definition.");
    std::set<std::tuple<std::string,std::string,std::string>> unique;
    for(const auto& r:value.references)
        if(r.owner_id.empty()||r.semantic_key.empty()||!r.instance_path.empty()||r.owner_id==value.id||
                !unique.emplace(r.owner_id,r.semantic_key,r.instance_path).second)
            throw std::invalid_argument("Invalid FEM condition geometry reference.");
    for(const auto& r:p.references)
        if(r.owner_id.empty()||r.semantic_key.empty()||!r.instance_path.empty()||r.owner_id==value.id||!std::isfinite(r.offset))
            throw std::invalid_argument("Invalid FEM condition geometry reference.");
}
void to_json(nlohmann::json& j,const FemCondition& v) {
    validate_fem_condition(v);
    j={{"id",v.id},{"name",v.name},{"kind",v.kind==FemConditionKind::Constraint?"constraint":v.kind==FemConditionKind::Force?"force":"pressure"},
        {"region",v.region==FemRegionKind::Point?"point":v.region==FemRegionKind::Curve?"curve":"surface"},
        {"placement",v.placement},{"references",nlohmann::json::array()},
        {"force_N",v.force_N},{"pressure_MPa",v.pressure_MPa},{"restrained",v.restrained},
        {"pressure_reversed",v.pressure_reversed},{"visible",v.visible}};
    for(const auto& r:v.references)j["references"].push_back({{"owner",r.owner_id},{"key",r.semantic_key},{"path",r.instance_path}});
}
void from_json(const nlohmann::json& j,FemCondition& v) {
    FemCondition n;n.id=j.at("id");n.name=j.at("name");
    const std::string kind=j.at("kind"),region=j.at("region");
    if(kind!="constraint"&&kind!="force"&&kind!="pressure")throw std::invalid_argument("Invalid FEM condition definition.");
    if(region!="point"&&region!="curve"&&region!="surface")throw std::invalid_argument("Invalid FEM condition definition.");
    n.kind=kind=="constraint"?FemConditionKind::Constraint:kind=="force"?FemConditionKind::Force:FemConditionKind::Pressure;
    n.region=region=="point"?FemRegionKind::Point:region=="curve"?FemRegionKind::Curve:FemRegionKind::Surface;
    n.placement=j.at("placement").get<Placement>();
    for(const auto& r:j.at("references"))n.references.push_back({r.at("owner"),r.at("key"),r.at("path")});
    n.force_N=j.at("force_N");n.pressure_MPa=j.at("pressure_MPa");
    const auto& restrained=j.at("restrained");
    if(!restrained.is_array()||restrained.size()!=3)throw std::invalid_argument("Invalid FEM condition definition.");
    n.restrained=restrained.get<std::array<bool,3>>();
    n.pressure_reversed=j.at("pressure_reversed");n.visible=j.at("visible");
    validate_fem_condition(n);v=std::move(n);
}
} // namespace zima::document

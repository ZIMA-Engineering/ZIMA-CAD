#include <zima/document/fem_condition.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/sketcher/sketch.hpp>
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
    if(value.projected_sketch) {
        if(value.kind==FemConditionKind::Pressure||value.region==FemRegionKind::Point)
            throw std::invalid_argument("Invalid FEM condition definition.");
        const auto sketch=sketcher::Sketch::from_serialized(value.projected_sketch->serialized);
        if(sketch.id.empty()||sketch.owner_container_id!=value.id||sketch.plane_auto||
                !sketch.plane_reference_owner_id.empty()||
                !sketch.external_references.empty()||!std::isfinite(sketch.plane_offset))
            throw std::invalid_argument("Invalid FEM condition definition.");
        const auto& seeds=value.projected_sketch->surface_seeds;
        const auto& keys=value.projected_sketch->surface_region_keys;
        if((value.region==FemRegionKind::Surface&&seeds.size()!=value.references.size())||
                (value.region==FemRegionKind::Curve&&!seeds.empty())||
                (!keys.empty()&&keys.size()!=seeds.size())||
                std::ranges::any_of(seeds,[](const auto& p){
                    return !std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z);
                }))throw std::invalid_argument("Invalid FEM condition geometry reference.");
    }
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
    if(v.projected_sketch) {
        auto seeds=nlohmann::json::array();
        for(const auto& seed:v.projected_sketch->surface_seeds)seeds.push_back({seed.x,seed.y,seed.z});
        j["projected_sketch"]={{"sketch",nlohmann::json::parse(v.projected_sketch->serialized)},
            {"surface_seeds",std::move(seeds)},{"surface_region_keys",v.projected_sketch->surface_region_keys}};
    }
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
    if(j.contains("projected_sketch")) {
        const auto& projection=j.at("projected_sketch");
        FemProjectedSketch owned{sketcher::Sketch::from_serialized_json(projection.at("sketch")).serialized(),{}};
        for(const auto& point:projection.at("surface_seeds")) {
            if(!point.is_array()||point.size()!=3)throw std::invalid_argument("Invalid FEM condition geometry reference.");
            owned.surface_seeds.push_back({point.at(0).get<double>(),point.at(1).get<double>(),point.at(2).get<double>()});
        }
        owned.surface_region_keys=projection.at("surface_region_keys").get<std::vector<std::string>>();
        n.projected_sketch=std::move(owned);
    }
    n.force_N=j.at("force_N");n.pressure_MPa=j.at("pressure_MPa");
    const auto& restrained=j.at("restrained");
    if(!restrained.is_array()||restrained.size()!=3)throw std::invalid_argument("Invalid FEM condition definition.");
    n.restrained=restrained.get<std::array<bool,3>>();
    n.pressure_reversed=j.at("pressure_reversed");n.visible=j.at("visible");
    validate_fem_condition(n);v=std::move(n);
}
} // namespace zima::document

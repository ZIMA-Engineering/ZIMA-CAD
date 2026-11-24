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
    if(value.kind!=FemConditionKind::Constraint&&value.kind!=FemConditionKind::Force&&value.kind!=FemConditionKind::Pressure&&
            value.kind!=FemConditionKind::HeatSource&&!fem_measurement(value.kind))
        throw std::invalid_argument("Invalid FEM condition definition.");
    if(value.region!=FemRegionKind::Point&&value.region!=FemRegionKind::Curve&&value.region!=FemRegionKind::Surface)
        throw std::invalid_argument("Invalid FEM condition definition.");
    if((value.kind==FemConditionKind::MeasurementDistance)!=value.distance.has_value())
        throw std::invalid_argument("Invalid FEM condition definition.");
    if(value.distance) {
        const auto& d=*value.distance;
        if(value.region!=FemRegionKind::Point||!value.references.empty()||value.projected_sketch||!value.placement.references.empty())
            throw std::invalid_argument("Invalid FEM condition definition.");
        if(d.point_ids[0].empty()||d.point_ids[1].empty()||d.point_ids[0]==d.point_ids[1]||
                d.point_ids[0]==value.id||d.point_ids[1]==value.id)
            throw std::invalid_argument("Select two different measurement points.");
        for(auto p:d.positions)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            throw std::invalid_argument("Invalid FEM condition definition.");
    }
    if(value.kind==FemConditionKind::Pressure&&value.region!=FemRegionKind::Surface)
        throw std::invalid_argument("Pressure requires a surface region.");
    if(value.kind==FemConditionKind::HeatSource&&value.region!=FemRegionKind::Surface)
        throw std::invalid_argument("A heat source requires a surface region.");
    if(value.kind==FemConditionKind::MeasurementPoint&&
            (value.region!=FemRegionKind::Point||!value.references.empty()||value.projected_sketch))
        throw std::invalid_argument("A measurement point uses its placed origin.");
    if(value.region!=FemRegionKind::Point&&value.references.empty()&&
            (!value.projected_sketch||value.projected_sketch->project_to_body))
        throw std::invalid_argument("Select the condition geometry.");
    if(value.projected_sketch) {
        if(value.region==FemRegionKind::Point)
            throw std::invalid_argument("Invalid FEM condition definition.");
        const auto sketch=sketcher::Sketch::from_serialized(value.projected_sketch->serialized);
        if(sketch.id.empty()||sketch.owner_container_id!=value.id||
                !sketch.plane_reference_owner_id.empty()||
                !std::isfinite(sketch.plane_offset))
            throw std::invalid_argument("Invalid FEM condition definition.");
        const auto& seeds=value.projected_sketch->surface_seeds;
        const auto& keys=value.projected_sketch->surface_region_keys;
        if(!value.projected_sketch->project_to_body&&
                (!value.references.empty()||!seeds.empty()||!keys.empty()))
            throw std::invalid_argument("Invalid FEM condition geometry reference.");
        if((value.region==FemRegionKind::Surface&&seeds.size()!=value.references.size())||
                (value.region==FemRegionKind::Curve&&!seeds.empty())||
                (!keys.empty()&&keys.size()!=seeds.size())||
                std::ranges::any_of(seeds,[](const auto& p){
                    return !std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z);
                }))throw std::invalid_argument("Invalid FEM condition geometry reference.");
    }
    if(!std::isfinite(value.force_N)||!std::isfinite(value.pressure_MPa)||value.pressure_MPa<0||!std::isfinite(value.heat_W))
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
    const auto kind=v.kind==FemConditionKind::Constraint?"constraint":v.kind==FemConditionKind::Force?"force":
        v.kind==FemConditionKind::Pressure?"pressure":v.kind==FemConditionKind::HeatSource?"heat-source":
        v.kind==FemConditionKind::MeasurementDistance?"measurement-distance":"measurement-point";
    j={{"id",v.id},{"name",v.name},{"kind",kind},
        {"region",v.region==FemRegionKind::Point?"point":v.region==FemRegionKind::Curve?"curve":"surface"},
        {"placement",v.placement},{"references",nlohmann::json::array()},
        {"force_N",v.force_N},{"pressure_MPa",v.pressure_MPa},{"restrained",v.restrained},
        {"pressure_reversed",v.pressure_reversed},{"visible",v.visible}};
    if(!v.calculation_error.empty())j["calculation_error"]=v.calculation_error;
    if(v.kind==FemConditionKind::HeatSource)j["heat_W"]=v.heat_W;
    if(v.distance) {
        auto positions=nlohmann::json::array();for(auto p:v.distance->positions)positions.push_back({p.x,p.y,p.z});
        j["distance"]={{"points",v.distance->point_ids},{"positions",std::move(positions)},{"valid",v.distance->reference_valid}};
    }
    for(const auto& r:v.references)j["references"].push_back({{"owner",r.owner_id},{"key",r.semantic_key},{"path",r.instance_path}});
    if(v.projected_sketch) {
        auto seeds=nlohmann::json::array();
        for(const auto& seed:v.projected_sketch->surface_seeds)seeds.push_back({seed.x,seed.y,seed.z});
        j["projected_sketch"]={{"sketch",nlohmann::json::parse(v.projected_sketch->serialized)},
            {"surface_seeds",std::move(seeds)},{"surface_region_keys",v.projected_sketch->surface_region_keys},
            {"project_to_body",v.projected_sketch->project_to_body}};
    }
}
void from_json(const nlohmann::json& j,FemCondition& v) {
    FemCondition n;n.id=j.at("id");n.name=j.at("name");
    const std::string kind=j.at("kind"),region=j.at("region");
    if(kind!="constraint"&&kind!="force"&&kind!="pressure"&&kind!="heat-source"&&kind!="measurement-point"&&kind!="measurement-distance")throw std::invalid_argument("Invalid FEM condition definition.");
    if(region!="point"&&region!="curve"&&region!="surface")throw std::invalid_argument("Invalid FEM condition definition.");
    n.kind=kind=="constraint"?FemConditionKind::Constraint:kind=="force"?FemConditionKind::Force:
        kind=="pressure"?FemConditionKind::Pressure:kind=="heat-source"?FemConditionKind::HeatSource:
        kind=="measurement-distance"?FemConditionKind::MeasurementDistance:FemConditionKind::MeasurementPoint;
    if(n.kind==FemConditionKind::HeatSource)n.heat_W=j.at("heat_W");
    n.region=region=="point"?FemRegionKind::Point:region=="curve"?FemRegionKind::Curve:FemRegionKind::Surface;
    n.placement=j.at("placement").get<Placement>();
    if(j.contains("distance")) {
        const auto& d=j.at("distance");n.distance.emplace();
        if(d.at("points").size()!=2||d.at("positions").size()!=2)throw std::invalid_argument("Invalid FEM condition definition.");
        n.distance->point_ids=d.at("points").get<std::array<std::string,2>>();n.distance->reference_valid=d.at("valid");
        for(unsigned i=0;i<2;++i) {
            const auto& p=d.at("positions").at(i);if(!p.is_array()||p.size()!=3)throw std::invalid_argument("Invalid FEM condition definition.");
            n.distance->positions[i]={p.at(0),p.at(1),p.at(2)};
        }
    }
    for(const auto& r:j.at("references"))n.references.push_back({r.at("owner"),r.at("key"),r.at("path")});
    if(j.contains("projected_sketch")) {
        const auto& projection=j.at("projected_sketch");
        FemProjectedSketch owned{sketcher::Sketch::from_serialized_json(projection.at("sketch")).serialized(),{}};
        for(const auto& point:projection.at("surface_seeds")) {
            if(!point.is_array()||point.size()!=3)throw std::invalid_argument("Invalid FEM condition geometry reference.");
            owned.surface_seeds.push_back({point.at(0).get<double>(),point.at(1).get<double>(),point.at(2).get<double>()});
        }
        owned.surface_region_keys=projection.at("surface_region_keys").get<std::vector<std::string>>();
        owned.project_to_body=projection.at("project_to_body").get<bool>();
        n.projected_sketch=std::move(owned);
    }
    n.force_N=j.at("force_N");n.pressure_MPa=j.at("pressure_MPa");
    const auto& restrained=j.at("restrained");
    if(!restrained.is_array()||restrained.size()!=3)throw std::invalid_argument("Invalid FEM condition definition.");
    n.restrained=restrained.get<std::array<bool,3>>();
    n.pressure_reversed=j.at("pressure_reversed");n.visible=j.at("visible");
    if(j.contains("calculation_error"))n.calculation_error=j.at("calculation_error").get<std::string>();
    validate_fem_condition(n);v=std::move(n);
}
} // namespace zima::document

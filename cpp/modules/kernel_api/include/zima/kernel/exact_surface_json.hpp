#pragma once
#include <zima/kernel/exact_surface.hpp>
#include <zima/kernel/bspline_json.hpp>

namespace zima::kernel {
namespace exact_surface_json_detail {
using Json=nlohmann::json;
inline Json point(Vec3 p) {return {p.x,p.y,p.z};}
inline Vec3 point(const Json& p) {
    if(!p.is_array()||p.size()!=3)throw std::invalid_argument("Invalid exact surface data");
    return {p.at(0).get<double>(),p.at(1).get<double>(),p.at(2).get<double>()};
}
template<class Data> Json frame(const Data& data) {
    return {{"origin",point(data.origin)},{"x",point(data.x_axis)},
        {"y",point(data.y_axis)},{"z",point(data.z_axis)}};
}
template<class Data> void frame(Data& data,const Json& value) {
    data.origin=point(value.at("origin"));data.x_axis=point(value.at("x"));
    data.y_axis=point(value.at("y"));data.z_axis=point(value.at("z"));
}
inline Json generator(const ExactSurfaceGenerator& value) {
    auto result=frame(value);result["kind"]=static_cast<int>(value.kind);
    result["radius"]=value.radius;result["minor_radius"]=value.minor_radius;
    result["offset"]=value.offset;
    if(value.kind==ExactSurfaceGenerator::Kind::Spline)result["spline"]=spline_json(value.spline);
    if(value.basis)result["basis"]=generator(*value.basis);
    return result;
}
inline std::shared_ptr<const ExactSurfaceGenerator> generator(const Json& value) {
    auto result=std::make_shared<ExactSurfaceGenerator>();
    const int kind=value.at("kind");
    if(kind<0||kind>static_cast<int>(ExactSurfaceGenerator::Kind::Offset))
        throw std::invalid_argument("Invalid exact surface generator");
    result->kind=static_cast<ExactSurfaceGenerator::Kind>(kind);frame(*result,value);
    result->radius=value.at("radius");result->minor_radius=value.at("minor_radius");result->offset=value.at("offset");
    if(result->kind==ExactSurfaceGenerator::Kind::Spline) {
        const auto curve=spline_from_json(value.at("spline"));
        if(!curve)throw std::invalid_argument("Invalid exact surface generator");
        result->spline=*curve;
    }
    if(result->kind==ExactSurfaceGenerator::Kind::Offset)result->basis=generator(value.at("basis"));
    result->validate();return result;
}
inline Json support(const ExactSurface& value) {
    auto result=frame(value);result["kind"]=static_cast<int>(value.kind);
    result["radius"]=value.radius;result["minor_radius"]=value.minor_radius;
    result["semi_angle"]=value.semi_angle;result["offset"]=value.offset;
    if(value.kind==ExactSurface::Kind::Spline) {
        auto poles=Json::array();for(auto p:value.poles)poles.push_back(point(p));
        result["spline"]={{"u_degree",value.u_degree},{"v_degree",value.v_degree},
            {"u_count",value.u_count},{"v_count",value.v_count},
            {"u_knots",value.u_knots},{"v_knots",value.v_knots},
            {"weights",value.weights},{"poles",std::move(poles)}};
    }
    if(value.generator)result["generator"]=generator(*value.generator);
    if(value.basis)result["basis"]=support(*value.basis);
    return result;
}
inline std::shared_ptr<const ExactSurface> support(const Json& value) {
    auto result=std::make_shared<ExactSurface>();const int kind=value.at("kind");
    if(kind<0||kind>static_cast<int>(ExactSurface::Kind::Offset))
        throw std::invalid_argument("Invalid exact surface data");
    result->kind=static_cast<ExactSurface::Kind>(kind);frame(*result,value);
    result->radius=value.at("radius");result->minor_radius=value.at("minor_radius");
    result->semi_angle=value.at("semi_angle");result->offset=value.at("offset");
    if(result->kind==ExactSurface::Kind::Spline) {
        const auto& data=value.at("spline");result->u_degree=data.at("u_degree");result->v_degree=data.at("v_degree");
        result->u_count=data.at("u_count");result->v_count=data.at("v_count");
        result->u_knots=data.at("u_knots").get<std::vector<double>>();
        result->v_knots=data.at("v_knots").get<std::vector<double>>();
        result->weights=data.at("weights").get<std::vector<double>>();
        for(const auto& p:data.at("poles"))result->poles.push_back(point(p));
    }
    if(result->kind==ExactSurface::Kind::Extrusion||result->kind==ExactSurface::Kind::Revolution)
        result->generator=generator(value.at("generator"));
    if(result->kind==ExactSurface::Kind::Offset)result->basis=support(value.at("basis"));
    result->validate();return result;
}
}
inline nlohmann::json exact_surface_json(const ExactSurfaceGeometry& value) {
    value.validate();auto result=exact_surface_json_detail::frame(value);
    result["support"]=exact_surface_json_detail::support(*value.support);
    result["domain"]={value.u_min,value.u_max,value.v_min,value.v_max};
    result["reversed"]=value.reversed;result["trims"]=nlohmann::json::array();
    for(const auto& loop:value.trim_loops) {
        auto curves=nlohmann::json::array();for(const auto& edge:loop)curves.push_back(spline_json(edge));
        result["trims"].push_back(std::move(curves));
    }
    return result;
}
inline std::shared_ptr<const ExactSurfaceGeometry> exact_surface_from_json(const nlohmann::json& value) {
    auto result=std::make_shared<ExactSurfaceGeometry>();exact_surface_json_detail::frame(*result,value);
    result->support=exact_surface_json_detail::support(value.at("support"));
    const auto& domain=value.at("domain");
    if(!domain.is_array()||domain.size()!=4)throw std::invalid_argument("Invalid exact surface frame or domain");
    result->u_min=domain.at(0);result->u_max=domain.at(1);result->v_min=domain.at(2);result->v_max=domain.at(3);
    result->reversed=value.at("reversed");
    for(const auto& loop:value.at("trims")) {
        std::vector<BSplineGeometry> curves;
        for(const auto& edge:loop) {
            auto curve=spline_from_json(edge);
            if(!curve)throw std::invalid_argument("Invalid exact surface trim loop");
            curves.push_back(std::move(*curve));
        }
        result->trim_loops.push_back(std::move(curves));
    }
    result->validate();return result;
}
}

#pragma once
#include <zima/commands/dispatcher.hpp>
#include <array>
#include <algorithm>
#include <cmath>
namespace zima::test {
// Build a real editable rectangular profile through public commands. The
// convenience fixture is not registered as an application command.
template<class Execute>
commands::Result create_rectangular_profile(Execute execute,double x,double y,double z) {
    using commands::Json;
    const auto run=[&](const char* name,Json args){return execute(Json{{"command",name},{"arguments",std::move(args)}});};
    auto created=run("sketch.create",{{"name","Fixture profile"},{"plane","XY"}});
    if(!created.ok)return created;
    const auto sketch=created.data.at("sketch").template get<std::string>();
    const std::array<std::array<double,2>,4> points{{{-x/2,-y/2},{x/2,-y/2},{x/2,y/2},{-x/2,y/2}}};
    for(std::size_t i=0;i<4;++i) {
        auto result=run("sketch.segment.create",{{"sketch",sketch},{"first",points[i]},
            {"second",points[(i+1)%4]},{"snap_mm",.000001}});
        if(!result.ok)return result;
    }
    return run("extrusion.create",{{"sketch",sketch},{"extent","symmetric"},{"length_forward_mm",z/2}});
}
// Compose public commands for tests whose subject is a downstream operation.
// The callback keeps each test's normal error handling and return type.
template<class Run>
auto rectangular_commands(Run run,commands::Json dimensions) {
    using commands::Json;
    const auto number=[&](const char* name,double fallback){
        if(!dimensions.contains(name))return fallback;
        const auto& value=dimensions.at(name);return value.is_string()?std::stod(value.template get<std::string>()):value.template get<double>();};
    const double x=number("length_mm",100),y=number("width_mm",80),z=number("height_mm",50);
    const auto data=[](auto result)->Json {
        if constexpr(requires {result.data;})return result.data;
        else return result;
    };
    const auto sketch=data(run("sketch.create",{{"name","Fixture profile"},{"plane","XY"}})).at("sketch").template get<std::string>();
    const std::array<std::array<double,2>,4> points{{{-x/2,-y/2},{x/2,-y/2},{x/2,y/2},{-x/2,y/2}}};
    for(std::size_t i=0;i<4;++i)run("sketch.segment.create",{{"sketch",sketch},{"first",points[i]},
        {"second",points[(i+1)%4]},{"snap_mm",.000001}});
    Json args={{"sketch",sketch},{"extent","symmetric"},{"length_forward_mm",z/2}};
    for(const auto* key:{"name","combine","placement"})if(dimensions.contains(key))args[key]=dimensions[key];
    return run("extrusion.create",args);
}

template<class Run>
auto circular_commands(Run run,commands::Json dimensions) {
    using commands::Json;
    const auto number=[&](const char* name,double fallback){if(!dimensions.contains(name))return fallback;
        const auto& v=dimensions.at(name);return v.is_string()?std::stod(v.template get<std::string>()):v.template get<double>();};
    const auto data=[](auto result)->Json {if constexpr(requires {result.data;})return result.data;else return result;};
    const auto sketch=data(run("sketch.create",{{"name","Fixture profile"},{"plane","XY"}})).at("sketch").template get<std::string>();
    run("sketch.circle.create",{{"sketch",sketch},{"center",{0,0}},{"radius_mm",number("radius_mm",40)}});
    return run("extrusion.create",{{"sketch",sketch},{"length_forward_mm",number("height_mm",50)}});
}
template<class Run>
auto spherical_commands(Run run,double radius) {
    using commands::Json;
    const auto data=[](auto result)->Json {if constexpr(requires {result.data;})return result.data;else return result;};
    const auto sketch=data(run("sketch.create",{{"name","Fixture profile"},{"plane","XZ"}})).at("sketch").template get<std::string>();
    run("sketch.arc.create",{{"sketch",sketch},{"center",{0,0}},{"start",{0,-radius}},{"end",{0,radius}}});
    run("sketch.segment.create",{{"sketch",sketch},{"first",{0,radius}},{"second",{0,-radius}}});
    const auto axis=data(run("sketch.segment.create",{{"sketch",sketch},{"first",{0,radius+1}},{"second",{0,-radius-1}}})).at("geometry").template get<std::string>();
    run("sketch.segment.centerline",{{"sketch",sketch},{"segment",axis},{"centerline",true}});
    return run("revolution.create",{{"sketch",sketch},{"axis",axis},{"angle_degrees",360}});
}

template<class Run>
auto resize_rectangular_commands(Run run,commands::Json dimensions) {
    using commands::Json;
    const auto data=[](auto result)->Json {if constexpr(requires {result.data;})return result.data;else return result;};
    const auto number=[&](const char* name){const auto& v=dimensions.at(name);return v.is_string()?std::stod(v.template get<std::string>()):v.template get<double>();};
    Json owner={{"container",dimensions.at("container")}};
    if(dimensions.contains("document"))owner["document"]=dimensions["document"];
    if(dimensions.contains("length_mm")||dimensions.contains("width_mm")) {
        const auto sketch=data(run("extrusion.get",owner)).at("sketch");
        const auto entities=data(run("sketch.entities",{{"sketch",sketch}})).at("items");
        Json points=Json::array(),operations=Json::array();double hx=0,hy=0;
        for(const auto& item:entities)if(item.at("kind")=="point") {
            auto point=data(run("sketch.entity.get",{{"sketch",sketch},{"entity",item.at("id")}})).at("entity");
            hx=std::max(hx,std::abs(point.at("x").template get<double>()));hy=std::max(hy,std::abs(point.at("y").template get<double>()));points.push_back(point);
        }
        const double sx=dimensions.contains("length_mm")?number("length_mm")/(2*hx):1;
        const double sy=dimensions.contains("width_mm")?number("width_mm")/(2*hy):1;
        for(const auto& p:points)operations.push_back({{"command","sketch.point.move"},{"arguments",{{"point",p.at("id")},{"position",{p.at("x").template get<double>()*sx,p.at("y").template get<double>()*sy}}}}});
        auto args=owner;args["operations"]=operations;
        if(!dimensions.contains("height_mm"))return run("extrusion.sketch.edit",args);
        run("extrusion.sketch.edit",args);
    }
    owner["length_forward_mm"]=number("height_mm")/2;
    return run("extrusion.set",owner);
}

}

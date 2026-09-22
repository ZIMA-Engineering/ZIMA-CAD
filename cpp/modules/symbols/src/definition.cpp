#include <zima/symbols/definition.hpp>
#include <nlohmann/json.hpp>
#include <QFile>
#include <QSaveFile>
#include <cmath>
#include <set>
#include <stdexcept>

namespace zima::symbols {
namespace {
using Json = nlohmann::json;
QString qpath(const std::filesystem::path& path) {
    const auto bytes=path.u8string();return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()),static_cast<qsizetype>(bytes.size()));
}
void require(bool valid) {if(!valid)throw std::invalid_argument("Invalid symbol definition");}
}
void Definition::validate() const {
    require(!id.empty()&&!name.empty()&&std::isfinite(insertion_point[0])&&std::isfinite(insertion_point[1]));
    sketch.validate();
    require(sketch.external_references.empty()&&sketch.plane_reference_owner_id.empty()&&!sketch.drawing_template);
    require(variants.contains(default_variant)&&!groups.empty());
    std::set<std::string> curves;
    for(const auto& v:sketch.segments)curves.insert(v.id);
    for(const auto& v:sketch.circles)curves.insert(v.id);
    for(const auto& v:sketch.arcs)curves.insert(v.id);
    for(const auto& v:sketch.ellipses)curves.insert(v.id);
    for(const auto& v:sketch.elliptical_arcs)curves.insert(v.id);
    for(const auto& v:sketch.bsplines)curves.insert(v.id);
    for(const auto& v:sketch.texts)curves.insert(v.id);
    std::set<std::string> assigned;
    for(const auto& [key, group]:groups) {
        require(!key.empty()&&!group.geometry.empty());
        for(const auto& entity:group.geometry)require(curves.contains(entity)&&assigned.insert(entity).second);
    }
    require(assigned==curves);
    for(const auto& [key, members]:variants) {
        require(!key.empty()&&!members.empty());std::set<std::string> used;
        for(const auto& group:members)require(groups.contains(group)&&used.insert(group).second);
    }
}
std::vector<std::string> Definition::visible_geometry(const std::string& variant) const {
    validate();std::vector<std::string> result;
    for(const auto& group:variants.at(variant)) {
        const auto& ids=groups.at(group).geometry;result.insert(result.end(),ids.begin(),ids.end());
    }
    return result;
}
std::string Definition::serialized() const {
    validate();Json group_data=Json::object();
    for(const auto& [id, group]:groups)group_data[id]={{"geometry",group.geometry},{"axis",group.axis}};
    return Json{{"format","zima.symbol"},{"version",1},{"units","mm"},{"id",id},{"name",name},
        {"insertion_point",insertion_point},{"sketch",Json::parse(sketch.serialized())},
        {"groups",group_data},{"variants",variants},{"default_variant",default_variant},{"variant_source",variant_source}}.dump(2)+"\n";
}
Definition Definition::from_serialized(const std::string& data) {
    const auto value=Json::parse(data);
    require(value.at("format")=="zima.symbol"&&value.at("version")==1&&value.at("units")=="mm");
    Definition result;result.id=value.at("id");result.name=value.at("name");
    result.insertion_point=value.at("insertion_point").get<std::array<double,2>>();
    result.sketch=sketcher::Sketch::from_serialized(value.at("sketch").dump());
    for(const auto& [id, group]:value.at("groups").items())result.groups[id]={group.at("geometry").get<std::vector<std::string>>(),group.at("axis").get<bool>()};
    result.variants=value.at("variants").get<decltype(result.variants)>();
    result.default_variant=value.at("default_variant");result.variant_source=value.at("variant_source");
    result.validate();return result;
}
Definition Definition::load(const std::filesystem::path& path) {
    QFile file(qpath(path));if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read symbol definition");
    return from_serialized(file.readAll().toStdString());
}
void Definition::save(const std::filesystem::path& path) const {
    const auto bytes=serialized();QSaveFile file(qpath(path));
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes.data(),static_cast<qint64>(bytes.size()))!=static_cast<qint64>(bytes.size())||!file.commit())
        throw std::runtime_error("Cannot save symbol definition");
}
Definition projection_method() {
    Definition d;d.id="ze:projection-method";d.name="ZE-PROJECTION-METHOD";
    d.sketch=sketcher::Sketch::create_default();d.sketch.id="ze:projection-method:sketch";d.sketch.name=d.name;
    d.default_variant="first_angle";d.variant_source="drawing.projection_method";
    // One local Sketch. Each variant selects geometry groups, never another Sketch.
    // ISO 5456-2 figures 4 and 7: the cone widens to the right in both variants.
    for(bool first:{true,false}) {
        const std::string key=first?"first_angle":"third_angle";
        auto& outline=d.groups[key+":outline"];auto& axes=d.groups[key+":axes"];axes.axis=true;
        const double left=first?-7.2:1.2, right=left+6., center=first?4.2:-4.2;
        const auto point=[&](std::string id,double x,double y) {
            id=key+":"+id;d.sketch.points.push_back({id,x,y,true,false});return id;
        };
        const auto a=point("cone-small-bottom",left,-1.5), b=point("cone-large-bottom",right,-3.);
        const auto c=point("cone-large-top",right,3.), e=point("cone-small-top",left,1.5);
        const auto segment=[&](std::string id,const std::string& p,const std::string& q,bool axis) {
            id=key+":"+id;d.sketch.segments.push_back({id,p,q,axis,false});
            (axis?axes:outline).geometry.push_back(id);
        };
        segment("cone-bottom",a,b,false);segment("cone-large",b,c,false);
        segment("cone-top",c,e,false);segment("cone-small",e,a,false);
        const auto center_id=point("circle-center",center,0);
        for(bool outer:{true,false}) {
            const std::string id=key+(outer?":outer-circle":":inner-circle");
            d.sketch.circles.push_back({id,center_id,outer?3.:1.5,false});outline.geometry.push_back(id);
        }
        segment("horizontal-axis",point("axis-left",-7.8,0),point("axis-right",7.8,0),true);
        segment("vertical-axis",point("axis-bottom",center,-3.6),point("axis-top",center,3.6),true);
        d.variants[key]={key+":outline",key+":axes"};
    }
    d.validate();return d;
}
}

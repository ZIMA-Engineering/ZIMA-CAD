#include <zima/document/measurement_record.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace zima::document {
namespace {
using J=nlohmann::json;using namespace zima::kernel;
const std::array<const char*,6> kinds{"point","curve","face","object","axis","plane"};
J point(Vec3 p){return J::array({p.x,p.y,p.z});}
Vec3 point(const J& j){
    if(!j.is_array()||j.size()!=3)throw std::runtime_error("Invalid measurement point");
    Vec3 p{j[0].get<double>(),j[1].get<double>(),j[2].get<double>()};
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw std::runtime_error("Non-finite measurement point");
    return p;
}
J value(const std::optional<MeasurementValue>& v){return v?J{{"value",v->value},{"approximate",v->approximate}}:J(nullptr);}
std::optional<MeasurementValue> value(const J& j){
    if(j.is_null())return {};MeasurementValue v{j.at("value"),j.at("approximate")};
    if(!std::isfinite(v.value)||v.value<0)throw std::runtime_error("Invalid measurement value");return v;
}
}
std::string serialize_measurements(const std::vector<kernel::SavedMeasurement>& records){
    J rows=J::array();
    for(const auto& record:records){
        J refs=J::array(),values=J::array();
        for(const auto& r:record.references)refs.push_back({{"kind",kinds.at(static_cast<std::size_t>(r.kind))},{"owner",r.owner_id},{"key",r.semantic_key},{"instance_path",r.instance_path}});
        for(const auto& v:record.values)values.push_back({{"position",v.position?point(*v.position):J(nullptr)},
            {"length",value(v.length)},{"area",value(v.area)},{"volume",value(v.volume)},{"mass",value(v.mass)}});
        rows.push_back({{"id",record.id},{"name",record.name},{"body_id",record.body_id},{"after_object_id",record.after_object_id},
            {"references",refs},{"values",values},{"distance",record.distance?J{{"value",value(record.distance->distance)},
                {"first",point(record.distance->first)},{"second",point(record.distance->second)}}:J(nullptr)}});
    }
    return rows.dump();
}
std::vector<kernel::SavedMeasurement> parse_measurements(const std::string& source){
    const auto rows=J::parse(source);if(!rows.is_array())throw std::runtime_error("Invalid measurements");
    std::vector<kernel::SavedMeasurement> records;std::set<std::string> ids;
    for(const auto& row:rows){
        kernel::SavedMeasurement r;r.id=row.at("id");r.name=row.at("name");r.body_id=row.at("body_id");r.after_object_id=row.at("after_object_id");
        if(r.id.empty()||r.name.empty()||!ids.insert(r.id).second)throw std::runtime_error("Invalid measurement identity");
        for(const auto& entry:row.at("references")){
            const std::string kind=entry.at("kind");const auto it=std::ranges::find(kinds,kind);
            if(it==kinds.end())throw std::runtime_error("Invalid measurement reference kind");
            kernel::MeasurementReference ref{static_cast<kernel::MeasurementKind>(it-kinds.begin()),entry.at("owner"),entry.at("key"),entry.at("instance_path")};
            if(ref.kind==kernel::MeasurementKind::Object?(ref.owner_id.empty()&&ref.instance_path.empty()):(ref.owner_id.empty()||ref.semantic_key.empty()))
                throw std::runtime_error("Invalid measurement reference");
            r.references.push_back(std::move(ref));
        }
        for(const auto& entry:row.at("values")){
            kernel::MeasurementValues v;if(!entry.at("position").is_null())v.position=point(entry.at("position"));
            v.length=value(entry.at("length"));v.area=value(entry.at("area"));v.volume=value(entry.at("volume"));v.mass=value(entry.at("mass"));r.values.push_back(std::move(v));
        }
        if(r.references.empty()||r.references.size()>2||r.values.size()!=r.references.size())throw std::runtime_error("Invalid measurement reference count");
        if(!row.at("distance").is_null()){
            const auto& d=row.at("distance");const auto v=value(d.at("value"));
            if(!v||r.references.size()!=2)throw std::runtime_error("Invalid measurement distance");
            r.distance=kernel::MeasurementDistance{*v,point(d.at("first")),point(d.at("second"))};
        }
        records.push_back(std::move(r));
    }
    return records;
}
}
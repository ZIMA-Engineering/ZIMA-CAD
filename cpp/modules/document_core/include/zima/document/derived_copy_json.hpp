#pragma once
#include <zima/document/derived_copy.hpp>
#include <nlohmann/json.hpp>
namespace zima::document {
inline void to_json(nlohmann::json& j,const DerivedCopyParameters& m) {
    const auto& p=m.resolved_plane;const auto& r=m.reference;
    j={{"source_id",m.source_id},{"reference",{{"owner_id",r.owner_id},{"semantic_key",r.semantic_key},{"instance_path",r.instance_path},{"offset",r.offset}}},
        {"point",{p.point.x,p.point.y,p.point.z}},{"normal",{p.normal.x,p.normal.y,p.normal.z}},
        {"reference_valid",m.reference_valid},{"pattern",nullptr}};
    if(m.pattern){const auto& p=*m.pattern;j["pattern"]={{"circular",p.circular},{"count",p.count},{"angle_degrees",p.angle_degrees},
        {"full_circle",p.full_circle},{"origin",{p.origin.x,p.origin.y,p.origin.z}},{"axis",{p.axis.x,p.axis.y,p.axis.z}},{"linear",nlohmann::json::array()}};
        for(const auto& d:p.linear)j["pattern"]["linear"].push_back({{"local_axis",d.local_axis},{"count",d.count},
            {"reverse_count",d.reverse_count},{"distribution",static_cast<int>(d.distribution)},{"spacing",d.spacing},
            {"direction",{d.direction.x,d.direction.y,d.direction.z}}});
    }
}
inline void from_json(const nlohmann::json& j,DerivedCopyParameters& m) {
    m.source_id=j.at("source_id");const auto& r=j.at("reference");
    m.reference={r.at("instance_path"),r.at("owner_id"),r.at("semantic_key"),r.at("offset")};
    const auto& p=j.at("point");const auto& n=j.at("normal");
    m.resolved_plane=kernel::normalized_mirror_plane({{p.at(0),p.at(1),p.at(2)},{n.at(0),n.at(1),n.at(2)}});
    m.reference_valid=j.at("reference_valid");
    if(!j.at("pattern").is_null()) {const auto& p=j.at("pattern");kernel::PatternRequest pattern;
        pattern.circular=p.at("circular");pattern.count=p.at("count");pattern.angle_degrees=p.at("angle_degrees");pattern.full_circle=p.at("full_circle");
        const auto vector=[&](const char* key){const auto& v=p.at(key);return kernel::Vec3{v.at(0),v.at(1),v.at(2)};};
        pattern.origin=vector("origin");pattern.axis=vector("axis");
        if(p.at("linear").size()!=3)throw std::invalid_argument("Pole potřebuje tři místní směrové sloty.");
        for(std::size_t i=0;i<3;++i){const auto& value=p.at("linear").at(i);auto& d=pattern.linear[i];
            d.local_axis=value.at("local_axis");d.count=value.at("count");d.reverse_count=value.at("reverse_count");
            if(d.local_axis < -1 || d.local_axis > 2)throw std::invalid_argument("Neplatná místní osa Pole.");
            d.distribution=static_cast<kernel::PatternDistribution>(value.at("distribution").get<int>());d.spacing=value.at("spacing");
            const auto& v=value.at("direction");d.direction={v.at(0),v.at(1),v.at(2)};
        }
        static_cast<void>(kernel::validated_pattern(pattern));m.pattern=pattern;
    }
}
}

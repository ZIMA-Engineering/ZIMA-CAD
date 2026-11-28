#include <zima/document/groove_catalog.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace zima::document {
std::string_view bundled_groove_catalog();
const std::vector<GrooveCatalog>& groove_catalogues() {
    static const auto all=[] {
        const auto root=nlohmann::json::parse(bundled_groove_catalog());
        if(root.at("schema")!=1)throw std::runtime_error("Invalid groove catalogue schema");
        std::vector<GrooveCatalog> result;
        for(const auto& item:root.at("catalogues")) {
            GrooveCatalog c;c.id=item.at("id");c.standard=item.at("standard");c.source=item.at("source");
            c.internal=item.at("internal");
            const std::string profile=item.at("profile");
            if(profile!="rectangular"&&profile!="circular")throw std::runtime_error("Invalid groove catalogue profile");
            c.profile=profile=="circular"?GrooveProfile::Circular:GrooveProfile::Rectangular;
            double previous=0;
            for(const auto& j:item.at("rows")) {
                GrooveCatalogRow r;
#define READ(field) j.at(#field).get_to(r.field)
                READ(id);READ(diameter);READ(groove_diameter);READ(width_min);READ(width_max);READ(radius);
#undef READ
                if(!(std::isfinite(r.diameter)&&r.diameter>previous&&r.groove_diameter>0&&
                    (c.internal?r.groove_diameter>r.diameter:r.groove_diameter<r.diameter)&&
                    r.width_min>0&&r.width_max>=r.width_min&&std::isfinite(r.width_max)&&
                    (c.profile==GrooveProfile::Rectangular||r.radius>0)))
                    throw std::runtime_error("Invalid groove catalogue row");
                previous=r.diameter;c.rows.push_back(std::move(r));
            }
            result.push_back(std::move(c));
        }
        return result;
    }();return all;
}
const GrooveCatalogRow& groove_size(std::string_view id,double diameter) {
    for(const auto& c:groove_catalogues())if(c.id==id) {
        for(const auto& r:c.rows)if(std::abs(r.diameter-diameter)<=1e-7)return r;
        throw std::invalid_argument("No groove size is available for this support diameter");
    }
    throw std::invalid_argument("Groove catalogue is unavailable");
}
void select_groove_size(MechGrooveParameters& p) {
    if(p.catalogue.empty()){p.row.clear();return;}
    const auto& all=groove_catalogues();
    const auto c=std::ranges::find(all,p.catalogue,&GrooveCatalog::id);
    if(c==all.end()||c->profile!=p.profile||c->internal!=p.internal)
        throw std::invalid_argument("Groove catalogue does not match the selected profile and material side");
    const auto& r=groove_size(p.catalogue,p.support_diameter);
    if(!std::isfinite(p.width_position)||p.width_position<0||p.width_position>1)
        throw std::invalid_argument("Groove width is outside the selected range");
    p.row=r.id;p.depth=std::abs(r.diameter-r.groove_diameter)/2;
    p.width=r.width_min+(r.width_max-r.width_min)*p.width_position;
    if(p.profile==GrooveProfile::Circular)p.radius=r.radius;
}
}

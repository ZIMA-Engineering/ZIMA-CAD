#include <zima/document/keyway_catalog.hpp>
#include <zima/document/thread_catalog.hpp>
#include <nlohmann/json.hpp>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace zima::document {
std::string_view bundled_keyway_catalog();
namespace {
std::string deviation(double n){
    if(n==0)return "0";
    char buffer[64];const auto result=std::to_chars(buffer,buffer+64,n);
    if(result.ec!=std::errc{})throw std::runtime_error("Invalid keyway deviation");
    return (n>0?"+":"")+std::string(buffer,result.ptr);
}
}
const std::vector<KeywayCatalog>& keyway_catalogues(){
    static const auto all=[] {
        std::vector<KeywayCatalog> result;
        const auto root=nlohmann::json::parse(bundled_keyway_catalog());
        if(root.at("schema")!=1)throw std::runtime_error("Invalid keyway catalogue schema");
        for(const auto& c:root.at("catalogues")){
            KeywayCatalog catalogue;catalogue.id=c.at("id");catalogue.source=c.at("source");catalogue.review=c.at("review");
            const auto type=c.at("type").get<std::string>();
            if(type!="parallel"&&type!="woodruff")throw std::runtime_error("Invalid keyway catalogue type");
            catalogue.type=type=="woodruff"?KeywayType::Woodruff:KeywayType::Parallel;
            for(const auto& j:c.at("rows")){
                KeywayCatalogRow r;
#define READ(field) j.at(#field).get_to(r.field)
                READ(id);READ(diameter_over);READ(width);READ(height);READ(shaft_depth);READ(hub_depth);
                r.diameter_through=j.at("diameter_through").is_null()?std::numeric_limits<double>::infinity():j.at("diameter_through").get<double>();
                READ(bottom_radius);READ(disc_diameter);READ(shaft_dimension_upper);READ(shaft_dimension_lower);
                READ(hub_dimension_upper);READ(hub_dimension_lower);READ(shaft_width_fit);READ(hub_width_fit);
#undef READ
                r.shaft_dimension_fit=j.value("shaft_dimension_fit",std::string{});r.hub_dimension_fit=j.value("hub_dimension_fit",std::string{});
                if(!j.at("retaining").is_null()){
                    const auto& t=j.at("retaining");r.retaining_thread=t.at("thread");r.retaining_depth=t.at("thread_depth");
                    if(!t.at("one_lengths").is_null())r.one_screw_lengths=t.at("one_lengths").get<std::array<double,2>>();
                    if(!t.at("two_lengths").is_null())r.two_screw_lengths=t.at("two_lengths").get<std::array<double,2>>();
                }
                if(!(r.diameter_through>r.diameter_over&&r.width>0&&r.height>0&&r.shaft_depth>0&&r.hub_depth>0))
                    throw std::runtime_error("Invalid keyway catalogue row");
                catalogue.rows.push_back(std::move(r));
            }
            result.push_back(std::move(catalogue));
        }return result;
    }();return all;
}
const KeywayCatalogRow& keyway_size(std::string_view id,double diameter,std::string_view preferred){
    if(!std::isfinite(diameter)||diameter<=0)throw std::invalid_argument("Invalid keyway support diameter");
    for(const auto& catalogue:keyway_catalogues())if(catalogue.id==id){
        const KeywayCatalogRow* first=nullptr;
        for(const auto& row:catalogue.rows)if(diameter>row.diameter_over&&diameter<=row.diameter_through){
            if(row.id==preferred)return row;if(!first)first=&row;
        }
        if(first)return *first;
        throw std::invalid_argument("No keyway size is available for this support diameter");
    }
    throw std::invalid_argument("Keyway catalogue is unavailable");
}
void select_keyway_size(MechKeywayParameters& p,double diameter){
    const auto& catalogues=keyway_catalogues();
    const auto catalogue=std::ranges::find(catalogues,p.table_id,&KeywayCatalog::id);
    if(catalogue==catalogues.end()||catalogue->type!=p.type)
        throw std::invalid_argument("Keyway catalogue does not match the selected key type");
    const auto& row=keyway_size(p.table_id,diameter,p.table_row_id);
    p.table_row_id=row.id;p.width=row.width;p.key_height=row.height;p.depth=p.internal?row.hub_depth:row.shaft_depth;
    p.bottom_radius=row.bottom_radius;p.woodruff_diameter=row.disc_diameter;
    p.width_fit=p.internal?row.hub_width_fit:row.shaft_width_fit;
    p.depth_fit=p.internal?row.hub_dimension_fit:row.shaft_dimension_fit;
    p.depth_upper_tolerance=deviation(p.internal?row.hub_dimension_upper:row.shaft_dimension_upper);
    p.depth_lower_tolerance=deviation(p.internal?row.hub_dimension_lower:row.shaft_dimension_lower);
    if(p.type==KeywayType::Woodruff&&!p.internal&&!row.shaft_dimension_fit.empty()) {
        // Historical h11 belongs to D-t, not to the direct t1 dimension.
        // Keep unreviewed converted deviations unset rather than relabel them.
        p.depth_fit.clear();p.depth_upper_tolerance.clear();p.depth_lower_tolerance.clear();
    }
    if(p.retaining_screws){
        const auto interval=p.retaining_screws==1?row.one_screw_lengths:row.two_screw_lengths;
        if(!interval||p.length<(*interval)[0]||p.length>(*interval)[1])
            throw std::invalid_argument("Retaining screws are unavailable for this key size and length");
        const auto& metric=thread_catalog("metric");
        const auto size=std::ranges::find(metric,row.retaining_thread,&ThreadCatalogSize::designation);
        if(size==metric.end())throw std::invalid_argument("Retaining screw thread is unavailable");
        p.retaining_diameter=size->nominal_diameter;p.retaining_pitch=size->pitch;p.retaining_depth=row.retaining_depth;
    }
}
}

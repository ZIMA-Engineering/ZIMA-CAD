#include <zima/document/named_views.hpp>
#include <zima/document/metadata.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace zima::document {
namespace {
using Json=nlohmann::json;
constexpr auto invalid="Named views require a name, a rotation quaternion, zoom, pan and reference scale.";
float number(const Json& value) {
    if(!value.is_number())throw std::invalid_argument(invalid);
    const auto result=value.get<float>();
    if(!std::isfinite(result))throw std::invalid_argument(invalid);
    return result;
}
}
void normalize_named_view(NamedView& view) {
    if(view.name.empty() || view.name.size()>1024 ||
       std::ranges::any_of(view.name,[](unsigned char c){return c<32 || c==127;}))
        throw std::invalid_argument("A view name must contain 1 to 1024 bytes without control characters.");
    validate_native_metadata_text(view.name);
    if(!std::ranges::all_of(view.camera,[](float v){return std::isfinite(v);}) ||
       view.camera[4]<=0 || view.camera[7]<=0)
        throw std::invalid_argument("View camera values must be finite and both scales must be positive.");
    const double length=std::hypot(std::hypot(double(view.camera[0]),double(view.camera[1])),
                                   std::hypot(double(view.camera[2]),double(view.camera[3])));
    if(length==0)throw std::invalid_argument("A view rotation quaternion must be nonzero.");
    // Preserve an already normalized viewer float snapshot exactly on repeated saves.
    if(std::abs(length-1)>1e-6)for(std::size_t i=0;i<4;++i)view.camera[i]=float(view.camera[i]/length);
}
std::vector<NamedView> parse_named_views(const std::string& serialized) {
    try {
        const auto data=Json::parse(serialized);
        if(!data.is_array())throw std::invalid_argument(invalid);
        std::vector<NamedView> result;std::set<std::string> names;
        for(const auto& entry:data) {
            if(!entry.is_object() || entry.size()!=6 || !entry.at("name").is_string() ||
               !entry.at("rotation").is_array() || entry.at("rotation").size()!=4)
                throw std::invalid_argument(invalid);
            NamedView view;view.name=entry.at("name").get<std::string>();
            for(std::size_t i=0;i<4;++i)view.camera[i]=number(entry.at("rotation").at(i));
            view.camera[4]=number(entry.at("zoom"));view.camera[5]=number(entry.at("pan_x"));
            view.camera[6]=number(entry.at("pan_y"));view.camera[7]=number(entry.at("reference_scale"));
            normalize_named_view(view);
            if(!names.insert(view.name).second)throw std::invalid_argument("Named view names must be unique.");
            result.push_back(std::move(view));
        }
        return result;
    }catch(const Json::exception&){throw std::invalid_argument(invalid);}
}
std::string serialize_named_views(const std::vector<NamedView>& views) {
    Json result=Json::array();std::set<std::string> names;
    for(auto view:views) {
        normalize_named_view(view);
        if(!names.insert(view.name).second)throw std::invalid_argument("Named view names must be unique.");
        result.push_back({{"name",view.name},{"rotation",{view.camera[0],view.camera[1],view.camera[2],view.camera[3]}},
            {"zoom",view.camera[4]},{"pan_x",view.camera[5]},{"pan_y",view.camera[6]},{"reference_scale",view.camera[7]}});
    }
    // JSON also validates UTF-8 before any caller publishes the metadata.
    try{return result.dump();}catch(const Json::exception&){throw std::invalid_argument(invalid);}
}
}

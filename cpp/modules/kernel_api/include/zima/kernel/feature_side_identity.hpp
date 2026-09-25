#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <charconv>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace zima::kernel {

enum class FeatureSide { Start, End };

struct FeatureSideParent {
    std::string feature_id;
    FeatureSide side;
    std::string source_id;
    bool operator==(const FeatureSideParent&) const = default;
};

// Authored ancestry, defined before calculation. Operation kind and extent do
// not participate: replacing an Extrusion with a Revolution keeps the parent.
// Length prefixes permit arbitrary persisted IDs, including embedded colons.
inline std::string feature_side_parent_key(const FeatureSideParent& parent) {
    if(parent.feature_id.empty()||parent.source_id.empty()||
       (parent.side!=FeatureSide::Start&&parent.side!=FeatureSide::End))
        throw std::invalid_argument("Feature side ancestry requires a feature and source parent");
    return "feature-side:"+std::to_string(parent.feature_id.size())+":"+
        parent.feature_id+(parent.side==FeatureSide::Start?":start:":":end:")+
        std::to_string(parent.source_id.size())+":"+parent.source_id;
}

inline std::optional<FeatureSideParent> feature_side_parent(std::string_view key) {
    constexpr std::string_view prefix="feature-side:";
    if(!key.starts_with(prefix))return std::nullopt;
    key.remove_prefix(prefix.size());
    const auto field=[&]() -> std::optional<std::string> {
        const auto separator=key.find(':');
        if(separator==std::string_view::npos||separator==0||key.front()=='0')return std::nullopt;
        std::size_t count{};
        const auto parsed=std::from_chars(key.data(),key.data()+separator,count);
        if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+separator||count==0)
            return std::nullopt;
        key.remove_prefix(separator+1);
        if(count>key.size())return std::nullopt;
        std::string value(key.substr(0,count));key.remove_prefix(count);
        return value;
    };
    const auto feature=field();
    if(!feature)return std::nullopt;
    FeatureSide side;
    if(key.starts_with(":start:")){side=FeatureSide::Start;key.remove_prefix(7);}
    else if(key.starts_with(":end:")){side=FeatureSide::End;key.remove_prefix(5);}
    else return std::nullopt;
    const auto source=field();
    if(!source||!key.empty())return std::nullopt;
    return FeatureSideParent{*feature,side,*source};
}

// Scope the source parents of one side before constructing a FeatureGroup.
// Geometry is unchanged; the OCCT adapter still derives caps, rims and swept
// topology through its ordinary profile ancestry contract.
template<class Request> inline Request feature_side_request(
        Request request,const std::string& feature_id,FeatureSide side) {
    static_assert(std::is_same_v<Request,ExtrusionRequest>||
                  std::is_same_v<Request,RevolutionRequest>);
    const auto scope=[&](std::string& id) {
        if(!id.empty())id=feature_side_parent_key({feature_id,side,id});
    };
    const auto scope_profile=[&](auto& profile) {
        scope(profile.outer_boundary_id);
        for(auto& id:profile.inner_boundary_ids)scope(id);
        for(auto& id:profile.outer_edge_source_ids)scope(id);
        for(auto& ids:profile.inner_edge_source_ids)for(auto& id:ids)scope(id);
        for(auto& id:profile.outer_vertex_source_ids)scope(id);
        for(auto& ids:profile.inner_vertex_source_ids)for(auto& id:ids)scope(id);
    };
    scope(request.profile_region_id);scope_profile(request);
    for(auto& region:request.additional_profile_regions){scope(region.region_id);scope_profile(region);}
    scope(request.open_profile_end_id);
    if(request.wall)scope(request.wall->end_point_id);
    scope(request.centerlines.origin_id);scope(request.centerlines.profile_id);
    return request;
}

} // namespace zima::kernel

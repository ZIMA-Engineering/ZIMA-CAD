#pragma once
#include <zima/document/derived_copy.hpp>
#include <zima/document/dimension_identifiers.hpp>
#include <zima/kernel/dimension_layout.hpp>

namespace zima::document {
struct PatternDimension {
    std::string key;
    double value{};
    bool angular{}, count{}, writable{true};
};
inline std::vector<PatternDimension> pattern_dimension_values(const DerivedCopyParameters& copy) {
    std::vector<PatternDimension> result;
    if(!copy.pattern)return result;
    const auto& p=*copy.pattern;
    if(p.circular) {
        result.push_back({"pattern:angle",p.full_circle?360.0/p.count:p.angle_degrees,true,false,!p.full_circle});
        result.push_back({"pattern:count",static_cast<double>(p.count),false,true});
    } else for(std::size_t i=0;i<p.linear.size();++i) {
        const auto& d=p.linear[i];if(d.local_axis<0)continue;
        const auto index=std::to_string(i);
        result.push_back({"pattern:spacing:"+index,d.spacing});
        result.push_back({"pattern:count:"+index,static_cast<double>(d.count),false,true});
        if(d.distribution==kernel::PatternDistribution::Both)
            result.push_back({"pattern:reverse_count:"+index,static_cast<double>(d.reverse_count),false,true});
    }
    for(auto& d:result)d.writable=d.writable&&!copy.value_locks.contains(d.key);
    return result;
}
inline void append_pattern_dimension_parameters(std::vector<DimensionParameter>& out,
    const std::string& owner,const std::string& name,const DerivedCopyParameters& copy) {
    for(const auto& d:pattern_dimension_values(copy))out.push_back({owner,"parameter:"+d.key,name});
}
// Assignment changes only the authored scalar. Full-pattern validation belongs
// after all relation targets have been assigned, so dependent edits are atomic.
inline bool assign_pattern_dimension(DerivedCopyParameters& copy,const std::string& key,double value) {
    for(const auto& d:pattern_dimension_values(copy))if(d.key==key) {
        if(!d.writable)throw std::invalid_argument("The pattern dimension is read-only.");
        if(!std::isfinite(value))throw std::invalid_argument("The pattern dimension must be finite.");
        if(d.count&&(value!=std::floor(value)||value<(key.starts_with("pattern:reverse_count:")?1:2)||value>1000))
            throw std::invalid_argument("The pattern count must be a whole number within its allowed range.");
        auto& p=*copy.pattern;
        if(key=="pattern:angle")p.angle_degrees=value;
        else if(key=="pattern:count")p.count=static_cast<unsigned>(value);
        else {
            auto& direction=p.linear.at(static_cast<std::size_t>(key.back()-'0'));
            if(key.starts_with("pattern:spacing:")) {
                if(value<1e-9)throw std::invalid_argument("The pattern spacing must be positive.");
                direction.spacing=value;
            } else if(key.starts_with("pattern:reverse_count:"))direction.reverse_count=static_cast<unsigned>(value);
            else direction.count=static_cast<unsigned>(value);
        }
        return true;
    }
    return false;
}
inline std::vector<kernel::ViewerDimension> pattern_dimensions(const std::string& owner,const DerivedCopyParameters& copy) {
    std::vector<kernel::ViewerDimension> result;
    if(!copy.pattern)return result;
    const auto& p=*copy.pattern;
    const auto normal=kernel::normalized_mirror_plane({{},p.axis}).normal;
    const auto radial=kernel::dimension_unit(kernel::dimension_cross(normal,
        std::abs(normal.x)<0.8?kernel::Vec3{1,0,0}:kernel::Vec3{0,1,0}));
    const auto add=[](auto a,auto b,double scale=1.){return kernel::Vec3{a.x+b.x*scale,a.y+b.y*scale,a.z+b.z*scale};};
    for(const auto& value:pattern_dimension_values(copy)) {
        kernel::ViewerDimension d;
        d.reference={owner,"parameter:"+value.key,{}};d.value=value.value;
        d.driving=value.writable;d.locked=copy.value_locks.contains(value.key);d.value_lock_key=value.key;
        d.witness_first=p.origin;d.plane_normal=normal;
        if(value.count) {
            d.label_only=true;d.label_prefix="N = ";d.unit_suffix.clear();
            d.label_position=add(p.origin,radial,20.+12.*result.size());
            d.line_first=d.line_second=d.witness_second=*d.label_position;
        } else if(value.angular) {
            d.kind=kernel::ViewerDimensionKind::Angular;d.unit_suffix="°";d.sweep_degrees=value.value;
            d.line_first=add(p.origin,radial,20.);
            auto resolved=p;resolved.angle_degrees=value.value;
            d.line_second=kernel::pattern_point(d.line_first,resolved,1);d.witness_second=d.line_second;
        } else {
            const auto& direction=p.linear.at(static_cast<std::size_t>(value.key.back()-'0'));
            auto axis=kernel::normalized_mirror_plane({{},direction.direction}).normal;
            if(direction.distribution==kernel::PatternDistribution::Reverse)axis={-axis.x,-axis.y,-axis.z};
            const auto offset=kernel::dimension_unit(kernel::dimension_cross(axis,
                std::abs(axis.z)<0.8?kernel::Vec3{0,0,1}:kernel::Vec3{0,1,0}));
            d.witness_second=add(p.origin,axis,value.value);
            d.line_first=add(p.origin,offset,10.);d.line_second=add(d.witness_second,offset,10.);
            d.plane_normal=kernel::dimension_unit(kernel::dimension_cross(axis,offset));
        }
        result.push_back(std::move(d));
    }
    return result;
}
} // namespace zima::document

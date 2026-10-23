#pragma once
#include <zima/kernel/curve_plane_intersection.hpp>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>

namespace zima::sketcher {
struct SketchCurveIntersectionBinding {
    std::string curve_owner_id;
    std::string curve_semantic_key;
    unsigned degree{};
    std::vector<double> knots;
    double parameter_first{},parameter_last{};
    kernel::CurvePlaneContactKind contact{kernel::CurvePlaneContactKind::Crossing};
    bool closed{};
    std::string parameterization_key;
    std::string branch_key;
    int orientation{};
    std::size_t root_count{};
    bool operator==(const SketchCurveIntersectionBinding&) const = default;
};
inline std::string curve_intersection_branch_key(const SketchCurveIntersectionBinding& binding) {
    const auto bits=[](double value){char buffer[17];const auto encoded=std::to_chars(buffer,buffer+sizeof(buffer),std::bit_cast<std::uint64_t>(value),16);
        return std::string(buffer,encoded.ptr);};
    return "curve-plane-point:"+std::to_string(static_cast<unsigned>(binding.contact))+":"+
        std::to_string(binding.orientation)+":"+std::to_string(binding.root_count)+":"+
        bits(binding.parameter_first)+":"+bits(binding.parameter_last);
}
inline void validate_curve_intersection_binding(const SketchCurveIntersectionBinding& binding) {
    constexpr auto error="Sketch intersection binding is invalid";
    if(binding.curve_owner_id.empty()||binding.curve_semantic_key.empty()||binding.parameterization_key.empty()||binding.degree<1||
            binding.degree>=binding.knots.size()||binding.knots.size()-binding.degree-1<=binding.degree||
            !std::isfinite(binding.parameter_first)||!std::isfinite(binding.parameter_last)||
            binding.parameter_first>=binding.parameter_last||static_cast<unsigned>(binding.contact)>static_cast<unsigned>(kernel::CurvePlaneContactKind::Endpoint)||
            binding.root_count==0||binding.root_count>binding.knots.size()||
            (binding.contact==kernel::CurvePlaneContactKind::Endpoint?binding.orientation!=0:(binding.orientation!=-1&&binding.orientation!=1))||
            binding.branch_key!=curve_intersection_branch_key(binding))throw std::runtime_error(error);
    const auto count=binding.knots.size()-binding.degree-1;
    kernel::BSplineGeometry basis{binding.degree,std::vector<kernel::Vec3>(count),binding.knots,std::vector<double>(count,1)};basis.validate();
    const double wrap=binding.closed?binding.knots.back()-binding.knots.front():0;
    if(binding.parameter_first<binding.knots.front()-wrap||binding.parameter_last>binding.knots.back()+wrap||
            (binding.closed&&binding.parameter_last-binding.parameter_first>wrap))throw std::runtime_error(error);
}
} // namespace zima::sketcher

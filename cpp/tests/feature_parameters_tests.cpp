#include <zima/document/feature_serialization.hpp>
#include <zima/document/feature_rotation_limit.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
#include <limits>

using namespace zima::document;
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class Action> void rejected(Action action) {
    try { action(); } catch(const std::exception&) { return; }
    throw std::runtime_error("Invalid Feature definition was accepted");
}
FeatureParameters fixture() {
    FeatureParameters p;
    p.sketch_id="sketch:source";p.axis_segment_id="axis:source";
    p.profile_source=ProfileSource::External;p.profile_plane_offset=-0.0;
    p.thin_thickness=.75;p.thin_mode=ThinMode::OtherSide;
    p.origin_centerline=p.centroid_centerline=true;
    p.sides[0].length=23.456789;p.sides[0].angle_degrees=135;
    p.sides[1].length=7.25;p.sides[1].angle_degrees=37;
    ExtrusionParameters::EndTarget target;
    target.kind=EndTargetKind::Face;target.reference={"parent","end:from:source","sub/part"};
    target.label="User target";target.fallback_origin={-0.0,4,8};target.fallback_normal={0,0,-1};
    target.fallback_triangles={{0,0,8},{10,0,8},{0,10,8}};
    p.sides[1].targets.push_back(target);
    return p;
}
}
int main() { try {
    ExtrusionParameters::EndTarget limit;limit.kind=EndTargetKind::Plane;limit.fallback_normal={1,0,0};
    const auto angle=[&](zima::kernel::Vec3 axis){return feature_rotation_limit_angle({0,0,0},axis,{0,0,1},limit);};
    check(std::abs(angle({0,1,0})-90)<1e-9,"Forward rotation plane angle is wrong");
    check(std::abs(angle({0,-1,0})-270)<1e-9,"Reverse rotation lost its directed side");
    limit.fallback_normal={-1,0,0};
    check(std::abs(angle({0,1,0})-270)<1e-9,"Opposite plane side was merged");
    limit.fallback_origin={1,0,0};rejected([&]{angle({0,1,0});});
    limit.fallback_origin={0,0,0};limit.fallback_normal={0,1,0};rejected([&]{angle({0,1,0});});
    limit.fallback_normal={0,0,1};check(std::abs(angle({0,1,0})-360)<1e-9,"Coincident plane must finish a full turn");
    limit.kind=EndTargetKind::Face;rejected([&]{angle({0,1,0});});
    for(const auto first:{FeatureSideOperation::None,FeatureSideOperation::Extrusion,FeatureSideOperation::Revolution})
    for(const auto second:{FeatureSideOperation::None,FeatureSideOperation::Extrusion,FeatureSideOperation::Revolution})
    for(const auto result:{ProfileResultType::Solid,ProfileResultType::Thin,ProfileResultType::Surface})
    for(const auto extent:{EndCondition::Length,EndCondition::UpTo,EndCondition::ThroughAll})
    for(const bool symmetric:{false,true}) {
        auto p=fixture();p.result_type=result;p.symmetric=symmetric;
        p.sides[0].operation=first;p.sides[1].operation=second;
        p.sides[1].extrusion_extent=extent;
        const auto saved=serialize_feature_parameters(p);
        const auto restored=load_feature_parameters(nlohmann::json::parse(saved.dump()));
        check(restored==p,"Feature parameters lost stored independent-side settings");
        check(std::signbit(restored.profile_plane_offset),"Feature serialization normalized signed zero");
        check(std::signbit(restored.sides[1].targets[0].fallback_origin.x),"Reference serialization normalized signed zero");
        check(restored.effective_side(1)==restored.sides[symmetric?0:1],"Stored symmetry changed side ownership");
    }
    for(const auto extent:{FeatureRotationExtent::Angle,FeatureRotationExtent::Full,FeatureRotationExtent::UpTo}) {
        auto p=fixture();p.sides[0].rotation_extent=extent;
        check(load_feature_parameters(serialize_feature_parameters(p))==p,"Rotation extent did not round-trip");
    }
    auto p=fixture();
    for(const double value:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid=p;invalid.sides[1].length=value;
        rejected([&]{static_cast<void>(serialize_feature_parameters(invalid));});
    }
    const auto valid=serialize_feature_parameters(p);
    for(const auto* field:{"sides","sketch_id","thin_mode","origin_centerline"}) {
        auto invalid=valid;invalid.erase(field);
        rejected([&]{static_cast<void>(load_feature_parameters(invalid));});
    }
    for(const auto* field:{"operation","extrusion_extent","rotation_extent"}) {
        auto invalid=valid;invalid["sides"][1][field]="unknown";
        rejected([&]{static_cast<void>(load_feature_parameters(invalid));});
    }
    for(const int count:{0,1,3}) {
        auto invalid=valid;invalid["sides"]=nlohmann::json::array();
        for(int i=0;i<count;++i)invalid["sides"].push_back(valid["sides"][0]);
        rejected([&]{static_cast<void>(load_feature_parameters(invalid));});
    }
    auto invalid=valid;invalid["sides"][1]["targets"][0]["triangles"].push_back({1,2,3});
    rejected([&]{static_cast<void>(load_feature_parameters(invalid));});
    invalid=valid;invalid["symmetric"]=1;
    rejected([&]{static_cast<void>(load_feature_parameters(invalid));});
    std::cout<<"Feature parameter persistence contracts passed\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; } }

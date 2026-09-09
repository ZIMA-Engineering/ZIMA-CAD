#pragma once
#include <zima/document/part_document.hpp>

namespace zima::document {
// One enumeration for persisted embedded Sketches used by property display,
// direct dimension editing and the feature's normal Sketcher sub-editor.
template<class Container,class Visitor>
void visit_feature_sketches(Container& feature,Visitor&& visit){
    using K=FeatureKind;
    std::size_t stage=0;
    const auto apply=[&](auto& data){if(!data.empty())visit(data,stage);++stage;};
    switch(feature.feature_kind){
    case K::Sweep2D: for(auto& data:feature.sweep2d.sketches())apply(data);break;
    case K::HelicalSweep: for(auto& data:feature.helical.sketches)apply(data);break;
    case K::Sweep3D: for(auto& profile:feature.sweep3d.profiles)apply(profile.sketch_serialized);break;
    case K::Hole:case K::Thread:
        apply(feature.hole.sketch_serialized);apply(feature.hole.chamfer_sketch_serialized);apply(feature.hole.tip_sketch_serialized);break;
    default:break;
    }
}
} // namespace zima::document

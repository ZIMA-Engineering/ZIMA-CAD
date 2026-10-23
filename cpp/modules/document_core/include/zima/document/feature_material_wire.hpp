#pragma once
#include <zima/document/part_document.hpp>
#include <zima/kernel/feature_side_identity.hpp>

namespace zima::document {
// Stationary start/end datums of an inactive side remain valid references.
// They do not belong to the material tool displayed during inspection/editing.
inline bool feature_material_wire_edge(const HistoryContainer& feature,
        const kernel::ViewerEdge& edge) {
    if(edge.reference.owner_id!=feature.id||!edge.reference.instance_path.empty()||edge.parameter_seam)return false;
    if(feature.feature_kind!=FeatureKind::Feature||feature.feature.type!=FeatureType::Modeling)return true;
    std::string_view key=edge.reference.semantic_key;
    if(key.starts_with("start:"))key.remove_prefix(6);
    else if(key.starts_with("end:"))key.remove_prefix(4);
    else return true;
    if(key.starts_with("thin:")) {
        const auto separator=key.find(":from:");
        if(separator==std::string_view::npos)return true;
        key.remove_prefix(separator+6);
    }
    const auto parent=kernel::feature_side_parent(key);
    if(!parent||parent->feature_id!=feature.feature_id)return true;
    const auto side=parent->side==kernel::FeatureSide::End?0u:1u;
    return feature.feature.effective_side(side).operation!=FeatureSideOperation::None;
}
} // namespace zima::document

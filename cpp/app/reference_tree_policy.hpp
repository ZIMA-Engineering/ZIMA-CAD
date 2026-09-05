#pragma once
#include <zima/assembly/assembly_document.hpp>
#include <algorithm>
#include <set>
#include <tuple>

namespace zima::app {
struct TreeReferenceIndex {
    std::set<std::tuple<std::string,std::string,std::string>> keys;
    template<class Ref> void add(const Ref& ref) {
        keys.emplace(ref.instance_path,ref.owner_id,ref.semantic_key);
    }
    template<class Geometry> void add_geometry(const Geometry& mesh) {
        for (const auto& ref : mesh.triangle_references) add(ref);
        for (const auto& edge : mesh.edges) add(edge.reference);
        for (const auto& point : mesh.points) add(point.reference);
        for (const auto& axis : mesh.axes) add(axis.reference);
    }
    bool contains(const std::string& owner,const std::string& semantic,const std::string& path={}) const {
        return keys.contains({path,owner,semantic});
    }
    template<class Ref> std::string missing(const Ref& ref) const {
        if (contains(ref.owner_id,ref.semantic_key,ref.instance_path)) return {};
        return "["+ref.instance_path+"|"+ref.owner_id+"|"+ref.semantic_key+"]";
    }
};
inline std::string placement_reference_issue(const document::Placement& placement,const TreeReferenceIndex& index) {
    std::string issue=placement.reference_valid ? "" : "placement";
    for (const auto& ref : placement.references) issue+=index.missing(ref);
    return issue;
}
inline std::string construction_reference_issue(const document::ConstructionObject& object,const TreeReferenceIndex& index) {
    std::string issue=object.reference_valid ? "" : "construction";
    for (const auto& ref : object.references) issue+=index.missing(ref);
    for (const auto& point : object.curve_points) issue+=construction_reference_issue(point,index);
    return issue;
}
template<class Document> std::string sketch_reference_issue(const sketcher::Sketch& sketch,const Document& document) {
    std::string issue;
    if (!sketch.plane_reference_owner_id.empty() &&
        std::ranges::none_of(document.constructions,[&](const auto& object) {
            return (object.id==sketch.plane_reference_owner_id || object.entity_id==sketch.plane_reference_owner_id)
                && object.reference_valid;
        }))
        issue+="plane:"+sketch.plane_reference_owner_id;
    for (const auto& ref : sketch.external_references)
        if (ref.broken) issue+="external:"+ref.id+":"+ref.source_owner_id+":"+ref.source_semantic_key;
    return issue;
}
template<class Document> std::string feature_reference_issue(const document::HistoryContainer& feature,
        const Document& document,const TreeReferenceIndex& index,const kernel::ViewerMesh* input=nullptr) {
    std::string issue=placement_reference_issue(feature.placement,index);
    const auto profile=[&](const std::string& id) {
        const auto found=std::ranges::find_if(document.sketches,[&](const auto& s){return s.id==id;});
        if (found==document.sketches.end()) issue+="profile:"+id;
        else issue+=sketch_reference_issue(*found,document);
    };
    const auto target=[&](document::EndCondition condition,const auto& targets) {
        if (condition!=document::EndCondition::UpTo) return;
        if (targets.empty()) issue+="target:empty";
        for (const auto& t : targets) issue+=index.missing(t.reference);
    };
    using document::FeatureKind;
    switch(feature.feature_kind) {
    case FeatureKind::Extrusion:
        profile(feature.extrusion.sketch_id);
        target(feature.extrusion.end_condition_forward,feature.extrusion.end_targets_forward);
        if (feature.extrusion.extent_mode==document::ProfileExtentMode::TwoSides)
            target(feature.extrusion.end_condition_reverse,feature.extrusion.end_targets_reverse);
        break;
    case FeatureKind::Revolution:
        profile(feature.revolution.sketch_id);
        for (const auto& sketch : document.sketches) if (sketch.id==feature.revolution.sketch_id &&
            std::ranges::none_of(sketch.segments,[&](const auto& segment) { return segment.id==feature.revolution.axis_segment_id; })) issue+="axis:"+feature.revolution.axis_segment_id;
        break;
    case FeatureKind::Sweep3D:
        issue+=construction_reference_issue(feature.sweep3d.path,index);
        for (const auto& entry : feature.sweep3d.profiles) {
            if (std::ranges::none_of(feature.sweep3d.path.curve_points,[&](const auto& point){return point.id==entry.point_id;}))
                issue+="profile-point:"+entry.point_id;
            if (!entry.sketch_serialized.empty()) issue+=sketch_reference_issue(sketcher::Sketch::from_serialized(entry.sketch_serialized),document);
        }
        break;
    case FeatureKind::ShaftThread:
        issue+=index.missing(feature.shaft_thread.cylinder);
        issue+=index.missing(feature.shaft_thread.start);
        if (feature.shaft_thread.chamfer) issue+=index.missing(*feature.shaft_thread.chamfer);
        if (feature.shaft_thread.end_condition==document::EndCondition::UpTo) {
            if (feature.shaft_thread.end) issue+=index.missing(*feature.shaft_thread.end);
            else issue+="target:empty";
        }
        break;
    case FeatureKind::Thread:
        target(feature.thread.end_condition_forward,feature.thread.end_targets_forward);
        if (feature.thread.enabled) target(feature.thread.length_end_condition,feature.thread.length_end_targets);
        break;
    case FeatureKind::Fillet: case FeatureKind::Chamfer:
        if (input) {
            TreeReferenceIndex operational;
            for (const auto& edge : input->edges) operational.add(edge.reference);
            for (const auto& edge : feature.edge_treatment.flattened_edges()) issue+=operational.missing(edge);
        }
        break;
    case FeatureKind::Shell:
        for (const auto& face : feature.shell.removed_faces) issue+=index.missing(face);
        break;
    case FeatureKind::DrillPoint:
        for (const auto& face : feature.drill_point.bottom_faces) issue+=index.missing(face);
        break;
    default: break;
    }
    for (const auto& sketch : document.sketches)
        if (sketch.owner_container_id==feature.id) issue+=sketch_reference_issue(sketch,document);
    return issue;
}
inline TreeReferenceIndex assembly_reference_index(const assembly::AssemblyDocument& document) {
    TreeReferenceIndex index;
    index.add_geometry(document.build_scene().original_references);
    return index;
}
inline std::string occurrence_reference_issue(const assembly::PartOccurrence& component,const TreeReferenceIndex& index) {
    std::string issue;
    for (const auto& row : component.placement_references) {
        for (const auto* ref : {&row.component_reference,&row.target_reference})
            if (!index.contains(ref->owner_id,ref->semantic_key,ref->instance_path.encoded()))
                issue+="["+ref->instance_path.encoded()+"|"+ref->owner_id+"|"+ref->semantic_key+"]";
    }
    return issue;
}
}

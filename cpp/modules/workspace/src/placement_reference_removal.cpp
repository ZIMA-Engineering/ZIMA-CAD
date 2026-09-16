#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/holes_operations.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/workspace/placement_reference_removal.hpp>
#include <zima/document/placement_reference_assignment.hpp>
#include <zima/workspace/body_operations.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/hole_operations.hpp>
#include <zima/workspace/opening_operations.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include <zima/workspace/imported_feature_operations.hpp>
#include <zima/workspace/section_operations.hpp>
#include <algorithm>

namespace zima::workspace {
namespace {
using Ref = document::ConstructionReference;
using Kind = document::FeatureKind;
void reject(const char* code, const char* message) { throw PlacementEditError(code, message); }
bool remove_row(std::vector<Ref>& references, std::size_t index) {
    std::vector<Ref> position, orientation(2);
    std::array<bool, 3> empty_locks{};
    for (const auto& ref : references) {
        if (ref.orientation_only && ref.orientation_role != "direction") {
            const bool second = ref.orientation_role == "top" || ref.orientation_role == "bottom" ||
                ref.orientation_role == "left" || ref.orientation_role == "right";
            orientation[second ? 1 : 0] = ref;
        } else position.push_back(ref);
    }
    if (position.size() > 3) reject("invalid_placement", "The placement has too many position reference rows.");
    const auto result = document::remove_placement_reference({position, orientation, empty_locks}, true, index);
    if (result.error != document::PlacementReferenceError::None)
        reject("invalid_arguments", "The placement reference slot is unavailable.");
    if (!result.changed) return false;
    references.clear();
    const auto append = [&](const auto& rows) {
        for (const auto& ref : rows)
            if (!ref.owner_id.empty() || !ref.semantic_key.empty()) references.push_back(ref);
    };
    append(position); append(orientation);
    return true;
}
void writable(const document::PartDocument& part, const std::string& object) {
    if (const auto* body = part.body_owner_for_object(object)) {
        if (body->derived_copy) reject("read_only_body", "A derived Body cannot be edited directly.");
        if (body->scope.id != part.body_history.active_body_id())
            reject("inactive_body", "Activate the owning Body before editing its placement.");
    }
}
bool supported(Kind kind) {
    return kind == Kind::Sketch || kind == Kind::Bend || kind == Kind::Flat || kind == Kind::Holes || primitive_definition(kind) || kind == Kind::Extrusion || kind == Kind::Revolution ||
        kind == Kind::Hole || kind == Kind::Thread || kind == Kind::Sweep2D || kind == Kind::Sweep3D ||
        kind == Kind::HelicalSweep || kind == Kind::ImportedStep;
}
bool commit_feature(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, document::HistoryContainer value) {
    const auto kind = value.feature_kind;
    if (kind == Kind::Sketch) {
        const auto& sketches=live.open_part(id)->session.document().sketches;
        const auto sketch=std::ranges::find(sketches,value.id,&sketcher::Sketch::owner_container_id);
        if(sketch==sketches.end())reject("sketch_not_found","The requested Sketch does not exist.");
        return commit_part_sketch_properties(live,kernel,id,*sketch,std::move(value.placement));
    }
    if (kind == Kind::Bend || kind == Kind::Flat || kind == Kind::Holes) {
        const auto& sketches=live.open_part(id)->session.document().sketches;
        const auto sketch=std::ranges::find(sketches,value.id,&sketcher::Sketch::owner_container_id);
        if(sketch==sketches.end())reject("sketch_not_found","The requested Sketch does not exist.");
        if(kind==Kind::Bend)return commit_bend(live,kernel,id,std::move(value),*sketch);
        if(kind==Kind::Flat)return commit_flat(live,kernel,id,std::move(value),*sketch);
        return commit_holes(live,kernel,id,std::move(value),*sketch);
    }
    if (primitive_definition(kind)) return commit_primitive(live, kernel, id, std::move(value), PrimitiveEditMode::Replace);
    if (kind == Kind::Extrusion || kind == Kind::Revolution) {
        normalize_owned_profile_front_references(value.placement.references);
        commit_profile(live, kernel, id, std::move(value), ProfileEditMode::Replace);
        return true;
    }
    if (kind == Kind::Hole) return commit_hole(live, kernel, id, std::move(value), HoleEditMode::Replace);
    if (kind == Kind::Thread) return commit_opening(live, kernel, id, std::move(value), OpeningEditMode::Replace);
    if (kind == Kind::ImportedStep) return commit_imported_feature(live, kernel, id, std::move(value));
    commit_sweep(live, kernel, id, std::move(value), SweepEditMode::Replace);
    return true;
}
}
PlacementReferenceRemovalResult remove_placement_reference(Workspace& live,
    const kernel::OcctKernel& kernel, const std::string& id, const std::string& object, std::size_t index) {
    if (index > 4) reject("invalid_arguments", "The placement reference slot is unavailable.");
    const auto* part = live.open_part(id);
    const auto* assembly = live.open_assembly(id);
    if (!part && !assembly) reject("unsupported_document", "Placement operations require an open Part or Assembly.");
    if (part) {
        const auto& doc = part->session.document();
        if (const auto* body = doc.body_history.find(object)) {
            if (body->derived_copy) reject("read_only_body", "A derived Body cannot be edited directly.");
            const auto edit = prepare_body_edit(doc, object);
            auto value = *body;
            if (!remove_row(value.scope.placement.references, index)) return {};
            const bool changed = commit_body_edit(live, kernel, edit, std::move(value), edit.original.active_body_id() == object);
            return {changed, changed};
        }
        if (const auto* original = doc.find_container(object)) {
            writable(doc, object);
            if (!supported(original->feature_kind)) reject("placement_not_found", "The object has no supported placement in this document.");
            auto value = *original;
            if (!remove_row(value.placement.references, index)) return {};
            const bool changed = commit_feature(live, kernel, id, std::move(value));
            return {changed, changed};
        }
        // An owned path Point is committed through its Sweep, never inserted
        // into the document as a second standalone construction.
        for (const auto& feature : doc.history) {
            if (feature.feature_kind != Kind::Sweep3D) continue;
            const auto found = std::ranges::find(feature.sweep3d.path.curve_points, object, &document::ConstructionObject::id);
            if (found == feature.sweep3d.path.curve_points.end()) continue;
            writable(doc, feature.id);
            auto value = feature;
            auto& point = *std::ranges::find(value.sweep3d.path.curve_points, object, &document::ConstructionObject::id);
            if (!remove_row(point.references, index)) return {};
            commit_sweep(live, kernel, id, std::move(value), SweepEditMode::Replace);
            return {true, true};
        }
    } else if (const auto* container = assembly->session.document().find_sketch_container(object)) {
        auto placement = container->placement;
        if (!remove_row(placement.references, index)) return {};
        const auto& sketches = assembly->session.document().sketches;
        const auto sketch = std::ranges::find(sketches, object, &sketcher::Sketch::owner_container_id);
        if (sketch == sketches.end()) reject("sketch_not_found", "The requested Sketch does not exist.");
        return {commit_sketch_properties(live, kernel, id, *sketch, std::move(placement)), false};
    } else if (const auto* cut = assembly->session.document().find_cut(object)) {
        if (cut->definition.feature_kind != Kind::Extrusion && cut->definition.feature_kind != Kind::Revolution)
            reject("placement_not_found", "The object has no supported placement in this document.");
        auto value = cut->definition;
        if (!remove_row(value.placement.references, index)) return {};
        normalize_owned_profile_front_references(value.placement.references);
        commit_assembly_profile(live, kernel, id, std::move(value), cut->target_occurrence_ids, ProfileEditMode::Replace);
        return {true, true};
    }
    const auto* construction = part ? part->session.document().find_construction(object)
        : assembly->session.document().find_construction(object);
    if (construction) {
        if (part) writable(part->session.document(), object);
        auto value = *construction;
        if (!remove_row(value.references, index)) return {};
        return {commit_construction(live, id, std::move(value), ConstructionEditMode::Replace), false};
    }
    const auto& sections = part ? part->session.document().sections : assembly->session.document().sections;
    if (std::ranges::any_of(sections, [&](const auto& section) { return section.id == object; })) {
        const auto edit = prepare_section_edit(live, id, object);
        auto value = edit.initial;
        if (!remove_row(value.placement.references, index)) return {};
        return {commit_section(live, edit, std::move(value)), false};
    }
    reject("placement_not_found", "The object has no supported placement in this document.");
    return {};
}
}

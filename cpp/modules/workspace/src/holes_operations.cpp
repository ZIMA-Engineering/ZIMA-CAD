#include <zima/workspace/holes_operations.hpp>
#include <zima/workspace/origin_display_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/holes.hpp>
#include <zima/document/sketch_placement.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>

namespace zima::workspace {
document::HistoryContainer holes_from_sketch(const document::PartDocument& part,
    const std::string& sketch_id) {
    const auto sketch = std::ranges::find(part.sketches, sketch_id, &sketcher::Sketch::id);
    if (sketch == part.sketches.end()) throw std::invalid_argument("Zdrojová skica otvorů neexistuje.");
    const auto* owner = part.find_container(sketch->owner_container_id);
    if (!owner || owner->feature_kind != document::FeatureKind::Sketch)
        throw std::invalid_argument("Vyberte samostatný kontejner Sketch.");
    auto result = *owner;
    result.feature_kind = document::FeatureKind::Holes;
    result.feature_id = document::PartDocument::create_sketch_container().feature_id;
    result.name = "Otvory";
    result.combine_mode = document::CombineMode::Subtract;
    result.holes.sketch_id = sketch_id;
    return result;
}

bool commit_holes(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, document::HistoryContainer feature, sketcher::Sketch sketch,
    std::optional<kernel::DimensionLayout> diameter_layout) {
    auto* state = live.open_part(id);
    if (!state) throw std::invalid_argument("Otvory jsou dostupné pouze v Partu.");
    const auto& before = state->session.document();
    const kernel::EdgeReference diameter_reference{feature.id,"parameter:diameter",{}};
    const auto* previous_layout = kernel::find_dimension_layout(before.dimension_layouts, diameter_reference);
    if (diameter_layout) kernel::validate_dimension_layout(*diameter_layout);
    const bool layout_changed = diameter_layout && (!previous_layout || *previous_layout != *diameter_layout);
    const auto* existing = before.find_container(feature.id);
    const auto* body = existing ? before.body_owner_for_object(feature.id)
        : before.body_history.find(before.body_history.active_body_id());
    if (body && body->derived_copy) throw std::invalid_argument("Odvozené těleso nelze přímo upravovat.");
    if (body && body->scope.id != before.body_history.active_body_id())
        throw std::invalid_argument("Nejprve aktivujte těleso vlastnící Otvory.");
    const auto old_sketch = std::ranges::find(before.sketches, sketch.id, &sketcher::Sketch::id);
    if (existing) {
        const bool converting = existing->feature_kind == document::FeatureKind::Sketch;
        if ((!converting && existing->feature_kind != document::FeatureKind::Holes) ||
            existing->container_origin != feature.container_origin ||
            existing->feature_parent_id != feature.feature_parent_id ||
            (!converting && (existing->feature_id != feature.feature_id || existing->holes.sketch_id != feature.holes.sketch_id)) ||
            old_sketch == before.sketches.end() || old_sketch->owner_container_id != feature.id)
            throw std::invalid_argument("Úprava musí zachovat identitu kontejneru a skici.");
        if (*existing == feature && old_sketch->serialized() == sketch.serialized() && !layout_changed) return false;
        if(!layout_changed && old_sketch->serialized()==sketch.serialized())
            if(const auto changed=commit_origin_display_only(live,id,feature))return *changed;
    } else if (feature.id.empty() || feature.feature_id.empty() || old_sketch != before.sketches.end()) {
        throw std::invalid_argument("Nové Otvory musí mít vlastní kontejner a skicu.");
    }
    document::validate_native_metadata_text(feature.name);
    if (feature.name.empty()) throw std::invalid_argument("Zadejte název otvorů.");
    static_cast<void>(document::holes_request(feature, sketch));
    document::normalize_container_front_references(feature.placement.references);
    const auto container = feature.id;
    auto next = before;
    if (diameter_layout) kernel::store_dimension_layout(next.dimension_layouts, diameter_reference, *diameter_layout);
    if (existing) {
        *next.find_container(container) = std::move(feature);
        *std::ranges::find(next.sketches, sketch.id, &sketcher::Sketch::id) = std::move(sketch);
    } else {
        next.insert_history_entry(document::PartHistoryKind::Feature, container);
        next.history.push_back(std::move(feature));
        next.sketches.push_back(std::move(sketch));
    }
    PartCalculationPolicy policy; policy.reject_errors = true;
    if (existing) {policy.edited_document_id = id; policy.edited_history_limit = before.history_index(container);}
    auto references = construction_reference_source_geometry(state->session.calculated_boundaries());
    append_reference_geometry(references, next.origin_viewer_mesh().original_references);
    append_reference_geometry(references, next.construction_viewer_mesh().original_references);
    next.resolve_constructions(std::move(references));
    auto calculated = calculate_part_with_resolved_references(kernel, next, &state->session.calculated_boundaries(), policy);
    if (!next.find_container(container)->placement.reference_valid)
        throw std::invalid_argument("Umístění otvorů obsahuje neplatnou referenci.");
    commit_part_document(live, id, std::move(next), std::move(calculated));
    return true;
}
}

#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


std::vector<zima::kernel::BodyResult> AssemblyWorkspaceWindow::calculate_part(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous) const {
    const auto operations = document.kernel_operations(false, true);
    auto calculated = kernel_.evaluate_history_recovering(operations,
        previous == nullptr ? std::vector<zima::kernel::BodyResult>{} : *previous);
    if (properties_dialog_ && !calculated.empty()) {
        const auto& errors = calculated.back().calculation_errors;
        if (part_rollback_ && part_rollback_->part_document_id == document.document_id &&
            part_rollback_->history_limit < document.history.size()) {
            // Editing validates this operation and its real input. Later errors
            // remain attached to later containers, outside this transaction.
            const auto& owner = document.history[part_rollback_->history_limit].id;
            if (const auto issue = errors.find(owner); issue != errors.end())
                throw std::runtime_error(issue->second);
        } else if (!errors.empty()) {
            throw std::runtime_error(errors.begin()->second);
        }
    }
    return calculated;
}

std::vector<zima::kernel::BodyResult>
AssemblyWorkspaceWindow::calculate_part_with_resolved_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous) const {
    std::vector<zima::kernel::BodyResult> calculated;
    const auto* incremental_source = previous;
    // A downstream container may reference geometry produced by another
    // downstream container.  Each pass advances that dependency chain by one
    // history boundary; the final unchanged pass proves that the calculated
    // body and all persisted placements describe the same state.
    const auto pass_limit = document.history.size() + 2;
    for (std::size_t pass = 0; pass < pass_limit; ++pass) {
        calculated = calculate_part(document, incremental_source);
        const auto history_before = document.history;
        const auto constructions_before = document.constructions;
        const auto bodies_before = document.body_history.bodies();
        document.resolve_constructions(
            construction_reference_source_geometry(calculated));
        const bool external_references_changed =
            refresh_sketch_external_references(document, calculated);
        const bool drill_points_changed =
            prune_missing_drill_point_references(document, calculated);
        if (!external_references_changed && !drill_points_changed &&
            document.history == history_before &&
            document.constructions == constructions_before &&
            document.body_history.bodies() == bodies_before) {
            if(!document.sections.empty()){
                auto geometry=construction_reference_source_geometry(calculated);
                append_reference_geometry(geometry,document.origin_viewer_mesh().original_references);
                append_reference_geometry(geometry,document.construction_viewer_mesh().original_references);
                append_reference_geometry(geometry,document.history_origin_reference_geometry_before({}));
                zima::document::resolve_section_placements(document.sections,geometry);
            }
            return calculated;
        }
        incremental_source = &calculated;
    }
    throw std::runtime_error(
        "Part placement references did not converge during regeneration");
}

void AssemblyWorkspaceWindow::calculate_assembly_cuts(
    zima::assembly::AssemblyDocument& document) const {
    // Assembly-owned cutters use the exact same persisted placement solver
    // as Part history containers.  The input universe is the Assembly's real
    // pre-cut component geometry plus its own Origin/constructions; no OCCT
    // topology is traversed to define or recover a reference here.
    auto reference_geometry = document.build_scene().original_references;
    append_reference_geometry(reference_geometry,
        document.origin_viewer_mesh().original_references);
    append_reference_geometry(reference_geometry,
        document.construction_viewer_mesh().original_references);
    for (auto& cut : document.cuts) {
        cut.input_component_bodies.clear();
        for (const auto& component : document.components) {
            cut.input_component_bodies.emplace(
                component.occurrence_id, component.calculated_source);
        }
        if (cut.definition.suppressed || cut.target_occurrence_ids.empty()) continue;
        zima::document::PartDocument cutter_document;
        cutter_document.document_precision = document.document_precision;
        cutter_document.user_parameters = document.user_parameters;
        cutter_document.relations = document.relations;
        cutter_document.sketches = document.sketches;
        cutter_document.constructions = document.constructions;
        cutter_document.history = {cut.definition};
        cutter_document.resolve_constructions(reference_geometry);
        cut.definition = cutter_document.history.front();
        const auto owned_sketch = std::find_if(cutter_document.sketches.begin(),
            cutter_document.sketches.end(), [&](const auto& sketch) {
                return sketch.owner_container_id == cut.definition.id;
            });
        if (owned_sketch != cutter_document.sketches.end()) {
            const auto target = std::find_if(document.sketches.begin(),
                document.sketches.end(), [&](const auto& sketch) {
                    return sketch.id == owned_sketch->id;
                });
            if (target != document.sketches.end()) *target = *owned_sketch;
        }
        auto cutter_operations = cutter_document.kernel_operations(true);
        if (cutter_operations.size() != 1) {
            throw std::runtime_error(
                "Assembly cut did not produce one cutter operation");
        }
        cutter_operations.front().operation =
            zima::kernel::BooleanOperation::Add;
        const auto cutter_boundaries = kernel_.evaluate_history(cutter_operations);
        if (cutter_boundaries.empty()) {
            throw std::runtime_error("Assembly cut body is empty");
        }
        for (const auto& target_id : cut.target_occurrence_ids) {
            auto* target = document.find_occurrence(target_id);
            if (target == nullptr) {
                throw std::runtime_error(
                    "Assembly cut target must be an immediate component occurrence");
            }
            if (target->suppressed) continue;
            target->calculated_source = kernel_.subtract_bodies(
                zima::assembly::calculate_component_body(*target,kernel_), cutter_boundaries.back(),
                {target->placement.x, target->placement.y, target->placement.z},
                {target->placement.rotation_x, target->placement.rotation_y,
                 target->placement.rotation_z}, cutter_operations.front().boolean_tolerance,
                cutter_operations.front().mesh_deflection);
        }
    }
    document.calculate_derived_copies(kernel_);
    if(!document.sections.empty()){
        auto geometry=document.build_scene().original_references;
        append_reference_geometry(geometry,document.origin_viewer_mesh().original_references);
        append_reference_geometry(geometry,document.construction_viewer_mesh().original_references);
        zima::document::resolve_section_placements(document.sections,geometry);
    }
}

} // namespace zima::app

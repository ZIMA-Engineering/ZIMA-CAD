#include <zima/workspace/model_calculation.hpp>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace zima::workspace {

std::vector<zima::kernel::BodyResult> calculate_part(
    const zima::kernel::OcctKernel& kernel,
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous,
    const PartCalculationPolicy& policy) {
    const auto operations = document.kernel_operations(false, true);
    auto calculated = kernel.evaluate_history_recovering(operations,
        previous == nullptr ? std::vector<zima::kernel::BodyResult>{} : *previous);
    if (policy.reject_errors && !calculated.empty()) {
        const auto& errors = calculated.back().calculation_errors;
        if (policy.edited_history_limit && policy.edited_document_id == document.document_id &&
            *policy.edited_history_limit < document.history.size()) {
            // Editing validates this operation and its real input. Later errors
            // remain attached to later containers, outside this transaction.
            const auto& owner = document.history[*policy.edited_history_limit].id;
            if (const auto issue = errors.find(owner); issue != errors.end())
                throw std::runtime_error(issue->second);
        } else if (!errors.empty()) {
            throw std::runtime_error(errors.begin()->second);
        }
    }
    return calculated;
}

std::vector<zima::kernel::BodyResult>
calculate_part_with_resolved_references(
    const zima::kernel::OcctKernel& kernel,
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous,
    const PartCalculationPolicy& policy) {
    std::vector<zima::kernel::BodyResult> calculated;
    const auto* incremental_source = previous;
    // A downstream container may reference geometry produced by another
    // downstream container.  Each pass advances that dependency chain by one
    // history boundary; the final unchanged pass proves that the calculated
    // body and all persisted placements describe the same state.
    const auto pass_limit = document.history.size() + 2;
    for (std::size_t pass = 0; pass < pass_limit; ++pass) {
        calculated = calculate_part(kernel, document, incremental_source, policy);
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

void calculate_resolved_assembly_cuts(
    const zima::kernel::OcctKernel& kernel,
    zima::assembly::AssemblyDocument& document) {
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
        const auto cutter_boundaries = kernel.evaluate_history(cutter_operations);
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
            target->calculated_source = kernel.subtract_bodies(
                zima::assembly::calculate_component_body(*target,kernel), cutter_boundaries.back(),
                {target->placement.x, target->placement.y, target->placement.z},
                {target->placement.rotation_x, target->placement.rotation_y,
                 target->placement.rotation_z}, cutter_operations.front().boolean_tolerance,
                cutter_operations.front().mesh_deflection);
        }
    }
    document.calculate_derived_copies(kernel);
    if(!document.sections.empty()){
        auto geometry=document.build_scene().original_references;
        append_reference_geometry(geometry,document.origin_viewer_mesh().original_references);
        append_reference_geometry(geometry,document.construction_viewer_mesh().original_references);
        zima::document::resolve_section_placements(document.sections,geometry);
    }
}

PartRegenerationResult regenerate_part(Workspace& workspace,
    const kernel::OcctKernel& kernel, const std::string& document_id,
    const PartCalculationPolicy& policy) {
    auto* part = workspace.open_part(document_id);
    if (!part) throw std::invalid_argument("Regeneration requires an open Part document");
    const auto& previous = part->session.document();
    auto next = previous;
    // Explicit Regenerate recalculates geometry even when parameters match
    // the persisted cache (for example after a kernel calculation fix).
    auto calculated = calculate_part_with_resolved_references(kernel, next, nullptr, policy);
    next.resolve_constructions(calculated.empty()
        ? zima::kernel::ViewerReferenceGeometry{}
        : calculated.back().mesh.original_references);
    const bool references_changed =
        refresh_sketch_external_references(next, calculated) |
        workspace.refresh_context_external_references(next) |
        prune_missing_drill_point_references(next, calculated);
    if (references_changed) calculated = calculate_part(kernel, next, &calculated, policy);
    const bool sketches_changed = next.sketches.size() != previous.sketches.size() ||
        !std::equal(next.sketches.begin(), next.sketches.end(), previous.sketches.begin(),
            [](const auto& left, const auto& right) { return left.serialized() == right.serialized(); });
    if (references_changed || sketches_changed ||
        zima::document::serialize_sections(next.sections)!=zima::document::serialize_sections(previous.sections) || next.history != previous.history ||
        next.constructions != previous.constructions ||
        next.body_history.bodies() != previous.body_history.bodies()) {
        part->session.commit(std::move(next), std::move(calculated));
    } else {
        part->session.update_calculated_boundaries(std::move(calculated));
    }
    return {references_changed};
}

void regenerate_assembly(Workspace& workspace, const kernel::OcctKernel& kernel,
    const std::string& id, const PartCalculationPolicy& policy) {
    std::set<std::string> regenerated_parts;
    std::set<std::string> visiting_parts;
    const std::function<void(const std::string&)> regenerate_part_dependencies =
        [&](const std::string& part_id) {
            if (regenerated_parts.contains(part_id)) return;
            if (!visiting_parts.insert(part_id).second) {
                throw std::runtime_error(
                    "External Sketch document dependency cycle detected");
            }
            auto* part = workspace.open_part(part_id);
            if (part == nullptr) {
                visiting_parts.erase(part_id);
                return;
            }
            for (const auto& sketch : part->session.document().sketches) {
                for (const auto& reference : sketch.external_references) {
                    if (reference.context_assembly_document_id == id &&
                        reference.source_document_id != part_id) {
                        regenerate_part_dependencies(
                            reference.source_document_id);
                    }
                }
            }
            auto next = part->session.document();
            const bool has_context = std::any_of(
                next.sketches.begin(), next.sketches.end(), [&](const auto& sketch) {
                    return std::any_of(sketch.external_references.begin(),
                        sketch.external_references.end(), [&](const auto& reference) {
                            return reference.context_assembly_document_id == id;
                        });
                });
            if (has_context) {
                const auto& previous = part->session.calculated_boundaries();
                auto calculated = calculate_part(kernel, next, &previous, policy);
                const auto previous_constructions = next.constructions;
                next.resolve_constructions(calculated.empty()
                    ? zima::kernel::ViewerReferenceGeometry{}
                    : calculated.back().mesh.original_references);
                const bool references_changed =
                    refresh_sketch_external_references(next, calculated) |
                    workspace.refresh_context_external_references(next);
                if (references_changed) {
                    calculated = calculate_part(kernel, next, &calculated, policy);
                }
                if (references_changed ||
                    next.constructions != previous_constructions) {
                    part->session.commit(std::move(next), std::move(calculated));
                } else {
                    part->session.update_calculated_boundaries(
                        std::move(calculated));
                }
            }
            visiting_parts.erase(part_id);
            regenerated_parts.insert(part_id);
        };
    for (const auto& state : workspace.documents()) {
        const auto* part = std::get_if<zima::workspace::PartState>(&state);
        if (part != nullptr) {
            regenerate_part_dependencies(
                part->session.document().document_id);
        }
    }
    workspace.regenerate_assembly_from_open_dependencies(id);
    if (auto* regenerated = workspace.open_assembly(id)) {
        auto next = regenerated->session.document();
        const bool references_changed =
            refresh_assembly_sketch_external_references(next);
        calculate_resolved_assembly_cuts(kernel, next);
        if (references_changed || !next.cuts.empty()) {
            regenerated->session.commit(std::move(next));
        }
    }
}

} // namespace zima::workspace

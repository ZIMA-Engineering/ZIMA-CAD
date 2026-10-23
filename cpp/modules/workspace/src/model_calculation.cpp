#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/workspace/history_deletion.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/opening_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/body_link_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/placement_reference_geometry.hpp>
#include <zima/document/solid_state_reference_views.hpp>
#include <zima/document/solid_state_calculation.hpp>
#include <zima/document/profile_targets.hpp>
#include "group_solid_state_calculation.hpp"
#include "ordered_up_to_assembly_sources.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>

namespace zima::workspace {

namespace { thread_local CalculationExecutionScope::Runner calculation_runner; }
CalculationExecutionScope::CalculationExecutionScope(Runner runner)
    : previous_(std::move(calculation_runner)) { calculation_runner = std::move(runner); }
CalculationExecutionScope::~CalculationExecutionScope() { calculation_runner = std::move(previous_); }

PartCalculationPolicy feature_definition_calculation_policy(
    const document::PartDocument& before, const std::vector<kernel::BodyResult>& previous,
    const std::string& container_id) {
    PartCalculationPolicy policy;
    if (const auto* existing = before.find_container(container_id)) {
        policy.edited_document_id = before.document_id;
        policy.edited_history_limit = before.history_index(container_id);
        policy.reject_errors = existing->feature_kind != document::FeatureKind::Sketch &&
            (previous.empty() || !previous.back().calculation_errors.contains(container_id));
    }
    return policy;
}

static void validate_part_calculation(const document::PartDocument& document,
    const std::vector<kernel::BodyResult>& calculated,const PartCalculationPolicy& policy) {
    if (policy.reject_errors && !calculated.empty()) {
        const auto& errors = calculated.back().calculation_errors;
        if (policy.edited_history_limit && policy.edited_document_id == document.document_id &&
            *policy.edited_history_limit < document.history.size()) {
            // Editing validates this operation and its real input. Later errors
            // remain attached to later containers, outside this transaction.
            const auto& owner = document.history[*policy.edited_history_limit].id;
            if (const auto issue = errors.find(owner); issue != errors.end()) {
                if(const auto ordered=calculated.back().ordered_up_to_failures.find(owner);
                        ordered!=calculated.back().ordered_up_to_failures.end()) {
                    const auto& d=ordered->second;throw kernel::OrderedUpToError(d.failure,d.row,d.other_row);
                }
                throw std::runtime_error(issue->second);
            }
        } else if (!errors.empty()) {
            throw std::runtime_error(errors.begin()->second);
        }
    }
}

std::vector<zima::kernel::BodyResult> calculate_part(
    const zima::kernel::OcctKernel& kernel,
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous,
    const PartCalculationPolicy& policy) {
    if (calculation_runner) {
        std::vector<kernel::BodyResult> result;
        const auto runner = calculation_runner;
        runner([&] {
            CalculationExecutionScope synchronous({});
            result = calculate_part(kernel,document,previous,policy);
        });
        return result;
    }
    if(!document.removed_reference_states.empty()) {
        auto repaired=document;
        if(refresh_removed_reference_states(repaired))return calculate_part(kernel,repaired,previous,policy);
    }
    const auto operations = group_solid_state_operations(kernel,document,previous);
    // Explicit calculation must rebuild combined solid states from their real
    // source geometry, rather than accept a persisted final-state snapshot.
    // Retain the valid source prefix; opening and unchanged OK never get here.
    std::vector<kernel::BodyResult> source_prefix;
    auto calculation_input=previous;
    if(previous&&std::ranges::any_of(operations,[](const auto& op) {
        return std::holds_alternative<kernel::FeatureGroupRequest>(op.primitive)&&kernel::solid_state_candidate(op);
    })) {
        const auto state=std::ranges::find_if(operations,[](const auto& op){return std::holds_alternative<kernel::SolidStateRequest>(op.primitive);});
        if(state!=operations.end()) {
            const auto count=std::min(previous->size(),static_cast<std::size_t>(state-operations.begin()));
            source_prefix.assign(previous->begin(),previous->begin()+count);calculation_input=&source_prefix;
        }
    }
    const std::vector<kernel::BodyResult> empty_previous;
    // Saved analysis frames are real placement inputs and must be refreshed
    // before reference settling. Otherwise aggregate analysis is demand driven.
    const bool eager_analysis=std::getenv("ZIMA_CPP_EAGER_ANALYSIS")!=nullptr;
    kernel::FaceMeasurementScope face_measurements(std::getenv("ZIMA_CPP_EAGER_FACE_MEASUREMENTS")!=nullptr);
    kernel::AggregatePropertyScope properties(document.body_properties.empty()&&!eager_analysis
        ?policy.refresh_physical_properties?kernel::AggregatePropertyDemand::Final:kernel::AggregatePropertyDemand::Deferred
        :kernel::AggregatePropertyDemand::All);
    auto calculated = kernel.evaluate_history_recovering(operations,
        calculation_input == nullptr ? empty_previous : *calculation_input);
    validate_part_calculation(document,calculated,policy);
    return calculated;
}

static kernel::ViewerReferenceGeometry construction_resolution_geometry(
    const document::PartDocument& document,const std::vector<kernel::BodyResult>& calculated) {
    // Independent imports consume only numeric local placement. Body placement
    // still needs every referenced face/edge/point/axis, selected from the exact
    // current source by the same approved lookup used in the dialog. All other
    // histories retain the complete established reference preparation path.
    const bool independent_imports=!document.body_history.bodies().empty()&&
        document.body_history.booleans().empty()&&document.constructions.empty()&&
        document.sketches.empty()&&document.sections.empty()&&!document.history.empty()&&
        std::ranges::all_of(document.history,[](const auto& feature) {
            return feature.feature_kind==document::FeatureKind::ImportedStep&&feature.placement.references.empty();
        })&&std::ranges::all_of(document.body_history.bodies(),[](const auto& body) {
            return !body.derived_copy&&!body.scale&&!body.link;
        })&&!calculated.empty()&&calculated.back().solid_state_reference_views.empty();
    if(!independent_imports)return construction_reference_source_geometry(calculated);
    std::vector<document::ConstructionReference> references;
    for(const auto& body:document.body_history.bodies())
        references.insert(references.end(),body.scope.placement.references.begin(),body.scope.placement.references.end());
    return document::PlacementReferenceGeometryIndex(calculated.back().mesh.original_references,references).select(references);
}

static std::vector<zima::kernel::BodyResult>
calculate_part_reference_state(
    const zima::kernel::OcctKernel& kernel,
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous,
    const PartCalculationPolicy& policy) {
    static_cast<void>(refresh_removed_reference_states(document));
    document.synchronize_derived_copy_sources();
    std::vector<zima::kernel::BodyResult> calculated;
    const auto* incremental_source = previous;
    // A downstream container may reference geometry produced by another
    // downstream container.  Each pass advances that dependency chain by one
    // history boundary; the final unchanged pass proves that the calculated
    // body and all persisted placements describe the same state.
    const auto pass_limit = 2 * document.history.size() + 3;
    const auto attached_flat_profiles=[&] {
        std::map<std::string,std::string> profiles;
        for(const auto& feature:document.history)if(feature.feature_kind==document::FeatureKind::Flat&&feature.flat.sheet_attachment)
            for(const auto& sketch:document.sketches)if(sketch.id==feature.flat.sketch_id)profiles.emplace(sketch.id,sketch.serialized());
        return profiles;
    };
    // Resolving an owned profile can move its Sketch frame without changing
    // the owning history container. Those frames are geometry inputs too.
    // Compare their exact bits without normalizing or rewriting any side data;
    // avoid serializing whole Sketches just to track a rigid frame.
    const auto sketch_frames=[&] {
        std::vector<std::array<std::uint64_t,12>> frames;
        frames.reserve(document.sketches.size());
        for(const auto& sketch:document.sketches) {
            std::array<std::uint64_t,12> frame;std::size_t index=0;
            for(const auto& vector:{sketch.resolved_origin,sketch.resolved_x_axis,
                    sketch.resolved_y_axis,sketch.resolved_normal})
                for(double value:{vector.x,vector.y,vector.z})frame[index++]=std::bit_cast<std::uint64_t>(value);
            frames.push_back(frame);
        }
        return frames;
    };
    for (std::size_t pass = 0; pass < pass_limit; ++pass) {
        auto phase_start=std::chrono::steady_clock::now();
        const auto phase=[&](const char* name) {
            const auto now=std::chrono::steady_clock::now();
            if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,"Part settling pass=%zu %s: %.3f ms\n",pass,name,
                std::chrono::duration<double,std::milli>(now-phase_start).count());
            phase_start=now;
        };
        PartCalculationPolicy calculation_policy;
        calculation_policy.refresh_physical_properties=policy.refresh_physical_properties;
        calculated = calculate_part(kernel, document, incremental_source, calculation_policy);
        phase("calculation");
        for(const auto& boundary:calculated)if(!boundary.resolved_up_to_limits.empty())for(auto& feature:document.history)
            if(feature.feature_kind==document::FeatureKind::Feature&&!boundary.calculation_errors.contains(feature.id))
                static_cast<void>(document::resolve_feature_up_to_choices(feature.feature,feature.id,feature.feature_id,boundary.resolved_up_to_limits));
        const auto history_before = document.history;
        const auto constructions_before = document.constructions;
        const auto bodies_before = document.body_history.bodies();
        const auto flat_profiles_before=attached_flat_profiles();
        const auto sketch_frames_before=sketch_frames();
        // Saved centroids use this pass's actual input boundary, before any
        // downstream placement consumes the refreshed frame. No kernel call.
        // Persist the native retained boundary only after successful explicit
        // calculation. It is validation intent, so the settling pass reuses
        // unchanged geometry rather than invoking Splitter a second time.
        if(!calculated.empty())for(auto& feature:document.history)
            if(feature.feature_kind==document::FeatureKind::SurfaceTrim&&!feature.suppressed&&
                feature.surface_trim.retained_region_key.empty()&&!calculated.back().calculation_errors.contains(feature.id)) {
                std::set<std::string> keys;
                for(const auto& face:calculated.back().mesh.original_references.triangle_references)
                    if(face.owner_id==feature.id&&face.semantic_key.starts_with("trim:face:from:"))keys.insert(face.semantic_key);
                if(keys.size()==1)feature.surface_trim.retained_region_key=*keys.begin();
            }
        document::refresh_body_properties(document,calculated);
        auto reference_geometry=construction_resolution_geometry(document,calculated);
        const auto state_views=calculated.empty()?document::HistoryReferenceViews{}:
            document::solid_state_reference_views(document,calculated.back(),reference_geometry);
        phase("reference preparation");
        document.resolve_constructions(std::move(reference_geometry),state_views);
        phase("reference resolution");
        // A changed placement describes the next geometry pass. Projecting
        // this pass's old source mesh into that new Sketch frame can create
        // a false constraint conflict while the dependency chain is settling.
        if(document.history!=history_before||document.constructions!=constructions_before||
            document.body_history.bodies()!=bodies_before||attached_flat_profiles()!=flat_profiles_before||
            sketch_frames()!=sketch_frames_before) {
            incremental_source=&calculated;continue;
        }
        const bool external_references_changed =
            refresh_sketch_external_references(document, calculated,&kernel);
        const bool drill_points_changed =
            prune_missing_drill_point_references(document, calculated);
        const bool profile_targets_changed=refresh_profile_end_targets(document,calculated);
        const bool opening_targets_changed=refresh_opening_end_targets(document,calculated);
        if (!external_references_changed && !drill_points_changed && !profile_targets_changed && !opening_targets_changed &&
            attached_flat_profiles()==flat_profiles_before && sketch_frames()==sketch_frames_before &&
            document.history == history_before &&
            document.constructions == constructions_before &&
            document.body_history.bodies() == bodies_before &&
            (calculated.empty() || calculated.back().source_fingerprint ==
                kernel::history_fingerprint(group_solid_state_cached_operations(document,&calculated),calculated.size()))) {
            if(!document.sections.empty()){
                auto geometry=construction_reference_source_geometry(calculated);
                append_reference_geometry(geometry,document.origin_viewer_mesh().original_references);
                append_reference_geometry(geometry,document.construction_viewer_mesh().original_references);
                append_reference_geometry(geometry,document.history_origin_reference_geometry_before({}));
                zima::document::resolve_section_placements(document.sections,geometry);
            }
            validate_part_calculation(document,calculated,policy);
            return calculated;
        }
        incremental_source = &calculated;
    }
    throw std::runtime_error(
        "Part placement references did not converge during regeneration");
}

std::vector<kernel::BodyResult> calculate_part_with_resolved_references(
    const kernel::OcctKernel& kernel,document::PartDocument& document,
    const std::vector<kernel::BodyResult>* previous,const PartCalculationPolicy& policy) {
    if (calculation_runner) {
        std::vector<kernel::BodyResult> result;
        const auto runner = calculation_runner;
        runner([&] {
            CalculationExecutionScope synchronous({});
            result = calculate_part_reference_state(kernel,document,previous,policy);
        });
        return result;
    }
    return calculate_part_reference_state(kernel,document,previous,policy);
}

void calculate_resolved_assembly_cuts(
    const zima::kernel::OcctKernel& kernel,
    zima::assembly::AssemblyDocument& document,bool refresh_physical_properties) {
    if (calculation_runner) {
        const auto runner = calculation_runner;
        runner([&] {
            CalculationExecutionScope synchronous({});
            calculate_resolved_assembly_cuts(kernel,document,refresh_physical_properties);
        });
        return;
    }
    kernel::AggregatePropertyScope properties(refresh_physical_properties?
        kernel::AggregatePropertyDemand::All:kernel::AggregatePropertyDemand::Deferred);
    kernel::FaceMeasurementScope face_measurements(std::getenv("ZIMA_CPP_EAGER_FACE_MEASUREMENTS")!=nullptr);
    // Assembly-owned cutters use the exact same persisted placement solver
    // as Part history containers.  The input universe is the Assembly's real
    // pre-cut component geometry plus its own Origin/constructions; no OCCT
    // topology is traversed to define or recover a reference here.
    auto reference_geometry = document.build_drawing_scene().original_references;
    append_reference_geometry(reference_geometry,
        document.origin_viewer_mesh().original_references);
    append_reference_geometry(reference_geometry,
        document.construction_viewer_mesh().original_references);
    const auto order=document.complete_history_order();
    const auto order_index=[&](const std::string& id) {
        const auto found=std::ranges::find(order,id,&assembly::AssemblyDocument::HistoryEntry::id);
        return static_cast<std::size_t>(found-order.begin());
    };
    const auto refresh_intersection=[&](auto& sketch) {
        if(std::ranges::none_of(sketch.external_references,[](const auto& r){return r.intersection.has_value();}))return;
        const auto values=calculate_assembly_sketch_intersections(kernel,document,sketch);
        if(!values.empty())static_cast<void>(sketch.refresh_external_references({}, {},false,nullptr,false,&values));
    };
    std::set<std::string> refreshed_sketches;
    const auto refresh_standalone_before=[&](std::size_t boundary) {
        for(auto& sketch:document.sketches)if(document.find_sketch_container(sketch.owner_container_id)&&
            order_index(sketch.owner_container_id)<boundary&&refreshed_sketches.insert(sketch.id).second)refresh_intersection(sketch);
    };
    for (auto& cut : document.cuts) {
        cut.input_component_bodies.clear();
        // Datums also need their input boundary when Properties rolls back
        // later Assembly operations. Snapshots share the calculated packets.
        for (const auto& component : document.components) {
            cut.input_component_bodies.emplace(
                component.occurrence_id, component.calculated_source);
        }
        refresh_standalone_before(order_index(cut.definition.id));
        if (cut.definition.suppressed) continue;
        for(auto& sketch:document.sketches)if(sketch.owner_container_id==cut.definition.id&&
            std::ranges::any_of(sketch.external_references,[](const auto& reference){return reference.body_edge;})) {
            const auto body=assembly_sketch_body_reference_geometry(document,sketch);
            std::set<std::string> sources;
            for(const auto& reference:sketch.external_references)sources.insert(reference.source_document_id);
            for(const auto& source:sources)static_cast<void>(sketch.refresh_external_references(source,reference_geometry,false,&body));
        }
        zima::document::PartDocument cutter_document;
        cutter_document.document_id=document.document_id;
        cutter_document.document_precision = document.document_precision;
        cutter_document.user_parameters = document.user_parameters;
        cutter_document.relations = document.relations;
        cutter_document.sketches = document.sketches;
        cutter_document.constructions = document.constructions;
        if(cut.definition.feature_kind==document::FeatureKind::Feature) {
            auto& definition=cut.definition.feature;
            for(std::size_t side=0;side<(definition.symmetric?1u:2u);++side) {
                const auto value=definition.effective_side(side);
                const bool up_to=(value.operation==document::FeatureSideOperation::Extrusion&&value.extrusion_extent==document::EndCondition::UpTo) ||
                    (value.operation==document::FeatureSideOperation::Revolution&&value.rotation_extent==document::FeatureRotationExtent::UpTo);
                if(!up_to || (definition.type==document::FeatureType::Axis&&!value.axis_enabled))continue;
                auto& targets=definition.sides[side].targets;
                if(targets.empty())throw std::runtime_error("Select at least one Feature end reference.");
                if(definition.type==document::FeatureType::Axis&&targets.size()!=1)
                    throw std::runtime_error("Select exactly one extrusion end reference.");
                for(auto& target:targets) {
                    const auto resolved=document::resolve_profile_target(target,reference_geometry);
                    if(!resolved)throw std::runtime_error("The original extrusion target is unavailable.");
                    target=*resolved;
                }
            }
        }
        cutter_document.history = {cut.definition};
        cutter_document.resolve_constructions(reference_geometry);
        cut.definition = cutter_document.history.front();
        const auto owned_sketch = std::find_if(cutter_document.sketches.begin(),
            cutter_document.sketches.end(), [&](const auto& sketch) {
                return sketch.owner_container_id == cut.definition.id;
            });
        if (owned_sketch != cutter_document.sketches.end()) {
            refresh_intersection(*owned_sketch);
            const auto target = std::find_if(document.sketches.begin(),
                document.sketches.end(), [&](const auto& sketch) {
                    return sketch.id == owned_sketch->id;
                });
            if (target != document.sketches.end()) *target = *owned_sketch;
        }
        if(cut.definition.feature_kind==document::FeatureKind::Feature && cut.definition.feature.sketch_only()) {
            if(!cut.definition.placement.reference_valid)
                throw std::runtime_error("The proposed feature placement references cannot be resolved.");
            static_cast<void>(cutter_document.kernel_operations(true)); // Validate the native definition, without calculating a body.
            append_reference_geometry(reference_geometry,cutter_document.feature_result_mesh(cut.definition).original_references);
            continue;
        }
        if(cut.target_occurrence_ids.empty())continue;
        auto cutter_operations = cutter_document.kernel_operations(true);
        if (cutter_operations.size() != 1) {
            throw std::runtime_error(
                "Assembly cut did not produce one cutter operation");
        }
        cutter_operations.front().operation =
            zima::kernel::BooleanOperation::Add;
        if(cut.definition.feature_kind==document::FeatureKind::Feature)
            detail::attach_ordered_up_to_sources(document,cutter_operations);
        const auto cutter_boundaries = kernel.evaluate_history(cutter_operations);
        if (cutter_boundaries.empty()) {
            throw std::runtime_error("Assembly cut body is empty");
        }
        if(cut.definition.feature_kind==document::FeatureKind::Feature)
            for(const auto& boundary:cutter_boundaries)
                static_cast<void>(document::resolve_feature_up_to_choices(cut.definition.feature,cut.definition.id,
                    cut.definition.feature_id,boundary.resolved_up_to_limits));
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
    refresh_standalone_before(order.size());
    document.calculate_derived_copies(kernel);
    if(!document.sections.empty()){
        auto geometry=document.build_drawing_scene().original_references;
        append_reference_geometry(geometry,document.origin_viewer_mesh().original_references);
        append_reference_geometry(geometry,document.construction_viewer_mesh().original_references);
        zima::document::resolve_section_placements(document.sections,geometry);
        for(auto& section:document.sections)refresh_intersection(section.sketch);
    }
}

PartRegenerationResult regenerate_part(Workspace& workspace,
    const kernel::OcctKernel& kernel, const std::string& document_id,
    const PartCalculationPolicy& policy) {
    auto* part = workspace.open_part(document_id);
    if (!part) throw std::invalid_argument("Regeneration requires an open Part document");
    const auto& previous = part->session.document();
    auto next = previous;
    auto relation_input=part->session.calculated_boundaries();
    if(!next.relations.empty()&&!relation_input.empty()) {
        const auto integrate=[&]{kernel.ensure_body_properties(relation_input.back());};
        if(calculation_runner)calculation_runner(integrate);else integrate();
    }
    apply_relation_dimensions(next,document::physical_values(previous,relation_input));
    refresh_body_links(workspace,next,part->path);
    // Native snapshots must be recalculated by this runtime at least once.
    // Thereafter explicit regeneration uses the existing dependency-aware
    // incremental calculation, retaining only unchanged live calculations.
    auto relation_policy=policy;
    relation_policy.refresh_physical_properties=true;
    if(!next.relations.empty())relation_policy.reject_errors=true;
    const auto& current=part->session.calculated_boundaries();
    auto calculated = calculate_part_with_resolved_references(kernel, next,
        kernel.has_current_calculation(current)?&current:nullptr, relation_policy);
    if(!calculated.empty()&&!calculated.back().solid_state_reference_views.empty()) {
        auto geometry=construction_reference_source_geometry(calculated);
        const auto views=document::solid_state_reference_views(next,calculated.back(),geometry);
        next.resolve_constructions(std::move(geometry),views);
    } else next.resolve_constructions(calculated.empty()
        ? zima::kernel::ViewerReferenceGeometry{}
        : calculated.back().mesh.original_references);
    const bool references_changed =
        refresh_sketch_external_references(next, calculated) |
        workspace.refresh_context_external_references(next) |
        refresh_context_sketch_intersections(workspace,kernel,next) |
        prune_missing_drill_point_references(next, calculated);
    if (references_changed) calculated = calculate_part_with_resolved_references(kernel, next, &calculated, relation_policy);
    apply_relation_parameters(next,document::physical_values(next,calculated));
    const bool sketches_changed = next.sketches.size() != previous.sketches.size() ||
        !std::equal(next.sketches.begin(), next.sketches.end(), previous.sketches.begin(),
            [](const auto& left, const auto& right) { return left.serialized() == right.serialized(); });
    if (references_changed || sketches_changed || next.appearance!=previous.appearance ||
        next.body_color!=previous.body_color || next.face_colors!=previous.face_colors || next.user_parameters!=previous.user_parameters ||
        next.user_parameter_values!=previous.user_parameter_values || next.user_parameter_order!=previous.user_parameter_order ||
        zima::document::serialize_sections(next.sections)!=zima::document::serialize_sections(previous.sections) || next.history != previous.history ||
        next.reference_errors != previous.reference_errors || next.removed_reference_states!=previous.removed_reference_states ||
        next.constructions != previous.constructions ||
        next.body_history.bodies() != previous.body_history.bodies() ||
        // Numeric equality can hide a changed rigid Euler zero sign. Publish
        // the exact document used by this calculation when its persisted input
        // fingerprint differs, rather than attaching the new result to the old
        // document. Keep authored side data and the fingerprint format intact.
        (!calculated.empty() && calculated.back().source_fingerprint !=
            kernel::history_fingerprint(group_solid_state_cached_operations(previous,&calculated),calculated.size()))) {
        part->session.commit(std::move(next), std::move(calculated));
    } else {
        part->session.update_calculated_boundaries(std::move(calculated));
    }
    return {references_changed};
}

void calculate_body_properties(Workspace& workspace,const kernel::OcctKernel& kernel,
    const std::string& id,const document::BodyProperties& position) {
    auto* part=workspace.open_part(id);
    if(!part)throw std::invalid_argument("Body properties require an open Part.");
    auto calculated=part->session.calculated_boundaries();
    const auto inputs=document::body_properties_inputs(part->session.document(),calculated,position);
    std::map<std::string,kernel::BodyResult> analysis;
    for(const auto& input:inputs)if(!input.body->physical_properties_current) {
        auto body=*input.body;
        const auto integrate=[&]{kernel.ensure_body_properties(body);};
        if(calculation_runner)calculation_runner(integrate);else integrate();
        analysis.insert_or_assign(body.source_fingerprint,std::move(body));
    }
    if(analysis.empty())return;
    const auto update=[&](auto&& self,kernel::BodyResult& result)->bool {
        bool changed=false;
        if(const auto found=analysis.find(result.source_fingerprint);found!=analysis.end()&&!result.physical_properties_current) {
            const auto& source=found->second;
            result.volume=source.volume;result.surface_area=source.surface_area;
            result.volume_integrals=source.volume_integrals;result.surface_centroid=source.surface_centroid;
            result.physical_properties_current=true;result.solid_present=source.solid_present;changed=true;
        }
        for(auto& [body,values]:result.body_boundaries) {
            auto local=values.get();bool updated=false;
            for(auto& value:local)updated=self(self,value)||updated;
            if(updated){values=std::move(local);changed=true;}
        }
        for(auto* snapshots:{&result.body_inputs,&result.body_outputs})for(auto& [body,value]:*snapshots) {
            auto local=value.get();
            if(self(self,local)){value=std::move(local);changed=true;}
        }
        return changed;
    };
    for(auto& result:calculated)update(update,result);
    part->session.update_calculated_boundaries(std::move(calculated));
}

static bool complete_assembly_analysis(Workspace& live,const kernel::OcctKernel& kernel,
    assembly::AssemblyDocument& document,std::set<std::string>& visiting) {
    if(!visiting.insert(document.document_id).second)
        throw std::runtime_error("External Sketch document dependency cycle detected");
    bool changed=false;
    for(auto& component:document.components) {
        if(component.source_missing||assembly::is_skeleton(component)||component.calculated_source->physical_properties_current)continue;
        if(component.source_kind==assembly::ComponentSourceKind::Assembly) {
            auto source=live.open_assembly(component.source_document_id)
                ?live.open_assembly(component.source_document_id)->session.document()
                :read_family_assembly(&live,component.source_path,component.source_document_id,false);
            complete_assembly_analysis(live,kernel,source,visiting);
            const auto complete=assembly::AssemblyDocument::create_assembly_occurrence(
                component.name,component.source_document_id,component.source_path,source);
            auto snapshot=complete.calculated_source.get();
            snapshot.source_fingerprint=component.calculated_source->source_fingerprint;
            component.calculated_source=std::move(snapshot);
            assembly::capture_nested_mass(component,source);
            // This is source analysis only. The owning Assembly's component
            // placements, mates, hierarchy and authored properties stay intact.
            if(auto* open=live.open_assembly(source.document_id))open->session.update_source_geometry(std::move(source));
        } else {
            auto analyzed=component.calculated_source.get();
            const auto integrate=[&]{kernel.ensure_body_properties(analyzed);};
            if(calculation_runner)calculation_runner(integrate);else integrate();
            component.calculated_source=std::move(analyzed);
            if(auto* open=live.open_part(component.source_document_id)) {
                auto boundaries=open->session.calculated_boundaries();
                if(!boundaries.empty()&&boundaries.back().source_fingerprint==component.calculated_source->source_fingerprint&&
                    !boundaries.back().physical_properties_current) {
                    boundaries.back()=component.calculated_source.get();
                    open->session.update_calculated_boundaries(std::move(boundaries));
                }
            }
        }
        changed=true;
    }
    visiting.erase(document.document_id);return changed;
}

static bool complete_assembly_analysis(Workspace& live,const kernel::OcctKernel& kernel,
    assembly::AssemblyDocument& document) {
    std::set<std::string> visiting;return complete_assembly_analysis(live,kernel,document,visiting);
}

static void regenerate_assembly_impl(Workspace& workspace, const kernel::OcctKernel& kernel,
    const std::string& document_id, const PartCalculationPolicy& policy) {
    const auto id=document_id;
    reconcile_external_sketch_dependencies(workspace,id);
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
            std::set<std::string> sources;
            bool has_context=false;
            visit_document_sketches(part->session.document(),[&](const auto& sketch) {
                for(const auto& reference:sketch.external_references)
                    if(reference.context_assembly_document_id==id) {
                        has_context=true;
                        if(reference.source_document_id!=part_id)sources.insert(reference.source_document_id);
                    }
                return true;
            });
            for(const auto& source:sources)regenerate_part_dependencies(source);
            part=workspace.open_part(part_id);
            auto next=part->session.document();
            if (has_context) {
                const auto& previous = part->session.calculated_boundaries();
                auto calculated = calculate_part(kernel, next, &previous, policy);
                const auto previous_constructions = next.constructions;
                next.resolve_constructions(calculated.empty()
                    ? zima::kernel::ViewerReferenceGeometry{}
                    : calculated.back().mesh.original_references);
                const bool references_changed =
                    refresh_sketch_external_references(next, calculated) |
                    workspace.refresh_context_external_references(next) |
                    refresh_context_sketch_intersections(workspace,kernel,next);
                if (references_changed) {
                    calculated = calculate_part_with_resolved_references(kernel, next, &calculated, policy);
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
        const auto intersection_sketches=[](const auto& document) {
            std::vector<std::string> result;
            visit_document_sketches(document,[&](const auto& sketch) {
                if(std::ranges::any_of(sketch.external_references,[](const auto& r){return r.intersection.has_value();}))result.push_back(sketch.serialized());
                return true;
            });return result;
        };
        const auto before_intersections=intersection_sketches(next);
        calculate_resolved_assembly_cuts(kernel, next,true);
        const bool intersections_changed=before_intersections!=intersection_sketches(next);
        const bool analysis_changed=complete_assembly_analysis(workspace,kernel,next);
        if (references_changed || intersections_changed || !next.cuts.empty()) {
            regenerated->session.commit(std::move(next));
        } else if(analysis_changed)regenerated->session.update_source_geometry(std::move(next));
    }
}

void regenerate_assembly(Workspace& workspace,const kernel::OcctKernel& kernel,
    const std::string& document_id,const PartCalculationPolicy& policy) {
    const auto id=document_id;
    const auto* current=workspace.open_assembly(id);
    if(!current)throw std::invalid_argument("Regeneration requires an open Assembly document");
    if(current->session.document().relations.empty()){regenerate_assembly_impl(workspace,kernel,id,policy);return;}
    auto staged=workspace;
    auto original=current->session;
    auto draft=original.document();
    complete_assembly_analysis(staged,kernel,draft);
    apply_relation_dimensions(draft,assembly::physical_values(draft));
    staged.open_assembly(id)->session.commit(std::move(draft));
    regenerate_assembly_impl(staged,kernel,id,policy);
    auto result=staged.open_assembly(id)->session.document();
    apply_relation_parameters(result,assembly::physical_values(result));
    auto& session=staged.open_assembly(id)->session;
    session=std::move(original);
    session.commit(std::move(result));
    workspace=std::move(staged);
}

} // namespace zima::workspace

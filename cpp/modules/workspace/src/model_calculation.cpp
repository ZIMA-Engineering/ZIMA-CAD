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
#include <zima/workspace/history_policy.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/feature_material_wire.hpp>
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

static kernel::HistoryOutputPolicy history_output_policy(
    const document::PartDocument& document,const bool final_boundary_first) {
    kernel::HistoryOutputPolicy result;
    result.final_boundary_first=final_boundary_first;
    if(!result.final_boundary_first)return result;
    const auto graph=part_history_dependency_graph(document);
    visit_document_sketches(document,[&](const auto& sketch) {
        const bool needs_input=std::ranges::any_of(
            sketch.external_references,[&](const auto& reference) {
                if(reference.body_edge)return true;
                return reference.intersection&&
                    reference.source_document_id==document.document_id&&
                    reference.source_instance_path.empty()&&
                    reference.context_assembly_document_id.empty();
            });
        if(!needs_input)return true;
        const auto owner=graph.owners.find(sketch.owner_container_id);
        const auto& consumer=owner==graph.owners.end()
            ?sketch.owner_container_id:owner->second;
        if(document.find_container(consumer))
            result.input_boundary_consumers.insert(consumer);
        return true;
    });
    // Edge-treatment queries and edits resolve their persisted routes against
    // the exact input immediately before the operation.  Retain that narrow
    // edge/identity packet just like other explicit input consumers; keeping
    // it avoids either a stale empty boundary or an on-demand OCCT rebuild.
    // Drill-point repair likewise validates selected bottoms against its exact
    // input body.
    for(const auto& feature:document.history)
        if(feature.feature_kind==document::FeatureKind::Fillet||
                feature.feature_kind==document::FeatureKind::Chamfer||
                feature.feature_kind==document::FeatureKind::DrillPoint)
            result.input_boundary_consumers.insert(feature.id);
    // A Solid State boundary owns the transformed reference view consumed by
    // later placement and by a subsequent Restore.  The forward OCCT shape is
    // enough to continue geometry calculation, but the compact dependency
    // packet cannot publish that semantic face/edge/point correspondence.
    // State commands are uncommon, so retain precisely their output packets.
    for(const auto& feature:document.history)
        if(!feature.suppressed&&document::is_solid_state(feature.feature_kind))
            result.retained_output_owners.insert(feature.id);

    // Saved Body Properties remain analysis-only unless their Origin is used
    // as a real placement/construction input.  In that case AggregateProperty
    // demand is already exact; retain the measured history boundary as well so
    // the refreshed centroid is available before the downstream placement is
    // settled.  Unreferenced records continue to be completed in background.
    if(!document.body_properties.empty()) {
        std::set<std::string> referenced_origins;
        const auto collect=[&](const document::ConstructionReference& reference) {
            if(reference.instance_path.empty()&&reference.owner_id.ends_with(":origin"))
                referenced_origins.insert(reference.owner_id);
        };
        for(const auto& body:document.body_history.bodies()) {
            for(const auto& reference:body.scope.placement.references)collect(reference);
            if(body.derived_copy)collect(body.derived_copy->reference);
        }
        for(const auto& reference:graph.references)
            if(std::get<1>(reference).empty()&&std::get<2>(reference).ends_with(":origin"))
                referenced_origins.insert(std::get<2>(reference));
        const auto retain_last_feature=[&](const document::BodyHistory& body,
                const std::string& through) {
            std::size_t limit=body.entries.size();
            if(!through.empty()) {
                const auto found=std::ranges::find(body.entries,through,
                    &document::PartHistoryEntry::id);
                if(found!=body.entries.end())limit=static_cast<std::size_t>(
                    found-body.entries.begin())+1;
            }
            while(limit) {
                const auto& entry=body.entries[--limit];
                if(entry.kind!=document::PartHistoryKind::Feature)continue;
                const auto* feature=document.find_container(entry.id);
                if(feature&&feature->feature_kind!=document::FeatureKind::Sketch) {
                    result.retained_output_owners.insert(feature->id);
                    break;
                }
            }
        };
        for(const auto& row:document.body_properties) {
            if(!referenced_origins.contains(row.id+":origin"))continue;
            if(const auto* body=document.body_history.find(row.body_id)) {
                retain_last_feature(*body,row.after_object_id);
            } else if(const auto* owner=document.body_history.owner(
                    row.after_object_id)) {
                retain_last_feature(*owner,row.after_object_id);
            } else if(const auto* feature=document.find_container(
                    row.after_object_id);
                    feature&&feature->feature_kind!=document::FeatureKind::Sketch) {
                result.retained_output_owners.insert(feature->id);
            }
        }
    }
    // A cursor inside history is an explicit output consumer.  Publish that
    // exact Body boundary as part of the requested calculation; otherwise the
    // final-first packet contains only dependency edges there and the View is
    // blank immediately after inserting or editing a feature at the cursor.
    if(const auto* body=document.body_history.find(
            document.body_history.active_body_id());
            body&&body->cursor<body->entries.size()) {
        for(std::size_t index=body->cursor;index>0;--index) {
            const auto& entry=body->entries[index-1];
            if(entry.kind!=document::PartHistoryKind::Feature)continue;
            const auto* feature=document.find_container(entry.id);
            if(feature&&feature->feature_kind!=document::FeatureKind::Sketch) {
                result.retained_output_owners.insert(feature->id);
                break;
            }
        }
    } else if(document.body_history.bodies().empty()&&
            document.effective_history_cursor()<document.history_order.size()) {
        const auto cursor=document.effective_history_cursor();
        for(std::size_t index=cursor;index>0;--index) {
            const auto& entry=document.history_order[index-1];
            if(entry.kind!=document::PartHistoryKind::Feature)continue;
            const auto* feature=document.find_container(entry.id);
            if(feature&&feature->feature_kind!=document::FeatureKind::Sketch) {
                result.retained_output_owners.insert(feature->id);
                break;
            }
        }
    }
    return result;
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
    auto operations = group_solid_state_operations(kernel,document,previous);
    if(policy.calculation_operation_limit&&
            *policy.calculation_operation_limit<operations.size())
        operations.resize(*policy.calculation_operation_limit);
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
    const auto analysis_is_geometry_input=[&] {
        if(document.body_properties.empty())return false;
        std::set<std::string> origins;
        for(const auto& row:document.body_properties)origins.insert(row.id+":origin");
        const auto referenced=[&](const document::ConstructionReference& reference) {
            return reference.instance_path.empty()&&origins.contains(reference.owner_id);
        };
        for(const auto& body:document.body_history.bodies()) {
            if(std::ranges::any_of(body.scope.placement.references,referenced))return true;
            if(body.derived_copy&&referenced(body.derived_copy->reference))return true;
        }
        const auto graph=part_history_dependency_graph(document);
        return std::ranges::any_of(graph.references,[&](const auto& reference) {
            return std::get<1>(reference).empty()&&origins.contains(std::get<2>(reference));
        });
    };
    const auto demand=eager_analysis||analysis_is_geometry_input()
        ?kernel::AggregatePropertyDemand::All
        :policy.refresh_physical_properties
            ?kernel::AggregatePropertyDemand::Final
            :document.body_properties.empty()
                ?kernel::AggregatePropertyDemand::Deferred
                :kernel::AggregatePropertyDemand::DeferredAdaptive;
    kernel::AggregatePropertyScope properties(demand);
    auto calculated = kernel.evaluate_history_recovering(operations,
        calculation_input == nullptr ? empty_previous : *calculation_input,
        // Ordinary regeneration needs the current final Body immediately too.
        // Dependency analysis retains the exact earlier inputs that are
        // actually consumed; all other history rows stay as lightweight live
        // native boundaries instead of publishing a full mesh/B-Rep packet.
        history_output_policy(document,!policy.publish_complete_history));
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
    refresh_cached_external_profile_target_frames(document);
    std::vector<zima::kernel::BodyResult> calculated;
    const auto* incremental_source = previous;
    bool may_reuse_local_sketch_references=
        policy.reuse_unchanged_local_sketch_references;
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
        const auto history_before = document.history;
        const auto constructions_before = document.constructions;
        const auto bodies_before = document.body_history.bodies();
        const auto flat_profiles_before=attached_flat_profiles();
        const auto sketch_frames_before=sketch_frames();
        auto phase_start=std::chrono::steady_clock::now();
        const auto phase=[&](const char* name) {
            const auto now=std::chrono::steady_clock::now();
            if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,"Part settling pass=%zu %s: %.3f ms\n",pass,name,
                std::chrono::duration<double,std::milli>(now-phase_start).count());
            phase_start=now;
        };
        PartCalculationPolicy calculation_policy;
        calculation_policy.refresh_physical_properties=policy.refresh_physical_properties;
        // Editing prioritizes the final visible result. Intermediate packets
        // without a real calculation dependency are completed only when a
        // later history editor explicitly requests that rollback boundary.
        calculation_policy.edited_history_limit=policy.edited_history_limit;
        calculated = calculate_part(kernel, document, incremental_source, calculation_policy);
        phase("calculation");
        for(const auto& boundary:calculated)if(!boundary.resolved_up_to_limits.empty())for(auto& feature:document.history)
            if(feature.feature_kind==document::FeatureKind::Feature&&!boundary.calculation_errors.contains(feature.id))
                static_cast<void>(document::resolve_feature_up_to_choices(feature.feature,feature.id,feature.feature_id,boundary.resolved_up_to_limits));
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
        bool unchanged_source_prefix=(policy.reuse_unchanged_local_sketch_references ||
            policy.reuse_unchanged_reference_resolution) && previous&&
            calculated.size()==previous->size()&&!calculated.empty()&&
            policy.edited_history_limit&&*policy.edited_history_limit+1==document.history.size();
        for(std::size_t index=0;unchanged_source_prefix&&index+1<calculated.size();++index)
            unchanged_source_prefix=calculated[index].source_fingerprint==
                previous->at(index).source_fingerprint;
        std::optional<std::vector<kernel::HistoryOperation>> unchanged_operations;
        const bool last_operation_proven=unchanged_source_prefix&&
            !policy.unchanged_last_operation_owner.empty()&&
            document.history[*policy.edited_history_limit].id==
                policy.unchanged_last_operation_owner;
        if(unchanged_source_prefix&&!last_operation_proven) {
            auto operations=group_solid_state_cached_operations(document,&calculated);
            unchanged_source_prefix=operations.size()==calculated.size()&&
                operations.back().owner_id==document.history[*policy.edited_history_limit].id;
            if(unchanged_source_prefix)unchanged_operations=std::move(operations);
        }
        const bool unchanged_reference_inputs = document.history==history_before &&
            document.constructions==constructions_before &&
            document.body_history.bodies()==bodies_before &&
            attached_flat_profiles()==flat_profiles_before &&
            sketch_frames()==sketch_frames_before;
        const bool reuse_reference_resolution=
            policy.reuse_unchanged_reference_resolution &&
            unchanged_source_prefix && unchanged_reference_inputs;
        if(!reuse_reference_resolution) {
            auto reference_geometry=construction_resolution_geometry(document,calculated);
            const auto state_views=calculated.empty()?document::HistoryReferenceViews{}:
                document::solid_state_reference_views(document,calculated.back(),reference_geometry);
            phase("reference preparation");
            document.resolve_constructions(std::move(reference_geometry),state_views);
            phase("reference resolution");
        }
        // A changed placement describes the next geometry pass. Projecting
        // this pass's old source mesh into that new Sketch frame can create
        // a false constraint conflict while the dependency chain is settling.
        if(document.history!=history_before||document.constructions!=constructions_before||
            document.body_history.bodies()!=bodies_before||attached_flat_profiles()!=flat_profiles_before||
            sketch_frames()!=sketch_frames_before) {
            may_reuse_local_sketch_references=false;
            incremental_source=&calculated;continue;
        }
        const bool reuse_local_sketch_references=
            may_reuse_local_sketch_references&&unchanged_source_prefix;
        const bool external_references_changed = reuse_local_sketch_references?false:
            refresh_sketch_external_references(document, calculated,&kernel);
        phase("Sketch external references");
        const bool drill_points_changed =
            prune_missing_drill_point_references(document, calculated);
        phase("Drill Point references");
        const bool profile_targets_changed=refresh_profile_end_targets(document,calculated);
        phase("profile targets");
        const bool opening_targets_changed=refresh_opening_end_targets(document,calculated);
        phase("opening targets");
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,
            "Part settling pass=%zu changes external=%d drill=%d profile=%d opening=%d history=%d constructions=%d bodies=%d frames=%d\n",
            pass,int(external_references_changed),int(drill_points_changed),
            int(profile_targets_changed),int(opening_targets_changed),
            int(document.history!=history_before),
            int(document.constructions!=constructions_before),
            int(document.body_history.bodies()!=bodies_before),
            int(sketch_frames()!=sketch_frames_before));
        const auto current_fingerprint=[&] {
            if(unchanged_operations)
                return kernel::history_fingerprint(*unchanged_operations,calculated.size());
            return kernel::history_fingerprint(
                group_solid_state_cached_operations(document,&calculated),calculated.size());
        };
        if (!external_references_changed && !drill_points_changed && !profile_targets_changed && !opening_targets_changed &&
            attached_flat_profiles()==flat_profiles_before && sketch_frames()==sketch_frames_before &&
            document.history == history_before &&
            document.constructions == constructions_before &&
            document.body_history.bodies() == bodies_before &&
            (calculated.empty() || reuse_reference_resolution || calculated.back().source_fingerprint ==
                current_fingerprint())) {
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
    zima::assembly::AssemblyDocument& document,bool refresh_physical_properties,const Workspace* authoritative_sources) {
    if (calculation_runner) {
        const auto runner = calculation_runner;
        runner([&] {
            CalculationExecutionScope synchronous({});
            calculate_resolved_assembly_cuts(kernel,document,refresh_physical_properties,authoritative_sources);
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
        if(std::ranges::none_of(sketch.external_references,[](const auto& r){return sketcher::is_intersection_reference(r);}))return;
        const auto values=calculate_assembly_sketch_intersections(kernel,document,sketch,authoritative_sources);
        if(!values.empty())static_cast<void>(sketch.refresh_external_references({}, {},false,nullptr,false,&values));
    };
    std::set<std::string> refreshed_sketches;
    const auto refresh_standalone_before=[&](std::size_t boundary) {
        for(auto& sketch:document.sketches)if(document.find_sketch_container(sketch.owner_container_id)&&
            order_index(sketch.owner_container_id)<boundary&&refreshed_sketches.insert(sketch.id).second)refresh_intersection(sketch);
    };
    for (auto& cut : document.cuts) {
        cut.input_component_bodies.clear();
        cut.preview_wire.clear();
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
        bool bounded_tool=false;
        if(cut.definition.feature_kind==document::FeatureKind::Feature)
            for(std::size_t side=0;side<(cut.definition.feature.symmetric?1u:2u);++side) {
                const auto value=cut.definition.feature.effective_side(side);
                bounded_tool|=value.operation==document::FeatureSideOperation::Extrusion&&value.extrusion_extent==document::EndCondition::ThroughAll;
            }
        const auto cutter_boundaries = bounded_tool
            ? std::vector<kernel::BodyResult>{kernel.calculate_feature_tool(cutter_operations.front(),{}, {},
                feature_preview_assembly_input(document,cut.target_occurrence_ids),
                feature_preview_external_sources(document,cut.definition))}
            : kernel.evaluate_history(cutter_operations);
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
        for (const auto& edge : cutter_boundaries.back().mesh.original_references.edges)
            if (document::feature_material_wire_edge(cut.definition,edge))
                cut.preview_wire.push_back(edge);
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
    // Calculated physical quantities are output-only. RelationProgram rejects
    // every direct or transitive attempt to drive a dimension from them, so
    // exact mass/inertia must not extend the geometry regeneration path.
    apply_relation_dimensions(next,document::physical_values(previous,relation_input));
    refresh_body_links(workspace,next,part->path);
    refresh_external_profile_targets(workspace,kernel,next);
    // Native snapshots must be recalculated by this runtime at least once.
    // Thereafter explicit regeneration uses the existing dependency-aware
    // incremental calculation, retaining only unchanged live calculations.
    auto relation_policy=policy;
    relation_policy.refresh_physical_properties=false;
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

namespace {
struct HistoryBoundaryLocation {
    std::string body_id;
    std::size_t body_index{};
};

HistoryBoundaryLocation history_boundary_location(
        const std::vector<kernel::HistoryOperation>& operations,
        const std::vector<kernel::BodyResult>& calculated,
        const std::size_t operation_count) {
    if(operation_count==0||operation_count>operations.size()||
            operation_count>calculated.size())
        throw std::out_of_range(
            "History boundary materialization is outside the calculated history");
    HistoryBoundaryLocation result;
    result.body_id=operations[operation_count-1].body.id;
    if(result.body_id.empty())return result;
    for(std::size_t index=0;index<operation_count;++index)
        if(operations[index].body.id==result.body_id)++result.body_index;
    const auto found=calculated.back().body_boundaries.find(result.body_id);
    if(found==calculated.back().body_boundaries.end()||
            found->second.size()<result.body_index)
        throw std::runtime_error("Calculated Body history boundary is missing");
    return result;
}

HistoryBoundaryLocation history_boundary_location(
        const std::vector<kernel::HistoryOperation>& operations,
        const std::size_t operation_count) {
    if(operation_count==0||operation_count>operations.size())
        throw std::out_of_range(
            "History boundary materialization is outside the operation plan");
    HistoryBoundaryLocation result;
    result.body_id=operations[operation_count-1].body.id;
    if(result.body_id.empty())return result;
    for(std::size_t index=0;index<operation_count;++index)
        if(operations[index].body.id==result.body_id)++result.body_index;
    return result;
}

const kernel::BodyResult& history_boundary(
        const std::vector<kernel::BodyResult>& calculated,
        const std::size_t operation_count,
        const HistoryBoundaryLocation& location) {
    if(location.body_id.empty())return calculated[operation_count-1];
    return calculated.back().body_boundaries.at(location.body_id)[
        location.body_index-1];
}

bool complete_history_geometry(const kernel::BodyResult& value,
        const kernel::HistoryOperation& operation) {
    // A compact dependency row may intentionally contain identity-bearing
    // sampled edges only. That is sufficient for reference settling, but it is
    // not the real rollback body required by Properties. A deliberately empty
    // datum Feature is already complete without a solid viewer mesh.
    if(!value.mesh.triangles.empty())return true;
    const auto* group=std::get_if<kernel::FeatureGroupRequest>(
        &operation.primitive);
    return group&&group->allow_empty&&group->children.empty()&&
        value.kernel_shape.empty();
}
}

std::optional<std::size_t> deferred_part_history_boundary(
        const document::PartDocument& document,
        const std::vector<kernel::BodyResult>& calculated) {
    if(calculated.empty())return std::nullopt;
    const auto operations=group_solid_state_cached_operations(
        document,&calculated);
    if(operations.size()!=calculated.size())return std::nullopt;
    for(std::size_t index=0;index+1<operations.size();++index) {
        const auto& operation=operations[index];
        if(operation.suppressed||
                calculated.back().calculation_errors.contains(
                    operation.owner_id))continue;
        const auto location=history_boundary_location(
            operations,calculated,index+1);
        if(!complete_history_geometry(history_boundary(
                calculated,index+1,location),operation))return index+1;
    }
    return std::nullopt;
}

bool complete_part_history_boundary(const kernel::OcctKernel& kernel,
        const document::PartDocument& document,
        std::vector<kernel::BodyResult>& current,
        const std::size_t operation_count,
        const std::string& known_body_id,
        const std::size_t known_body_index) {
    if(operation_count==0)return false;
    const auto operations=group_solid_state_cached_operations(
        document,&current);
    if(operation_count>operations.size())throw std::out_of_range(
        "History boundary materialization is outside the operation plan");
    const auto& operation=operations[operation_count-1];
    HistoryBoundaryLocation location;
    if(!known_body_id.empty()) {
        location.body_id=known_body_id;
        location.body_index=known_body_index;
        const auto found=current.back().body_boundaries.find(
            location.body_id);
        if(location.body_index==0||
           found==current.back().body_boundaries.end()||
           found->second.size()<location.body_index)
            throw std::runtime_error(
                "Known calculated Body history boundary is missing");
    } else {
        location=history_boundary_location(operations,operation_count);
    }
    if(current.size()>=operation_count) {
        if(location.body_id.empty()) {
            if(complete_history_geometry(
                    current[operation_count-1],operation))
                return false;
        } else {
            const auto found=current.back().body_boundaries.find(
                location.body_id);
            if(found==current.back().body_boundaries.end()||
                    found->second.size()<location.body_index)
                throw std::runtime_error(
                    "Calculated Body history boundary is missing");
            if(complete_history_geometry(found->second[
                    location.body_index-1],operation))return false;
        }
    }
    PartCalculationPolicy policy;
    policy.calculation_operation_limit=operation_count;
    // This is an explicit exact prefix calculation.  Supplying even a compact
    // previous-boundary packet selects incremental replay and prevents the
    // kernel's direct grouped regeneration.  All authored inputs are already
    // present in the document, so calculate the requested prefix directly and
    // merge only its proven output into the immutable final-history packet.
    auto calculated=calculate_part(
        kernel,document,nullptr,policy);
    if(calculated.size()!=operation_count||calculated.empty()||
            !calculated.back().calculation_errors.empty())
        throw std::runtime_error("History boundary materialization failed");
    kernel::BodyResult published_boundary=calculated.back();
    kernel::BodyResult body_boundary=published_boundary;
    if(!location.body_id.empty()) {
        const auto found=calculated.back().body_boundaries.find(
            location.body_id);
        if(found==calculated.back().body_boundaries.end()||
                found->second.size()<location.body_index)
            throw std::runtime_error(
                "Materialized Body history boundary is missing");
        body_boundary=found->second[location.body_index-1];
    }
    if(!complete_history_geometry(body_boundary,operation))
        throw std::runtime_error(
            "Materialized history boundary has no geometry");
    if(current.size()<operation_count) {
        // A document saved without a calculated cache can still contain a
        // valid earlier input before a broken downstream feature. Publish the
        // explicitly calculated prefix for editing that embedded Sketch.
        current=std::move(calculated);
        return true;
    }
    // The top-level row carries the global operation-prefix fingerprint. The
    // nested row carries the Body-local prefix fingerprint. They describe the
    // same shape here, but are distinct persistence contracts.
    current[operation_count-1]=std::move(published_boundary);
    if(!location.body_id.empty()) {
        auto rows=current.back().body_boundaries.at(location.body_id).get();
        rows[location.body_index-1]=std::move(body_boundary);
        current.back().body_boundaries[location.body_id]=std::move(rows);
    }
    return true;
}

bool materialize_part_history_boundary(Workspace& workspace,
    const kernel::OcctKernel& kernel,const std::string& document_id,
    const std::size_t operation_count) {
    auto* part=workspace.open_part(document_id);
    if(!part)throw std::invalid_argument(
        "History boundary materialization requires an open Part document");
    auto published=part->session.calculated_boundaries();
    if(!complete_part_history_boundary(
            kernel,part->session.document(),published,operation_count))
        return false;
    part->session.update_calculated_cache_only(std::move(published));
    return true;
}

bool materialize_part_history_input_boundary(Workspace& workspace,
    const kernel::OcctKernel& kernel,const std::string& document_id,
    const std::string& container_id) {
    auto* part=workspace.open_part(document_id);
    if(!part)throw std::invalid_argument(
        "History input materialization requires an open Part document");
    const auto& document=part->session.document();
    if(!document.find_container(container_id))return false;
    std::string preceding_owner;
    std::string body_id;
    if(!document.body_history.bodies().empty()) {
        const auto boundary=document.body_history.rollback_before(container_id);
        body_id=boundary.body_id;
        const auto* body=document.body_history.find(body_id);
        if(!body)throw std::runtime_error("History input Body is missing");
        for(std::size_t entry=boundary.entry_count;entry>0;--entry) {
            const auto& candidate=body->entries[entry-1];
            if(candidate.kind!=document::PartHistoryKind::Feature)continue;
            const auto* feature=document.find_container(candidate.id);
            if(feature&&feature->feature_kind!=document::FeatureKind::Sketch) {
                preceding_owner=feature->id;
                break;
            }
        }
    } else {
        const auto target=document.history_index(container_id);
        if(!target)return false;
        for(std::size_t index=*target;index>0;--index) {
            const auto& feature=document.history[index-1];
            if(feature.feature_kind!=document::FeatureKind::Sketch) {
                preceding_owner=feature.id;
                break;
            }
        }
    }
    if(preceding_owner.empty())return false;
    const auto operations=group_solid_state_cached_operations(
        document,&part->session.calculated_boundaries());
    std::optional<std::size_t> operation_count;
    for(std::size_t index=0;index<operations.size();++index)
        if(operations[index].owner_id==preceding_owner&&
                (body_id.empty()||operations[index].body.id==body_id))
            operation_count=index+1;
    if(!operation_count)throw std::runtime_error(
        "History input operation is missing from the calculated plan");
    return materialize_part_history_boundary(
        workspace,kernel,document_id,*operation_count);
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

bool complete_part_physical_analysis(const kernel::OcctKernel& kernel,
    document::PartDocument& document,std::vector<kernel::BodyResult>& calculated,
    bool include_final_body) {
    if(calculated.empty())return false;
    std::map<std::string,kernel::BodyResult> analysis;
    const auto request=[&](const kernel::BodyResult& source) {
        if(source.physical_properties_current)return;
        if(source.source_fingerprint.empty())
            throw std::runtime_error("Deferred physical analysis has no source fingerprint");
        if(analysis.contains(source.source_fingerprint))return;
        auto body=source;kernel.ensure_body_properties(body);
        analysis.emplace(body.source_fingerprint,std::move(body));
    };
    if(include_final_body)request(calculated.back());
    for(const auto& row:document.body_properties)try {
        for(const auto& input:document::body_properties_inputs(document,calculated,row))request(*input.body);
    }catch(const std::invalid_argument&) {
        // Broken records remain editable/removable and keep their established
        // error. They must not block unrelated final-body analysis.
    }
    if(analysis.empty())return false;
    const auto update=[&](auto&& self,kernel::BodyResult& result)->bool {
        bool changed=false;
        if(const auto found=analysis.find(result.source_fingerprint);
                found!=analysis.end()&&!result.physical_properties_current) {
            const auto& source=found->second;
            result.volume=source.volume;result.surface_area=source.surface_area;
            result.volume_integrals=source.volume_integrals;
            result.surface_centroid=source.surface_centroid;
            result.physical_properties_current=true;
            result.solid_present=source.solid_present;changed=true;
        }
        for(auto& [body,values]:result.body_boundaries) {
            auto local=values.get();bool updated=false;
            for(auto& value:local)updated=self(self,value)||updated;
            if(updated){values=std::move(local);changed=true;}
        }
        for(auto* snapshots:{&result.body_inputs,&result.body_outputs})
            for(auto& [body,value]:*snapshots) {
                auto local=value.get();
                if(self(self,local)){value=std::move(local);changed=true;}
            }
        return changed;
    };
    bool changed=false;
    for(auto& result:calculated)changed=update(update,result)||changed;
    if(changed) {
        document::refresh_body_properties(document,calculated);
        static_cast<void>(apply_physical_relation_parameters(
            document,document::physical_values(document,calculated)));
    }
    return changed;
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
            bool has_external_targets=false;
            for(const auto& feature:part->session.document().history)if(feature.feature_kind==document::FeatureKind::Feature)
                for(const auto& side:feature.feature.sides)for(const auto& target:side.targets)
                    if(target.external_source&&target.external_source->context_assembly_id==id) {
                        has_context=has_external_targets=true;sources.insert(target.external_source->document_id);
                    }
            for(const auto& source:sources)regenerate_part_dependencies(source);
            part=workspace.open_part(part_id);
            auto next=part->session.document();
            if (has_context) {
                const auto previous_history=next.history;
                if(has_external_targets)refresh_external_profile_targets(workspace,kernel,next);
                const auto& previous = part->session.calculated_boundaries();
                auto calculated = has_external_targets?calculate_part_with_resolved_references(kernel,next,&previous,policy):
                    calculate_part(kernel, next, &previous, policy);
                const auto previous_constructions = next.constructions;
                next.resolve_constructions(calculated.empty()
                    ? zima::kernel::ViewerReferenceGeometry{}
                    : calculated.back().mesh.original_references);
                const bool references_changed =
                    (next.history!=previous_history) |
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
                if(std::ranges::any_of(sketch.external_references,[](const auto& r){return sketcher::is_intersection_reference(r);}))result.push_back(sketch.serialized());
                return true;
            });return result;
        };
        const auto before_intersections=intersection_sketches(next);
        calculate_resolved_assembly_cuts(kernel,next,false,&workspace);
        const bool intersections_changed=before_intersections!=intersection_sketches(next);
        if (references_changed || intersections_changed || !next.cuts.empty()) {
            regenerated->session.commit(std::move(next));
        }
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

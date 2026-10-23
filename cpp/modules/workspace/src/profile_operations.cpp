#include <zima/document/sketch_placement.hpp>
#include <zima/document/feature_serialization.hpp>
#include <zima/document/feature_up_to.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include "ordered_up_to_assembly_sources.hpp"
#include <zima/workspace/origin_display_operations.hpp>
#include <algorithm>
#include <cmath>

namespace zima::workspace {
void normalize_owned_profile_front_references(
        std::vector<zima::document::ConstructionReference>& references) {
    document::normalize_container_front_references(references);
}

std::string revolution_axis_segment_id(
    const zima::sketcher::Sketch& sketch,
    const std::string& configured_id) {
    const auto valid_axis = [&](const auto& segment) {
        return segment.construction && segment.centerline;
    };
    if (!configured_id.empty()) {
        const auto configured = std::find_if(
            sketch.segments.begin(), sketch.segments.end(), [&](const auto& segment) {
                return segment.id == configured_id && valid_axis(segment);
            });
        if (configured != sketch.segments.end()) return configured->id;
    }
    std::string result;
    for (const auto& segment : sketch.segments) {
        if (!valid_axis(segment)) continue;
        result = segment.id;
        break;
    }
    if (result.empty()) {
        throw std::runtime_error(
            "Ve skice rotace nakreslete zelenou konstrukční osu.");
    }
    return result;
}


namespace {
bool is_profile(document::FeatureKind kind) {
    return kind == document::FeatureKind::Extrusion || kind == document::FeatureKind::Revolution ||
        kind == document::FeatureKind::Feature;
}
}
void validate_profile_definition(const document::HistoryContainer& value) {
    if (value.feature_kind == document::FeatureKind::Feature) {
        try { document::validate_feature_parameters(value.feature); }
        catch (const std::invalid_argument&) {
            throw ProfileOperationError("invalid_arguments", "Invalid Feature definition.");
        }
        if (value.is_surface_result() && value.combine_mode == document::CombineMode::Subtract)
            throw ProfileOperationError("invalid_arguments", "A surface cannot subtract material.");
        for (std::size_t i=0; i<2; ++i) {
            const auto& side=value.feature.effective_side(i);
            if(value.feature.type==document::FeatureType::Axis&&!side.axis_enabled)continue;
            if (side.operation==document::FeatureSideOperation::Revolution &&
                side.rotation_extent==document::FeatureRotationExtent::UpTo &&
                (side.targets.empty() || std::ranges::any_of(side.targets,[](const auto& t){return !t.reference.valid();})))
                throw ProfileOperationError("missing_reference", "Select at least one Feature end reference.");
            if (side.operation!=document::FeatureSideOperation::Extrusion) continue;
            if (side.extrusion_extent==document::EndCondition::ThroughAll && value.combine_mode!=document::CombineMode::Subtract)
                throw ProfileOperationError("invalid_arguments", "Through all is available only for a cut.");
            if (side.extrusion_extent==document::EndCondition::UpTo) {
                if (side.targets.empty() || std::ranges::any_of(side.targets,[](const auto& t){return !t.reference.valid();}))
                    throw ProfileOperationError("missing_reference", "Select at least one Feature end reference.");
                if(value.feature.type==document::FeatureType::Axis&&side.targets.size()!=1)
                    throw ProfileOperationError("missing_reference", "Select exactly one extrusion end reference.");
            }
        }
        return;
    }
    if(value.feature_kind==document::FeatureKind::Extrusion&&value.extrusion.sheet_cut_clearance&&!value.extrusion.sheet_cut)
        throw ProfileOperationError("invalid_arguments","Profile clearance is available only for Sheet Cut.");
    if(value.feature_kind==document::FeatureKind::Extrusion&&value.extrusion.sheet_cut&&
        (value.combine_mode!=document::CombineMode::Subtract||value.extrusion.result_type!=document::ProfileResultType::Solid||
         value.extrusion.profile_source!=document::ProfileSource::Internal))
        throw ProfileOperationError("invalid_arguments","Sheet Cut requires an owned closed profile projected through the sheet.");
    if(value.feature_kind==document::FeatureKind::Revolution&&value.revolution.sheet_metal&&
        (value.combine_mode!=document::CombineMode::Add||value.revolution.result_type!=document::ProfileResultType::Thin||
         value.revolution.profile_plane_offset!=0||value.revolution.profile_source!=document::ProfileSource::Internal||
         (value.revolution.sheet_attachment&&(value.revolution.thickness_override||value.revolution.thin_mode!=document::ThinMode::OneSide))))
        throw ProfileOperationError("invalid_arguments","Sheet Revolution requires an additive owned thin profile without plane offset.");
    if (!is_profile(value.feature_kind)) throw ProfileOperationError("wrong_feature", "This container is not an Extrusion or Revolution.");
    const bool extrusion = value.feature_kind == document::FeatureKind::Extrusion;
    if ((extrusion ? value.extrusion.result_type : value.revolution.result_type) == document::ProfileResultType::Surface &&
        value.combine_mode == document::CombineMode::Subtract)
        throw ProfileOperationError("invalid_arguments", "A surface cannot subtract material.");
    const auto number = [](double value, double minimum, double maximum) {
        if (!std::isfinite(value) || value < minimum || value > maximum)
            throw ProfileOperationError("invalid_arguments", "Profile dimension is outside the supported range.");
    };
    number(extrusion ? value.extrusion.profile_plane_offset : value.revolution.profile_plane_offset, -1000000, 1000000);
    number(extrusion ? value.extrusion.thin_thickness : value.revolution.thin_thickness, 0.001, 1000000);
    number(extrusion ? value.extrusion.length_forward : value.revolution.angle_degrees, 0.001, extrusion ? 1000000 : 360);
    number(extrusion ? value.extrusion.length_reverse : value.revolution.angle_reverse, 0.001, extrusion ? 1000000 : 360);
    if (!extrusion) return;
    const auto side = [&](document::EndCondition condition, const auto& targets) {
        if (condition == document::EndCondition::UpTo && (targets.empty() || !targets.front().reference.valid()))
            throw ProfileOperationError("missing_reference", "Select a target point, plane or planar face.");
        if (condition == document::EndCondition::ThroughAll && value.combine_mode != document::CombineMode::Subtract)
            throw ProfileOperationError("invalid_arguments", "Through all is available only for a cut.");
    };
    side(value.extrusion.end_condition_forward, value.extrusion.end_targets_forward);
    if (value.extrusion.extent_mode == document::ProfileExtentMode::TwoSides)
        side(value.extrusion.end_condition_reverse, value.extrusion.end_targets_reverse);
}
document::HistoryContainer profile_from_sketch(const document::PartDocument& part,
    const std::string& sketch_id, document::FeatureKind kind) {
    if (!is_profile(kind)) throw ProfileOperationError("wrong_feature", "This container is not an Extrusion or Revolution.");
    const auto sketch = std::ranges::find(part.sketches, sketch_id, &sketcher::Sketch::id);
    if (sketch == part.sketches.end()) throw ProfileOperationError("sketch_not_found", "The requested Sketch does not exist.");
    const auto* owner = part.find_container(sketch->owner_container_id);
    if (!owner || owner->feature_kind != document::FeatureKind::Sketch)
        throw ProfileOperationError("profile_owned", "Select a standalone Sketch container for conversion to a profile feature.");
    auto value = kind == document::FeatureKind::Feature ? document::PartDocument::create_feature_container(sketch_id)
        : kind == document::FeatureKind::Extrusion ? document::PartDocument::create_extrusion_container(sketch_id)
        : document::PartDocument::create_revolution_container(sketch_id);
    value.id = owner->id; value.feature_parent_id = owner->id; value.container_origin = owner->container_origin;
    value.placement = owner->placement; value.suppressed = owner->suppressed; value.combine_mode = owner->combine_mode;
    if (value.placement.value_locks.erase("profile_offset")) value.value_locks.insert("profile_offset");
    if (kind == document::FeatureKind::Feature) value.feature.profile_plane_offset = sketch->plane_offset;
    else if (kind == document::FeatureKind::Extrusion) value.extrusion.profile_plane_offset = sketch->plane_offset;
    else value.revolution.profile_plane_offset = sketch->plane_offset;
    return value;
}
void commit_profile(Workspace& live, const kernel::OcctKernel& kernel, const std::string& id,
    document::HistoryContainer value, ProfileEditMode mode, const std::optional<sketcher::Sketch>& owned_sketch,
    ProfileFailurePolicy failure_policy) {
    auto* state = live.open_part(id);
    if (!state) throw ProfileOperationError("unsupported_document", "Profile operations require an open Part.");
    validate_profile_definition(value);
    const auto& before = state->session.document();
    const auto* existing = before.find_container(value.id);
    if (mode == ProfileEditMode::Create) {
        if (existing || value.id.empty() || value.feature_id.empty())
            throw ProfileOperationError("identity_changed", "A new container must have a new nonempty identity.");
    } else {
        if (!existing) throw ProfileOperationError("container_not_found", "The requested container does not exist.");
        const bool transform = mode == ProfileEditMode::TransformSketch;
        if (transform ? existing->feature_kind != document::FeatureKind::Sketch : existing->feature_kind != value.feature_kind)
            throw ProfileOperationError("wrong_feature", "The requested profile type does not match the container.");
        if (existing->container_origin != value.container_origin || value.feature_parent_id != existing->id ||
            (!transform && value.feature_id != existing->feature_id) || (transform && (value.feature_id.empty() || value.feature_id == existing->feature_id)))
            throw ProfileOperationError("identity_changed", "Editing must preserve the container identity.");
    }
    const auto* body = existing ? before.body_owner_for_object(value.id) : before.body_history.find(before.body_history.active_body_id());
    if (body && body->derived_copy) throw ProfileOperationError("read_only_body", "A derived Body cannot be edited directly.");
    const bool extrusion = value.feature_kind == document::FeatureKind::Extrusion;
    const bool feature = value.feature_kind == document::FeatureKind::Feature;
    const auto sketch_id = feature ? value.feature.sketch_id : extrusion ? value.extrusion.sketch_id : value.revolution.sketch_id;
    normalize_owned_profile_front_references(value.placement.references);
    auto next = before;
    if (owned_sketch) {
        if (owned_sketch->id != sketch_id) throw ProfileOperationError("identity_changed", "The profile Sketch identity does not match its feature.");
        const auto found = std::ranges::find(next.sketches, sketch_id, &sketcher::Sketch::id);
        if (found != next.sketches.end() && !found->owner_container_id.empty() && found->owner_container_id != value.id)
            throw ProfileOperationError("profile_owned", "The Sketch already belongs to another container.");
        if (found == next.sketches.end()) next.sketches.push_back(*owned_sketch);
        else *found = *owned_sketch;
    }
    const auto profile = std::ranges::find(next.sketches, sketch_id, &sketcher::Sketch::id);
    if (profile == next.sketches.end()) throw ProfileOperationError("sketch_not_found", "Internal profile Sketch no longer exists");
    if (!profile->owner_container_id.empty() && profile->owner_container_id != value.id)
        throw ProfileOperationError("profile_owned", "The Sketch already belongs to another container.");
    if (feature) {
        if (value.feature.effective_side(0).operation==document::FeatureSideOperation::Revolution ||
            value.feature.effective_side(1).operation==document::FeatureSideOperation::Revolution)
            value.feature.axis_segment_id=revolution_axis_segment_id(*profile,value.feature.axis_segment_id);
    } else if (!extrusion) value.revolution.axis_segment_id = revolution_axis_segment_id(*profile, value.revolution.axis_segment_id);
    const auto source = feature ? value.feature.profile_source : extrusion ? value.extrusion.profile_source : value.revolution.profile_source;
    if (source == document::ProfileSource::Internal) {
        profile->owner_container_id = value.id;
        document::apply_automatic_sketch_work_plane(*profile,value.placement.references);
        profile->plane_offset = feature ? value.feature.profile_plane_offset : extrusion ? value.extrusion.profile_plane_offset : value.revolution.profile_plane_offset;
    }
    // Confirming an unchanged definition must keep its current calculation and
    // Undo boundary. The owned Sketch is part of the edit, not just its owner.
    if (mode == ProfileEditMode::Replace && existing) {
        const auto original_profile = std::ranges::find(before.sketches, sketch_id, &sketcher::Sketch::id);
        if (original_profile != before.sketches.end() &&
            original_profile->serialized() == profile->serialized() &&
            commit_origin_display_only(live,id,value).has_value()) return;
    }
    const auto container_id = value.id;
    if (existing) *next.find_container(container_id) = std::move(value);
    else {next.insert_history_entry(document::PartHistoryKind::Feature, container_id); next.history.push_back(std::move(value));}
    if(feature)for(std::size_t side=0;side<2;++side) {
        auto& current=next.find_container(container_id)->feature.sides[side];
        if(current.targets.size()>1&&existing&&existing->feature_kind==document::FeatureKind::Feature) {
            const auto& old=existing->feature.sides[side];
            const auto primary=old.targets.size()==1?std::optional<kernel::FaceReference>{old.targets.front().reference}:old.up_to.primary_cap_source;
            if(primary) {
                if(current.up_to.primary_cap_source&&current.up_to.primary_cap_source!=primary)
                    throw ProfileOperationError("identity_changed","Editing must preserve the container identity.");
                current.up_to.primary_cap_source=primary;
            }
        }
    }
    const auto& previous = state->session.calculated_boundaries();
    if (feature) {
        auto& container=*next.find_container(container_id);
        if(container.feature.type==document::FeatureType::Modeling || container.feature.type==document::FeatureType::Axis)
        for (auto& side:container.feature.sides) {
            const bool limit=((side.operation==document::FeatureSideOperation::Extrusion ||
                (container.feature.type==document::FeatureType::Axis&&side.axis_enabled)) && side.extrusion_extent==document::EndCondition::UpTo) ||
                (side.operation==document::FeatureSideOperation::Revolution && side.rotation_extent==document::FeatureRotationExtent::UpTo);
            if(!limit)continue;
            if (container.feature.symmetric && &side==&container.feature.sides[1]) continue;
            for(std::size_t row=0;row<side.targets.size();++row)try {
                side.targets[row]=prepare_profile_end_target(next,previous,container,side.targets[row]);
            }catch(const std::runtime_error&) {
                if(document::feature_uses_extended_up_to(container.feature))
                    throw kernel::OrderedUpToError(kernel::OrderedUpToFailure::InvalidInput,row,row);
                throw;
            }
        }
    }
    if(extrusion) {
        auto& parameters=next.find_container(container_id)->extrusion;
        const auto prepare=[&](document::EndCondition condition,auto& targets) {
            if(condition!=document::EndCondition::UpTo)return;
            if(targets.size()!=1)throw ProfileOperationError("missing_reference","Select exactly one extrusion end reference.");
            targets.front()=prepare_profile_end_target(next,previous,*next.find_container(container_id),targets.front());
        };
        prepare(parameters.end_condition_forward,parameters.end_targets_forward);
        if(parameters.extent_mode==document::ProfileExtentMode::TwoSides)prepare(parameters.end_condition_reverse,parameters.end_targets_reverse);
    }
    auto geometry = construction_reference_source_geometry(previous);
    append_reference_geometry(geometry, next.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry, next.construction_viewer_mesh().original_references);
    next.resolve_constructions(geometry);
    if(!next.find_container(container_id)->placement.reference_valid)
        throw ProfileOperationError("invalid_reference", "The proposed feature placement references cannot be resolved.");
    auto policy = feature_definition_calculation_policy(before, previous, container_id);
    if (existing) {policy.edited_document_id = id; policy.edited_history_limit = before.history_index(container_id);}
    if(feature&&next.find_container(container_id)->feature.type==document::FeatureType::Modeling) {
        const auto& parameters=next.find_container(container_id)->feature;
        for(std::size_t i=0;i<(parameters.symmetric?1u:2u);++i) {
            const auto side=parameters.effective_side(i);
            const bool extended=(side.operation==document::FeatureSideOperation::Extrusion&&side.extrusion_extent==document::EndCondition::UpTo&&
                (side.targets.size()>1||side.up_to.length_correction!=0))||
                (side.operation==document::FeatureSideOperation::Revolution&&side.rotation_extent==document::FeatureRotationExtent::UpTo&&
                (side.targets.size()>1||side.up_to.angle_correction_degrees!=0));
            if(extended) {
                policy.reject_errors=true;policy.edited_document_id=id;policy.edited_history_limit=next.history_index(container_id);break;
            }
        }
        // Interactive OK may retain a valid authored definition whose geometry
        // fails. The calculated boundary records that failure and input state;
        // structural/reference validation above still rejects invalid ownership.
        if(failure_policy==ProfileFailurePolicy::KeepDefinition)policy.reject_errors=false;
    }
    auto calculated = calculate_part_with_resolved_references(kernel, next, &previous, policy);
    static_cast<void>(refresh_sketch_external_references(next, calculated));
    commit_part_document(live,id,std::move(next), std::move(calculated));
}
kernel::HistoryOperation feature_preview_operation(document::PartDocument carrier,
        const document::HistoryContainer& definition) {
    auto* current=carrier.find_container(definition.id);
    if(!current)throw ProfileOperationError("invalid_feature","Invalid Feature definition.");
    *current=definition;
    const auto entry=std::ranges::find(carrier.history_order,definition.id,&document::PartHistoryEntry::id);
    if(entry!=carrier.history_order.end())carrier.history_cursor=static_cast<std::size_t>(entry-carrier.history_order.begin())+1;
    const auto operations=carrier.kernel_operations(true);
    const auto found=std::ranges::find(operations,definition.id,&kernel::HistoryOperation::owner_id);
    if(found==operations.end())throw ProfileOperationError("invalid_feature","Invalid Feature definition.");
    auto result=*found;result.body={};return result;
}

kernel::BodySnapshot feature_preview_part_sources(const document::DocumentSession& session,
        const std::string& container_id) {
    const auto& boundaries=session.calculated_boundaries();
    if(boundaries.empty())return {};
    const auto& document=session.document();
    const auto* input=calculated_operation_input(session,container_id);
    if(!input)return boundaries.back();
    if(document.body_history.bodies().empty())return *input;
    const auto* body=container_id.empty()?document.body_history.find(document.body_history.active_body_id()):
        document.body_owner_for_object(container_id);
    if(!body)return boundaries.back();
    // The edited Body must supply its rollback input, not the final result
    // which may already have consumed the selected target face. Other Bodies
    // retain their current immutable data and document placement.
    auto sources=boundaries.back();
    sources.body_boundaries[body->scope.id]=std::vector<kernel::BodyResult>{*input};
    sources.body_outputs[body->scope.id]=*input;
    return sources;
}

std::vector<kernel::OrderedUpToPatch> feature_preview_external_sources(
        const assembly::AssemblyDocument& source,const document::HistoryContainer& definition) {
    kernel::ExtrusionRequest request;request.ordered_end_limit.emplace();
    for(std::size_t side=0;side<(definition.feature.symmetric?1u:2u);++side) {
        const auto end=definition.feature.effective_side(side);
        if(!((end.operation==document::FeatureSideOperation::Extrusion&&end.extrusion_extent==document::EndCondition::UpTo)||
             (end.operation==document::FeatureSideOperation::Revolution&&end.rotation_extent==document::FeatureRotationExtent::UpTo)))continue;
        for(const auto& target:end.targets) {
            if(target.kind==document::EndTargetKind::Point||document::profile_target_is_datum(target.reference))continue;
            kernel::OrderedUpToPatch patch;patch.source_reference=target.reference;
            patch.target.reference=target.reference;patch.target.reference.instance_path.clear();
            request.ordered_end_limit->patches.push_back(std::move(patch));
        }
    }
    std::vector<kernel::HistoryOperation> operations{{definition.id,std::move(request)}};
    detail::attach_ordered_up_to_sources(source,operations);
    return std::get<kernel::ExtrusionRequest>(operations.front().primitive).ordered_end_limit->patches;
}

kernel::BodySnapshot feature_preview_assembly_input(const assembly::AssemblyDocument& source,
        const std::vector<std::string>& targets) {
    kernel::BodyResult result;result.kernel_shape=kernel::kernel_body_compound;
    for(const auto& id:targets) {
        const auto found=std::ranges::find(source.components,id,&assembly::PartOccurrence::occurrence_id);
        if(found==source.components.end()||found->suppressed||found->calculated_source->kernel_shape.empty())continue;
        // Wrap the immutable source rather than copying its mesh or BRep.
        kernel::BodyResult placed;placed.kernel_shape=kernel::kernel_body_compound;
        placed.kernel_compound_bodies.push_back("source");placed.body_outputs.emplace("source",found->calculated_source);
        const auto& p=found->placement;
        placed.kernel_placements.push_back({{p.x,p.y,p.z},{p.rotation_x,p.rotation_y,p.rotation_z}});
        result.kernel_compound_bodies.push_back(id);result.body_outputs.emplace(id,std::move(placed));
        kernel::HistoryOperation frame;frame.body.id=id;frame.body.translation={p.x,p.y,p.z};
        frame.body.rotation_degrees={p.rotation_x,p.rotation_y,p.rotation_z};
        result.source_fingerprint+=":"+id+":"+found->calculated_source->source_fingerprint+":"+kernel::history_fingerprint({frame},1);
    }
    return result.kernel_compound_bodies.empty()?kernel::BodySnapshot{}:kernel::BodySnapshot{std::move(result)};
}
} // namespace zima::workspace

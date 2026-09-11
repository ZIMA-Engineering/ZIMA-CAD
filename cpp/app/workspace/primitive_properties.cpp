#include "workspace_internal.hpp"
#include <zima/workspace/primitive_operations.hpp>

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::show_primitive_properties(
    zima::document::FeatureKind feature_kind,
    const std::string& container_id) {
    if (feature_kind == zima::document::FeatureKind::ShaftThread) {
        show_shaft_thread_properties(container_id);return;
    }
    if (feature_kind == zima::document::FeatureKind::Sweep2D) {show_sweep2d_properties(container_id);return;}
    if (feature_kind == zima::document::FeatureKind::HelicalSweep) {
        show_helical_sweep_properties(container_id);return;
    }
    if (feature_kind == zima::document::FeatureKind::Sweep3D) {
        show_sweep3d_properties(container_id);
        return;
    }
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    // Sketch is a HistoryContainer kind, but it is not a primitive body.
    // Keep this guard at the common entry point as well as in Tree dispatch,
    // so no caller can fall through PrimitivePropertiesDialog's default
    // shape branch (which is Box) and accidentally show Kvádr properties.
    if (feature_kind == zima::document::FeatureKind::Sketch) {
        if (part == nullptr) return;
        const auto sketch = std::find_if(
            part->session.document().sketches.begin(),
            part->session.document().sketches.end(), [&](const auto& value) {
                return value.owner_container_id == container_id;
            });
        if (sketch != part->session.document().sketches.end()) {
            show_sketch_properties(sketch->id);
        }
        return;
    }
    const bool assembly_cut = assembly != nullptr;
    if (assembly_cut && feature_kind != zima::document::FeatureKind::Extrusion &&
        feature_kind != zima::document::FeatureKind::Revolution) return;
    std::string source_sketch_id;
    // A new Assembly cut reopens without a committed container ID. Its
    // pending feature still owns the Sketch just edited in the Sketcher.
    const auto profile_owner = assembly_cut && container_id.empty() &&
            pending_profile_feature_ && pending_profile_feature_->feature_kind == feature_kind
        ? pending_profile_feature_->id : container_id;
    if (!property_owned_sketch_draft_ ||
        property_owned_sketch_draft_->owner_container_id != profile_owner) {
        property_owned_sketch_draft_.reset();
        property_owned_feature_draft_.reset();
    }
    const bool resuming_assembly_profile = assembly_cut && container_id.empty() &&
        pending_profile_feature_ &&
        pending_profile_feature_->feature_kind == feature_kind &&
        std::any_of(assembly->session.document().sketches.begin(),
            assembly->session.document().sketches.end(), [&](const auto& sketch) {
                return sketch.owner_container_id == pending_profile_feature_->id;
            });
    if (container_id.empty() &&
        (feature_kind == zima::document::FeatureKind::Extrusion ||
         feature_kind == zima::document::FeatureKind::Revolution)) {
        if (resuming_assembly_profile) {
            const auto found = std::find_if(
                assembly->session.document().sketches.begin(),
                assembly->session.document().sketches.end(), [&](const auto& sketch) {
                    return sketch.owner_container_id == pending_profile_feature_->id;
                });
            if (!property_owned_sketch_draft_) property_owned_sketch_draft_ = *found;
            source_sketch_id = property_owned_sketch_draft_->id;
        } else {
            if (assembly_cut && !selected_sketch_id_.empty()) {
                const auto selected = std::find_if(
                    assembly->session.document().sketches.begin(),
                    assembly->session.document().sketches.end(),
                    [&](const auto& sketch) {
                        return sketch.id == selected_sketch_id_ &&
                            sketch.owner_container_id.empty();
                    });
                if (selected != assembly->session.document().sketches.end()) {
                    property_owned_sketch_draft_ = *selected;
                }
            }
            if (!property_owned_sketch_draft_) {
                property_owned_sketch_draft_ = zima::sketcher::Sketch::create_default();
                property_owned_sketch_draft_->name = tr("Skica").toStdString();
            }
            source_sketch_id = property_owned_sketch_draft_->id;
        }
    }
    const auto* edited_cut = assembly_cut && !container_id.empty()
        ? assembly->session.document().find_cut(container_id) : nullptr;
    const auto* edited = assembly_cut
        ? (edited_cut == nullptr ? nullptr : &edited_cut->definition)
        : container_id.empty() ? nullptr
                               : part->session.document().find_container(container_id);
    const bool transforming_profile =
        edited != nullptr && edited->feature_kind ==
            zima::document::FeatureKind::Sketch &&
        pending_profile_feature_ &&
        pending_profile_transform_original_ &&
        pending_profile_feature_->id == container_id &&
        pending_profile_feature_->feature_kind == feature_kind &&
        pending_profile_transform_original_->id == container_id;
    if (!container_id.empty() &&
        (edited == nullptr ||
         (edited->feature_kind != feature_kind && !transforming_profile))) return;
    const bool edit_mode = edited != nullptr;
    const bool pending_profile_edit = edit_mode && pending_profile_feature_ &&
        pending_profile_feature_->id == container_id;
    const bool profile_feature =
        feature_kind == zima::document::FeatureKind::Extrusion ||
        feature_kind == zima::document::FeatureKind::Revolution;
    std::optional<std::size_t> assembly_cut_index;
    if (edit_mode && assembly_cut) {
        const auto& cuts = assembly->session.document().cuts;
        const auto found = std::find_if(cuts.begin(), cuts.end(), [&](const auto& cut) {
            return cut.definition.id == container_id;
        });
        if (found == cuts.end() || found->input_component_bodies.empty()) {
            QMessageBox::warning(this, tr("Chybí vypočtený vstup"),
                tr("Řez nelze editovat bez uložené geometrie jeho vstupu. "
                   "Nejprve explicitně regenerujte sestavu."));
            return;
        }
        assembly_cut_index = static_cast<std::size_t>(
            std::distance(cuts.begin(), found));
    }
    std::optional<std::string> rollback_occurrence;
    const auto rollback_boundary = edit_mode && !assembly_cut
        ? part->session.rollback_boundary(container_id)
        : std::optional<zima::document::HistoryRollbackBoundary>{};
    if (edit_mode && !assembly_cut) {
        if (!rollback_boundary) {
            QMessageBox::warning(this, tr("Chybí vypočtený vstup"),
                tr("Prvek nelze editovat bez uložené geometrie jeho vstupu. "
                   "Nejprve explicitně regenerujte Part."));
            return;
        }
        rollback_occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!rollback_occurrence) {
            QMessageBox::warning(
                this, tr("Nejednoznačný výskyt"),
                tr("Nejprve aktivujte přesný výskyt Partu ve stromu sestavy."));
            return;
        }
    }
    if (!edit_mode && (feature_kind == zima::document::FeatureKind::Fillet ||
                       feature_kind == zima::document::FeatureKind::Chamfer ||
                       feature_kind == zima::document::FeatureKind::Shell ||
                       feature_kind == zima::document::FeatureKind::ImportedStep)) return;
    auto initial = (resuming_assembly_profile || pending_profile_edit)
        ? *pending_profile_feature_
        : edit_mode ? *edited
        : feature_kind == zima::document::FeatureKind::Cylinder
            ? zima::document::PartDocument::create_cylinder_container()
        : feature_kind == zima::document::FeatureKind::Hole
            ? zima::document::PartDocument::create_hole_container()
        : feature_kind == zima::document::FeatureKind::Thread
            ? zima::document::PartDocument::create_thread_container()
        : feature_kind == zima::document::FeatureKind::DrillPoint
            ? zima::document::PartDocument::create_drill_point_container()
        : feature_kind == zima::document::FeatureKind::Sphere
            ? zima::document::PartDocument::create_sphere_container()
        : feature_kind == zima::document::FeatureKind::Cone
            ? zima::document::PartDocument::create_cone_container()
        : feature_kind == zima::document::FeatureKind::Pyramid
            ? zima::document::PartDocument::create_pyramid_container()
        : feature_kind == zima::document::FeatureKind::Wedge
            ? zima::document::PartDocument::create_wedge_container()
        : feature_kind == zima::document::FeatureKind::Extrusion
            ? zima::document::PartDocument::create_extrusion_container(source_sketch_id)
        : feature_kind == zima::document::FeatureKind::Revolution
            ? zima::document::PartDocument::create_revolution_container(source_sketch_id)
            : zima::document::PartDocument::create_box_container();
    if (feature_kind == zima::document::FeatureKind::DrillPoint && part != nullptr) {
        const zima::kernel::BodyResult* input_body = nullptr;
        if (rollback_boundary && rollback_boundary->input_body) {
            input_body = &*rollback_boundary->input_body;
        } else if (!part->session.calculated_boundaries().empty()) {
            input_body = &part->session.calculated_boundaries().back();
        }
        if (input_body != nullptr) {
            const auto& available =
                input_body->mesh.original_references.triangle_references;
            std::erase_if(initial.drill_point.bottom_faces,
                [&](const auto& face) {
                    return std::ranges::none_of(available,
                        [&](const auto& candidate) {
                            return candidate.owner_id == face.owner_id &&
                                candidate.semantic_key == face.semantic_key;
                        });
                });
        }
    }
    if (property_owned_sketch_draft_) {
        property_owned_sketch_draft_->owner_container_id = initial.id;
        // The transient owned Sketch and its profile feature must share one
        // identity before Sketcher is ever opened.  Relying on the later
        // Sketcher commit to repair this association made the first
        // Properties preview unable to find its plane; offset plane and
        // dimensions then appeared only after returning from Sketcher.
        if (feature_kind == zima::document::FeatureKind::Extrusion) {
            property_owned_sketch_draft_->id = initial.extrusion.sketch_id;
        } else if (feature_kind == zima::document::FeatureKind::Revolution) {
            property_owned_sketch_draft_->id = initial.revolution.sketch_id;
        }
    }
    if (!edit_mode && (feature_kind == zima::document::FeatureKind::Extrusion ||
                       feature_kind == zima::document::FeatureKind::Revolution)) {
        const auto source = zima::document::ProfileSource::Internal;
        if (feature_kind == zima::document::FeatureKind::Extrusion) {
            initial.extrusion.profile_source = source;
        } else {
            initial.revolution.profile_source = source;
        }
    }
    if (part != nullptr &&
        (feature_kind == zima::document::FeatureKind::Extrusion ||
         feature_kind == zima::document::FeatureKind::Revolution)) {
        const auto profile_source = feature_kind ==
                zima::document::FeatureKind::Extrusion
            ? initial.extrusion.profile_source : initial.revolution.profile_source;
        const auto& sketch_id = feature_kind ==
                zima::document::FeatureKind::Extrusion
            ? initial.extrusion.sketch_id : initial.revolution.sketch_id;
        const auto source = std::find_if(part->session.document().sketches.begin(),
            part->session.document().sketches.end(), [&](const auto& sketch) {
                return sketch.id == sketch_id;
            });
        if (profile_source == zima::document::ProfileSource::Internal &&
            source != part->session.document().sketches.end()) {
            if (feature_kind == zima::document::FeatureKind::Extrusion) {
                initial.extrusion.profile_plane_offset = source->plane_offset;
            } else {
                initial.revolution.profile_plane_offset = source->plane_offset;
                const auto axis_count = std::count_if(
                    source->segments.begin(), source->segments.end(),
                    [](const auto& segment) {
                        return segment.construction && segment.centerline;
                    });
                if (axis_count == 1) {
                    initial.revolution.axis_segment_id =
                        revolution_axis_segment_id(
                            *source, initial.revolution.axis_segment_id);
                }
            }
        }
    }
    // Returning from the embedded editor resumes the same property draft.
    // Reading the saved feature here would discard pending references,
    // FRONT/BACK, quarter turns and extent parameters.
    if (property_owned_feature_draft_ &&
        property_owned_feature_draft_->id == initial.id) {
        initial = *property_owned_feature_draft_;
    }
    if (feature_kind == zima::document::FeatureKind::Revolution &&
        property_owned_sketch_draft_ &&
        std::count_if(property_owned_sketch_draft_->segments.begin(),
            property_owned_sketch_draft_->segments.end(), [](const auto& segment) {
                return segment.construction && segment.centerline;
            }) == 1) {
        initial.revolution.axis_segment_id = revolution_axis_segment_id(
            *property_owned_sketch_draft_, initial.revolution.axis_segment_id);
    }
    if (profile_feature && !property_owned_sketch_draft_) {
        const auto internal = feature_kind == zima::document::FeatureKind::Extrusion
            ? initial.extrusion.profile_source : initial.revolution.profile_source;
        const auto& id = feature_kind == zima::document::FeatureKind::Extrusion
            ? initial.extrusion.sketch_id : initial.revolution.sketch_id;
        const auto& sketches = assembly_cut ? assembly->session.document().sketches
                                            : part->session.document().sketches;
        const auto source = std::ranges::find(sketches, id, &zima::sketcher::Sketch::id);
        if (internal == zima::document::ProfileSource::Internal && source != sketches.end())
            property_owned_sketch_draft_ = *source;
    }
    const bool allow_subtract = assembly_cut || (!part->session.document().history.empty() &&
        !(edit_mode && part->session.document().history.front().id == initial.id));
    const std::string owner_id = assembly_cut
        ? assembly->session.document().document_id
        : part->session.document().document_id;
    std::vector<PrimitivePropertiesDialog::AssemblyTarget> assembly_targets;
    std::vector<std::string> selected_targets;
    if (assembly_cut) {
        for (const auto& component : assembly->session.document().components) {
            if (component.suppressed || component.derived_copy || component.source_kind !=
                    zima::assembly::ComponentSourceKind::Part) continue;
            assembly_targets.emplace_back(component.occurrence_id, component.name);
            if (!edit_mode) selected_targets.push_back(component.occurrence_id);
        }
        if (edited_cut != nullptr) selected_targets = edited_cut->target_occurrence_ids;
    }
    auto* dialog = new PrimitivePropertiesDialog(
        initial, edit_mode, allow_subtract,
        [this, owner_id, edit_mode, assembly_cut, container_id,
         pending_profile_edit](
            zima::document::HistoryContainer committed,
            std::vector<std::string> target_occurrences) mutable {
            if (committed.feature_kind ==
                    zima::document::FeatureKind::Extrusion ||
                committed.feature_kind ==
                    zima::document::FeatureKind::Revolution ||
                committed.feature_kind ==
                    zima::document::FeatureKind::Hole ||
                committed.feature_kind ==
                    zima::document::FeatureKind::Thread) {
                normalize_owned_profile_front_references(
                    committed.placement.references);
            }
            if (assembly_cut) {
                workspace_.regenerate_assembly_from_open_dependencies(owner_id);
                auto* target = workspace_.open_assembly(owner_id);
                if (target == nullptr) throw std::runtime_error(
                    "Assembly is no longer open");
                auto next = target->session.document();
                if (property_owned_sketch_draft_) {
                    auto owned = *property_owned_sketch_draft_;
                    owned.owner_container_id = committed.id;
                    owned.plane_offset = committed.feature_kind ==
                            zima::document::FeatureKind::Extrusion
                        ? committed.extrusion.profile_plane_offset
                        : committed.revolution.profile_plane_offset;
                    const auto existing = std::find_if(
                        next.sketches.begin(), next.sketches.end(),
                        [&](const auto& sketch) {
                            return sketch.id == owned.id;
                        });
                    if (existing == next.sketches.end()) {
                        next.sketches.push_back(std::move(owned));
                    } else {
                        *existing = std::move(owned);
                    }
                }
                if (committed.feature_kind ==
                        zima::document::FeatureKind::Revolution) {
                    const auto axis_sketch = std::find_if(
                        next.sketches.begin(), next.sketches.end(),
                        [&](const auto& sketch) {
                            return sketch.id == committed.revolution.sketch_id;
                        });
                    if (axis_sketch == next.sketches.end()) {
                        throw std::runtime_error("Skica rotace nebyla nalezena.");
                    }
                    committed.revolution.axis_segment_id =
                        revolution_axis_segment_id(*axis_sketch,
                            committed.revolution.axis_segment_id);
                }
                zima::assembly::AssemblyCut cut{
                    std::move(committed), std::move(target_occurrences)};
                const auto committed_cut_id = cut.definition.id;
                if (edit_mode) {
                    auto* existing = next.find_cut(cut.definition.id);
                    if (existing == nullptr) throw std::runtime_error(
                        "Assembly cut no longer exists");
                    *existing = std::move(cut);
                } else {
                    next.cuts.push_back(std::move(cut));
                }
                calculate_assembly_cuts(next);
                target->session.commit(std::move(next));
                if (pending_profile_feature_ &&
                    pending_profile_feature_->id == committed_cut_id) {
                    pending_profile_feature_.reset();
                }
                return;
            }
            if (zima::workspace::primitive_definition(committed.feature_kind)) {
                try {
                    static_cast<void>(zima::workspace::commit_primitive(workspace_, kernel_, owner_id,
                        std::move(committed), edit_mode ? zima::workspace::PrimitiveEditMode::Replace
                                                        : zima::workspace::PrimitiveEditMode::Create));
                } catch (const zima::workspace::PrimitiveOperationError& error) {
                    throw std::runtime_error(tr(error.what()).toStdString());
                }
                return;
            }
            auto* target_part = workspace_.open_part(owner_id);
            if (target_part == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            if (property_owned_sketch_draft_) {
                const auto found = std::ranges::find(next.sketches, property_owned_sketch_draft_->id,
                    &zima::sketcher::Sketch::id);
                if (found == next.sketches.end()) next.sketches.push_back(*property_owned_sketch_draft_);
                else *found = *property_owned_sketch_draft_;
            }
            if (committed.feature_kind ==
                    zima::document::FeatureKind::Revolution) {
                const auto axis_sketch = std::find_if(
                    next.sketches.begin(), next.sketches.end(),
                    [&](const auto& sketch) {
                        return sketch.id == committed.revolution.sketch_id;
                    });
                if (axis_sketch == next.sketches.end()) {
                    throw std::runtime_error("Skica rotace nebyla nalezena.");
                }
                committed.revolution.axis_segment_id =
                    revolution_axis_segment_id(*axis_sketch,
                        committed.revolution.axis_segment_id);
            }
            if (edit_mode) {
                auto* target = next.find_container(committed.id);
                if (target == nullptr) throw std::runtime_error("Container no longer exists");
                // OK on a profile feature is an explicit body-calculation
                // request even when its visible numeric fields are unchanged:
                // the owned Sketch geometry lives outside HistoryContainer
                // equality and may have changed from blank to a closed
                // rectangle. Skipping here left the valid cyan preview with
                // no calculated solid.
                if (*target == committed && !pending_profile_edit &&
                    committed.feature_kind !=
                        zima::document::FeatureKind::Extrusion &&
                    committed.feature_kind !=
                        zima::document::FeatureKind::Revolution) return;
                *target = std::move(committed);
            } else {
                next.insert_history_entry(
                    zima::document::PartHistoryKind::Feature, committed.id);
                next.history.push_back(std::move(committed));
            }
            const auto* committed_container = next.find_container(
                edit_mode ? container_id : next.history.back().id);
            if (committed_container != nullptr &&
                (committed_container->feature_kind ==
                        zima::document::FeatureKind::Extrusion ||
                 committed_container->feature_kind ==
                        zima::document::FeatureKind::Revolution)) {
                const bool extrusion = committed_container->feature_kind ==
                    zima::document::FeatureKind::Extrusion;
                const auto profile_source = extrusion
                    ? committed_container->extrusion.profile_source
                    : committed_container->revolution.profile_source;
                const auto& sketch_id = extrusion
                    ? committed_container->extrusion.sketch_id
                    : committed_container->revolution.sketch_id;
                if (profile_source == zima::document::ProfileSource::Internal) {
                    const auto owned = std::find_if(next.sketches.begin(),
                        next.sketches.end(), [&](const auto& sketch) {
                            return sketch.id == sketch_id;
                        });
                    if (owned == next.sketches.end()) {
                        throw std::runtime_error(
                            "Internal profile Sketch no longer exists");
                    }
                    owned->owner_container_id = committed_container->id;
                    // The first planar placement reference defines local
                    // FRONT (+Y). The owned profile must therefore use the
                    // local XZ plane, whose normal is parallel to that first
                    // reference, exactly like standalone Sketch Properties.
                    const auto first_reference = std::find_if(
                        committed_container->placement.references.begin(),
                        committed_container->placement.references.end(),
                        [](const auto& reference) {
                            return !reference.owner_id.empty();
                        });
                    if (first_reference !=
                            committed_container->placement.references.end() &&
                        first_reference->supports_offset) {
                        owned->plane = zima::sketcher::SketchPlane::XZ;
                    }
                    owned->plane_offset = extrusion
                        ? committed_container->extrusion.profile_plane_offset
                        : committed_container->revolution.profile_plane_offset;
                }
            }
            // Universal container placement: resolve any HistoryContainer
            // placement references against the geometry that existed before
            // this edit, exactly like ConstructionObject editing does, before
            // the kernel evaluates the (possibly placement-dependent) history.
            const auto& calculated_before =
                target_part->session.calculated_boundaries();
            auto reference_geometry =
                construction_reference_source_geometry(calculated_before);
            append_reference_geometry(reference_geometry,
                next.origin_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                next.construction_viewer_mesh().original_references);
            // Resolving Body histories replaces the working document and
            // invalidates pointers into its former feature vector.
            const bool completes_pending_profile = pending_profile_feature_ && committed_container &&
                pending_profile_feature_->id == committed_container->id;
            next.resolve_constructions(reference_geometry);
            auto calculated = calculate_part(next, &calculated_before);
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            target_part->session.commit(std::move(next), std::move(calculated));
            if (completes_pending_profile) {
                pending_profile_feature_.reset();
            }
        }, this, std::move(assembly_targets), std::move(selected_targets),
        assembly_cut);
    primitive_parameter_owner_id_ = initial.id;
    // Extrusion/Revolution OK always means validate + calculate. Their owned
    // Sketch is stored separately from the parameter object, so numeric
    // equality cannot prove that the operation is a no-op.
    dialog->set_commit_required(pending_profile_edit || profile_feature);
    std::function<zima::document::HistoryContainer(
        const zima::document::HistoryContainer&)> placement_preview;
    const auto prepare_owned_profile_preview = [this](
            zima::document::PartDocument& preview_document,
            const zima::document::HistoryContainer& preview) {
        const bool extrusion = preview.feature_kind ==
            zima::document::FeatureKind::Extrusion;
        const auto profile_source = extrusion ? preview.extrusion.profile_source
                                              : preview.revolution.profile_source;
        if (profile_source != zima::document::ProfileSource::Internal) return;
        const auto& sketch_id = extrusion ? preview.extrusion.sketch_id
                                          : preview.revolution.sketch_id;
        auto sketch = std::find_if(preview_document.sketches.begin(),
            preview_document.sketches.end(), [&](const auto& value) {
                return value.id == sketch_id;
            });
        if (sketch == preview_document.sketches.end()) {
            // Before the first planar placement reference exists there is no
            // user-defined profile plane to present. Creating a default
            // Sketch here used to paint an unrelated XY plane in world space
            // immediately after starting Extrusion/Revolution.
            const bool has_planar_reference = std::any_of(
                preview.placement.references.begin(),
                preview.placement.references.end(), [](const auto& reference) {
                    return !reference.orientation_only &&
                        reference.supports_offset &&
                        !reference.owner_id.empty();
                });
            if (!has_planar_reference) return;
            // Once the support plane is known, the offset plane and operation
            // dimensions must already work before entering Sketcher even if
            // the empty owned Sketch has not yet been inserted into this
            // transient document.
            auto empty_profile = zima::sketcher::Sketch::create_default();
            empty_profile.id = sketch_id;
            empty_profile.owner_container_id = preview.id;
            preview_document.sketches.push_back(std::move(empty_profile));
            sketch = std::prev(preview_document.sketches.end());
        }
        sketch->owner_container_id = preview.id;
        const auto first_reference = std::find_if(
            preview.placement.references.begin(),
            preview.placement.references.end(), [](const auto& reference) {
                return !reference.owner_id.empty();
            });
        if (first_reference != preview.placement.references.end() &&
            first_reference->supports_offset) {
            sketch->plane = zima::sketcher::SketchPlane::XZ;
        }
        const double next_offset = extrusion
            ? preview.extrusion.profile_plane_offset
            : preview.revolution.profile_plane_offset;
        const double offset_delta = next_offset - sketch->plane_offset;
        sketch->resolved_origin.x += sketch->resolved_normal.x * offset_delta;
        sketch->resolved_origin.y += sketch->resolved_normal.y * offset_delta;
        sketch->resolved_origin.z += sketch->resolved_normal.z * offset_delta;
        sketch->plane_offset = next_offset;
        const auto owner = std::find_if(preview_document.history.begin(),
            preview_document.history.end(), [&](const auto& value) {
                return value.id == preview.id;
            });
        if (owner == preview_document.history.end()) {
            // Body-local resolution visits registered history entries only.
            // Register a new preview in the transient document as well.
            preview_document.insert_history_entry(
                zima::document::PartHistoryKind::Feature, preview.id);
            preview_document.history.push_back(preview);
        } else {
            *owner = preview;
        }
        preview_document.resolve_constructions(primitive_reference_geometry_);
        // The document resolver is the sole authority for the owned work
        // plane. Reconstructing this frame again from the container's Euler
        // fields made referenced/quarter-rotated profiles consume a second,
        // different interpretation of the same placement and shifted the
        // preview away from the calculated solid.
    };
    const auto update_owned_profile_context_preview = [this](
            zima::document::PartDocument& preview_document,
            const zima::document::HistoryContainer& preview) {
        const bool extrusion = preview.feature_kind ==
            zima::document::FeatureKind::Extrusion;
        const auto sketch_id = extrusion ? preview.extrusion.sketch_id
                                          : preview.revolution.sketch_id;
        auto sketch = std::find_if(preview_document.sketches.begin(),
            preview_document.sketches.end(), [&](const auto& value) {
                return value.id == sketch_id;
            });
        if (sketch == preview_document.sketches.end()) return;
        auto plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        plane.id = preview.id;
        plane.entity_id = preview.feature_id;
        plane.entity_parent_id = preview.id;
        plane.container_origin = preview.container_origin;
        plane.name = tr("Rovina").toStdString();
        plane.origin = {preview.placement.x, preview.placement.y,
                        preview.placement.z};
        plane.rotation = {preview.placement.rotation_x,
                          preview.placement.rotation_y,
                          preview.placement.rotation_z};
        plane.absolute_rotation = {preview.placement.absolute_rotation_x,
                                   preview.placement.absolute_rotation_y,
                                   preview.placement.absolute_rotation_z};
        plane.orientation_back = preview.placement.orientation_back;
        plane.orientation_quarter_turns =
            preview.placement.orientation_quarter_turns;
        plane.base_plane = sketch->plane == zima::sketcher::SketchPlane::XY
            ? zima::document::LocalDatumPlane::XY
            : sketch->plane == zima::sketcher::SketchPlane::XZ
                ? zima::document::LocalDatumPlane::XZ
                : zima::document::LocalDatumPlane::YZ;
        plane.offset = sketch->plane_offset;
        plane.reference_valid = true;
        preview_document.constructions.push_back(plane);
        preview_document.resolve_constructions(primitive_reference_geometry_);
        // Body resolution replaces the document, including the Sketch vector.
        sketch = std::find_if(preview_document.sketches.begin(), preview_document.sketches.end(),
            [&](const auto& value) { return value.id == sketch_id; });
        if (sketch == preview_document.sketches.end()) return;
        if (property_owned_sketch_draft_ &&
            property_owned_sketch_draft_->id == sketch_id) {
            // Properties annotations and embedded Sketcher consume the draft.
            // Keep its frame in sync with the same resolved profile used by
            // the wire preview, while preserving pending local 2D geometry.
            auto& draft = *property_owned_sketch_draft_;
            draft.plane = sketch->plane;
            draft.plane_offset = sketch->plane_offset;
            draft.resolved_origin = sketch->resolved_origin;
            draft.resolved_x_axis = sketch->resolved_x_axis;
            draft.resolved_y_axis = sketch->resolved_y_axis;
            draft.resolved_normal = sketch->resolved_normal;
        }
        // Extrusion/Revolution use the owned Sketch's resolved work plane,
        // which may differ from the generic container frame (notably for a
        // manually entered triad of built-in Origin planes).  The cyan
        // offset-plane preview must be built from that final Sketch frame,
        // otherwise only the highlighted source FRONT plane is visible and
        // FRONT/BACK, quarter-turn and profile offset appear to do nothing.
        auto& resolved_plane = preview_document.constructions.back();
        const zima::kernel::Vec3 local_z{
            -sketch->resolved_y_axis.x,
            -sketch->resolved_y_axis.y,
            -sketch->resolved_y_axis.z};
        const auto resolved_rotation = euler_degrees_from_frame_columns(
            sketch->resolved_x_axis, sketch->resolved_normal, local_z);
        resolved_plane.base_plane = zima::document::LocalDatumPlane::XZ;
        resolved_plane.rotation = resolved_rotation;
        resolved_plane.absolute_rotation = resolved_rotation;
        resolved_plane.direction = sketch->resolved_normal;
        resolved_plane.entity_origin = sketch->resolved_origin;
        resolved_plane.origin = {
            sketch->resolved_origin.x -
                sketch->resolved_normal.x * sketch->plane_offset,
            sketch->resolved_origin.y -
                sketch->resolved_normal.y * sketch->plane_offset,
            sketch->resolved_origin.z -
                sketch->resolved_normal.z * sketch->plane_offset};
        // This Plane is a display carrier derived from the owned Sketch, not
        // an independently constrained model object. resolve_constructions()
        // therefore (correctly for persisted constructions) marks its empty
        // reference set unresolved. Restore validity only for this transient
        // carrier so construction_viewer_mesh() emits the actual offset plane
        // instead of stopping after the editing Origin frame.
        resolved_plane.reference_valid = true;
        primitive_origin_preview_mesh_ =
            preview_document.construction_viewer_mesh(plane.id);
        // The temporary Plane publishes its own generic offset dimension.
        // Feature Properties rebuild the operation dimensions below from the
        // feature policy; retaining both sources painted the same value twice.
        primitive_origin_preview_mesh_->dimensions.clear();
        const double profile_offset = extrusion
            ? preview.extrusion.profile_plane_offset
            : preview.revolution.profile_plane_offset;
        const auto start = sketch->resolved_origin;
        const auto normal = sketch->resolved_normal;
        const auto base = zima::kernel::Vec3{
            start.x - normal.x * profile_offset,
            start.y - normal.y * profile_offset,
            start.z - normal.z * profile_offset};
        const auto front = sketch->resolved_x_axis;
        constexpr double witness_length = 8.0;
        const auto append_dimension = [&](const zima::kernel::Vec3& first,
                                           const zima::kernel::Vec3& second,
                                           double value,
                                           std::string key,
                                           double witness_sign) {
            const zima::kernel::Vec3 witness{
                front.x * witness_length * witness_sign,
                front.y * witness_length * witness_sign,
                front.z * witness_length * witness_sign};
            zima::kernel::ViewerDimension dimension{
                first, second,
                {first.x + witness.x, first.y + witness.y,
                 first.z + witness.z},
                {second.x + witness.x, second.y + witness.y,
                 second.z + witness.z},
                value, {preview.id, std::move(key), {}}, {}};
            const zima::kernel::Vec3 direction{
                second.x - first.x, second.y - first.y,
                second.z - first.z};
            dimension.plane_normal = {
                direction.y * front.z - direction.z * front.y,
                direction.z * front.x - direction.x * front.z,
                direction.x * front.y - direction.y * front.x};
            primitive_origin_preview_mesh_->dimensions.push_back(
                std::move(dimension));
        };
        const bool display_profile_offset = extrusion
            ? extrusion_profile_offset_dimension_value(
                  preview.extrusion).has_value()
            : std::abs(profile_offset) > 1.0e-12;
        if (display_profile_offset) {
            append_dimension(base, start, profile_offset,
                "parameter:profile_offset", 1.0);
        }
        if (extrusion) {
            zima::kernel::Vec3 direction = normal;
            if (preview.extrusion.direction ==
                    zima::document::ExtrusionDirection::Reverse) {
                direction = {-direction.x, -direction.y, -direction.z};
            }
            const auto along = [&](double distance) {
                return zima::kernel::Vec3{
                    start.x + direction.x * distance,
                    start.y + direction.y * distance,
                    start.z + direction.z * distance};
            };
            if (const auto length = extrusion_length_dimension_value(
                    preview.extrusion, false)) {
                append_dimension(start,
                    along(*length), *length,
                    "parameter:length_forward", -1.0);
            }
            if (const auto length = extrusion_length_dimension_value(
                    preview.extrusion, true)) {
                append_dimension(start,
                    along(-*length), *length,
                    preview.extrusion.extent_mode ==
                            zima::document::ProfileExtentMode::Symmetric
                        ? "parameter:length_forward"
                        : "parameter:length_reverse", -1.0);
            }
        }
        viewer_->set_feature_preview_owners({plane.entity_id});
    };
    struct ExtentDragBaseline {
        double offset{};
        double length{};
        double reverse_length{};
        bool reversed{};
        zima::document::ProfileExtentMode extent_mode{
            zima::document::ProfileExtentMode::OneSide};
        zima::sketcher::SketchPlane profile_plane{
            zima::sketcher::SketchPlane::XY};
    };
    auto extent_drag_baseline = std::make_shared<ExtentDragBaseline>();
    const auto publish_extrusion_extent = [this, extent_drag_baseline](
            const zima::document::PartDocument& preview_document,
            const zima::document::HistoryContainer& preview) {
        const auto sketch = std::find_if(preview_document.sketches.begin(),
            preview_document.sketches.end(), [&](const auto& value) {
                return value.id == preview.extrusion.sketch_id;
            });
        if (sketch == preview_document.sketches.end()) {
            viewer_->set_extent_manipulator(std::nullopt);
            return;
        }
        extent_drag_baseline->profile_plane = sketch->plane;
        auto direction = sketch->resolved_normal;
        if (preview.extrusion.direction ==
                zima::document::ExtrusionDirection::Reverse) {
            direction = {-direction.x, -direction.y, -direction.z};
        }
        const bool editable_length = extrusion_length_dimension_value(
            preview.extrusion, false).has_value();
        const bool second_side = preview.extrusion.extent_mode !=
            zima::document::ProfileExtentMode::OneSide;
        const bool editable_reverse = extrusion_length_dimension_value(
            preview.extrusion, true).has_value();
        std::vector<zima::viewer::ExtentManipulator> manipulators;
        // Up-to and Through-all have no editable numeric length.  They show
        // only the direction arrow; a fixed endpoint would look like a
        // draggable length handle and would compete with the profile-offset
        // point at the same origin.
        if (editable_length) {
            manipulators.push_back({"profile_start", "length_forward",
                sketch->resolved_origin, direction,
                preview.extrusion.length_forward, false});
        }
        if (second_side) {
            const zima::kernel::Vec3 reverse_direction{
                -direction.x, -direction.y, -direction.z};
            if (editable_reverse) {
                manipulators.push_back({"profile_start",
                    preview.extrusion.extent_mode ==
                            zima::document::ProfileExtentMode::Symmetric
                        ? "length_forward" : "length_reverse",
                    sketch->resolved_origin, reverse_direction,
                    preview.extrusion.extent_mode ==
                            zima::document::ProfileExtentMode::Symmetric
                        ? preview.extrusion.length_forward
                        : preview.extrusion.length_reverse,
                    false});
            }
        }
        viewer_->set_extent_manipulators(std::move(manipulators));
        std::vector<zima::viewer::OperationDirectionIndicator> indicators;
        if (!editable_length) indicators.push_back(
            {sketch->resolved_origin, direction, {}, 0.0, false});
        if (second_side && !editable_reverse) indicators.push_back(
            {sketch->resolved_origin,
             {-direction.x, -direction.y, -direction.z}, {}, 0.0, false});
        viewer_->set_operation_direction_indicators(std::move(indicators));
    };
    const auto publish_revolution_direction = [this](
            const zima::document::PartDocument& preview_document,
            const zima::document::HistoryContainer& preview) {
        const auto sketch = std::find_if(preview_document.sketches.begin(),
            preview_document.sketches.end(), [&](const auto& value) {
                return value.id == preview.revolution.sketch_id;
            });
        if (sketch == preview_document.sketches.end()) {
            viewer_->set_operation_direction_indicator(std::nullopt);
            return;
        }
        auto frame = revolution_cue_frame(
            *sketch, preview.revolution.axis_segment_id);
        if (!frame) {
            viewer_->set_operation_direction_indicator(std::nullopt);
            viewer_->set_extent_manipulator(std::nullopt);
            return;
        }
        auto axis = frame->axis;
        if (preview.revolution.direction ==
                zima::document::ExtrusionDirection::Reverse) {
            axis = {-axis.x, -axis.y, -axis.z};
        }
        const auto radial = frame->radial;
        const zima::kernel::Vec3 tangent{
            axis.y * radial.z - axis.z * radial.y,
            axis.z * radial.x - axis.x * radial.z,
            axis.x * radial.y - axis.y * radial.x};
        std::vector<zima::viewer::OperationDirectionIndicator> indicators;
        std::vector<zima::viewer::ExtentManipulator> handles;
        const auto append_side = [&](double degrees, const char* key, double sign) {
            const double signed_degrees = degrees * sign;
            indicators.push_back({frame->center, axis, radial,
                signed_degrees, true});
            const double radians = signed_degrees *
                std::numbers::pi / 180.0;
            const zima::kernel::Vec3 endpoint{
                frame->center.x + radial.x * std::cos(radians) +
                    tangent.x * std::sin(radians),
                frame->center.y + radial.y * std::cos(radians) +
                    tangent.y * std::sin(radians),
                frame->center.z + radial.z * std::cos(radians) +
                    tangent.z * std::sin(radians)};
            zima::kernel::Vec3 endpoint_tangent{
                -radial.x * std::sin(radians) +
                    tangent.x * std::cos(radians),
                -radial.y * std::sin(radians) +
                    tangent.y * std::cos(radians),
                -radial.z * std::sin(radians) +
                    tangent.z * std::cos(radians)};
            endpoint_tangent = {endpoint_tangent.x * sign,
                endpoint_tangent.y * sign, endpoint_tangent.z * sign};
            handles.push_back({key, {}, endpoint, endpoint_tangent, 0.0, true});
        };
        append_side(preview.revolution.angle_degrees,
            "revolution_angle_forward", 1.0);
        if (preview.revolution.extent_mode ==
                zima::document::ProfileExtentMode::TwoSides) {
            append_side(preview.revolution.angle_reverse,
                "revolution_angle_reverse", -1.0);
        } else if (preview.revolution.extent_mode ==
                zima::document::ProfileExtentMode::Symmetric) {
            append_side(preview.revolution.angle_degrees,
                "revolution_angle_symmetric", -1.0);
        }
        viewer_->set_operation_direction_indicators(std::move(indicators));
        viewer_->set_extent_manipulators(std::move(handles));
    };
    if (supports_placement_reference_picking(feature_kind)) {
        // Universal container placement: Part features and Assembly cuts use
        // the same persisted reference rows and the same DOF solver.
        zima::kernel::ViewerReferenceGeometry reference_geometry;
        if (part != nullptr) {
            const auto& calculated = part->session.calculated_boundaries();
            reference_geometry = construction_reference_source_geometry(calculated);
            const auto& document = part->session.document();
            append_reference_geometry(reference_geometry,
                document.origin_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                document.construction_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                document.history_origin_reference_geometry_before(initial.id));
            std::set<std::string> preceding_container_ids;
            for (const auto& container : document.history) {
                if (container.id == initial.id) break;
                preceding_container_ids.insert(container.container_origin.id);
            }
            append_reference_geometry(reference_geometry,
                local_container_context_mesh(document, preceding_container_ids,
                    false, true).original_references);
            const auto* body = document.body_owner_for_object(initial.id);
            const auto body_id = body ? body->scope.id : document.body_history.active_body_id();
            if (!body_id.empty()) reference_geometry = document.construction_reference_geometry_for(
                body_id, std::move(reference_geometry));
        } else {
            const auto& document = assembly->session.document();
            reference_geometry = document.build_scene().original_references;
            append_reference_geometry(reference_geometry,
                document.origin_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                document.construction_viewer_mesh().original_references);
        }
        primitive_reference_geometry_ = reference_geometry;
        dialog->set_reference_request_callback(
            [this](std::size_t index) { start_primitive_reference_selection(index); });
        dialog->set_reference_highlights_changed_callback([this, dialog] {
            viewer_->set_constraint_reference_highlights(
                {}, highlighted_reference_edge_keys(*dialog));
        });
        primitive_reference_dialog_ = dialog;
        // Opening edits (including View dimensions) keep the user's camera.
        const bool fit_new_basic_preview = !edit_mode &&
            feature_kind != zima::document::FeatureKind::Thread &&
            feature_kind != zima::document::FeatureKind::Extrusion &&
            feature_kind != zima::document::FeatureKind::Revolution &&
            feature_kind != zima::document::FeatureKind::Fillet &&
            feature_kind != zima::document::FeatureKind::Chamfer;
        const bool defer_profile_scene_refresh =
            feature_kind == zima::document::FeatureKind::Extrusion ||
            feature_kind == zima::document::FeatureKind::Revolution;
        placement_preview = [this, fit_new_basic_preview,
                             defer_profile_scene_refresh](
                const zima::document::HistoryContainer& preview)
                -> zima::document::HistoryContainer {
            if (primitive_reference_dialog_ == nullptr) return preview;
            auto resolved_preview = preview;
            auto& placement = resolved_preview.placement;
            // Creation and later Properties editing must resolve the same
            // reference set. OK normalizes a Thread's Axis + Plane rows
            // before committing; doing that only at commit time left the
            // first cyan preview on its stale numerical origin (typically
            // Z=0), while reopening Properties immediately looked correct.
            if (resolved_preview.feature_kind ==
                    zima::document::FeatureKind::Thread) {
                normalize_owned_profile_front_references(
                    placement.references);
            }
            if (defer_profile_scene_refresh) {
                normalize_owned_profile_front_references(
                    placement.references,
                    /*preserve_front_through_origin_triad=*/true);
            }
            zima::kernel::Vec3 base_rotation;
            bool orientation_from_reference = false;
            const bool placement_valid = zima::document::resolve_placement(
                placement, primitive_reference_geometry_, &base_rotation,
                &orientation_from_reference);
            const auto constraint_state = zima::document::point_constraint_state(
                placement.references, primitive_reference_geometry_);
            primitive_translation_dof_ = constraint_state.remaining_dof;
            primitive_reference_dialog_->set_translation_constraint_state(
                constraint_state,
                {placement.x, placement.y, placement.z});
            primitive_reference_dialog_->set_rotation_constraint_state(
                zima::document::orientation_constraint_state(
                    placement.references, primitive_reference_geometry_, true,
                    {placement.x, placement.y, placement.z}));
            primitive_reference_dialog_->set_orientation_base_rotation(
                base_rotation, orientation_from_reference);
            primitive_reference_dialog_->set_resolved_rotation(
                {placement.rotation_x, placement.rotation_y,
                 placement.rotation_z},
                placement_valid);
            zima::document::PartDocument preview_geometry;
            std::optional<zima::kernel::ViewerAxis> opening_preview_axis;
            const zima::kernel::ViewerMesh* through_all_input = nullptr;
            if (resolved_preview.feature_kind ==
                    zima::document::FeatureKind::Hole &&
                resolved_preview.hole.bore_end_condition ==
                    zima::document::EndCondition::ThroughAll) {
                if (part_rollback_ && part_rollback_->input_body) {
                    through_all_input = &part_rollback_->input_body->mesh;
                } else if (const auto* active_part = workspace_.open_part(
                               workspace_.active_document_id());
                           active_part != nullptr &&
                           !active_part->session.calculated_boundaries().empty()) {
                    through_all_input =
                        &active_part->session.calculated_boundaries().back().mesh;
                }
            }
            if (resolved_preview.feature_kind ==
                    zima::document::FeatureKind::Thread) {
                const zima::kernel::ViewerMesh* body = nullptr;
                if (part_rollback_ && part_rollback_->input_body) {
                    body = &part_rollback_->input_body->mesh;
                } else if (const auto* active_part = workspace_.open_part(
                        workspace_.active_document_id()); active_part != nullptr &&
                    !active_part->session.calculated_boundaries().empty()) {
                    body = &active_part->session.calculated_boundaries().back().mesh;
                }
                viewer_->set_transient_edges(
                    preview_geometry.thread_edges(resolved_preview, body, &opening_preview_axis));
            } else {
                viewer_->set_transient_edges(through_all_input != nullptr
                    ? preview_geometry.primitive_preview_edges(
                          resolved_preview, *through_all_input)
                    : preview_geometry.primitive_preview_edges(resolved_preview));
            }
            // Basic solids are only an analytical cyan wire until OK. Their
            // local Container Origin is nevertheless real editing context:
            // standard-colour axes/planes plus a cyan origin point, all
            // resolved from the same live placement as the wire preview.
            zima::document::ConstructionObject origin_preview;
            origin_preview.id = resolved_preview.id;
            origin_preview.entity_id = resolved_preview.feature_id;
            origin_preview.container_origin = resolved_preview.container_origin;
            origin_preview.kind = defer_profile_scene_refresh
                ? zima::document::ConstructionKind::Point
                : zima::document::ConstructionKind::Axis;
            origin_preview.origin = {placement.x, placement.y, placement.z};
            origin_preview.rotation = {placement.rotation_x,
                placement.rotation_y, placement.rotation_z};
            origin_preview.reference_valid = false;
            preview_geometry.constructions.push_back(std::move(origin_preview));
            primitive_origin_preview_mesh_ =
                preview_geometry.construction_viewer_mesh(resolved_preview.id);
            if (resolved_preview.feature_kind == zima::document::FeatureKind::Thread) {
                const auto placeholder_axis = [&](const auto& axis) {
                    return axis.reference.owner_id == resolved_preview.feature_id &&
                        axis.reference.semantic_key == "axis";
                };
                std::erase_if(primitive_origin_preview_mesh_->axes, placeholder_axis);
                std::erase_if(primitive_origin_preview_mesh_->original_references.axes,
                    placeholder_axis);
                if (opening_preview_axis)
                    primitive_origin_preview_mesh_->axes.push_back(*opening_preview_axis);
            }
            parameter_dimension_preview_ = resolved_preview;
            construction_dimension_object_id_ = resolved_preview.id;
            viewer_->set_feature_preview_owners({
                resolved_preview.feature_id,
                resolved_preview.container_origin.id});
            // Extrusion/Revolution continue by rebuilding their owned Sketch
            // plane, operation dimensions, manipulator and wire. Publishing
            // this placement-only intermediate state made the dimensions
            // disappear for one frame on every drag update. Their outer
            // preview callback performs the single atomic scene refresh once
            // all of those pieces are ready.
            if (!defer_profile_scene_refresh) {
                preserve_view_on_refresh_ = true;
                refresh_scene();
                if (!pending_primitive_reference_index_ &&
                    extrusion_target_dialog_ == nullptr) {
                    set_primitive_properties_dimension_selection();
                }
                if (fit_new_basic_preview) viewer_->fit_all();
            }
            return resolved_preview;
        };
        // Box/Cylinder/.../Wedge have no other preview needs: install the
        // placement-only preview directly. Extrusion/Revolution below merge
        // this with their own transient-edge preview into one callback.
        if (feature_kind != zima::document::FeatureKind::Extrusion &&
            feature_kind != zima::document::FeatureKind::Revolution) {
            dialog->set_preview_callback(placement_preview);
        }
    }
    const auto profile_scene_signature =
        std::make_shared<std::optional<std::vector<double>>>();
    const auto publish_profile_preview_scene =
        [this, profile_scene_signature](
            const zima::document::PartDocument* preview_document,
            const zima::document::HistoryContainer& preview) {
        std::vector<zima::kernel::ViewerDimension> live_dimensions;
        if (primitive_origin_preview_mesh_) {
            live_dimensions =
                std::move(primitive_origin_preview_mesh_->dimensions);
            primitive_origin_preview_mesh_->dimensions.clear();
        }
        viewer_->set_transient_dimensions(std::move(live_dimensions));

        const auto& placement = preview.placement;
        std::vector<double> signature{
            placement.x, placement.y, placement.z,
            placement.rotation_x, placement.rotation_y, placement.rotation_z,
            placement.absolute_rotation_x, placement.absolute_rotation_y,
            placement.absolute_rotation_z,
            placement.orientation_back ? 1.0 : 0.0,
            static_cast<double>(placement.orientation_quarter_turns)};
        if (preview.feature_kind ==
                zima::document::FeatureKind::Extrusion) {
            signature.push_back(preview.extrusion.profile_plane_offset);
            signature.push_back(extrusion_length_dimension_value(
                preview.extrusion, false).has_value() ? 1.0 : 0.0);
            signature.push_back(extrusion_length_dimension_value(
                preview.extrusion, true).has_value() ? 1.0 : 0.0);
        } else {
            signature.push_back(preview.revolution.profile_plane_offset);
        }
        if (preview_document != nullptr) {
            const auto& sketch_id = preview.feature_kind ==
                    zima::document::FeatureKind::Extrusion
                ? preview.extrusion.sketch_id : preview.revolution.sketch_id;
            const auto sketch = std::find_if(
                preview_document->sketches.begin(),
                preview_document->sketches.end(), [&](const auto& value) {
                    return value.id == sketch_id;
                });
            if (sketch != preview_document->sketches.end()) {
                for (const auto& value : {
                        sketch->resolved_origin, sketch->resolved_normal,
                        sketch->resolved_x_axis, sketch->resolved_y_axis}) {
                    signature.insert(signature.end(),
                        {value.x, value.y, value.z});
                }
            }
        }
        if (*profile_scene_signature &&
            **profile_scene_signature == signature) {
            return;
        }
        *profile_scene_signature = std::move(signature);
        // The base body and static work-plane frame change only when the
        // placement/profile frame changes. Length/angle edits update the
        // transient wire, manipulators and live dimensions without sending
        // the complete STEP mesh through set_mesh()/upload_mesh().
        preserve_view_on_refresh_ = true;
        refresh_scene();
        if (!pending_primitive_reference_index_ &&
            extrusion_target_dialog_ == nullptr) {
            set_primitive_properties_dimension_selection();
        }
    };
    const auto extrusion_needs_input_bounds = [](
            const zima::document::HistoryContainer& preview) {
        const auto& parameters = preview.extrusion;
        return parameters.extent ==
                zima::document::ExtrusionExtent::ThroughAll ||
            parameters.end_condition_forward ==
                zima::document::EndCondition::ThroughAll ||
            (parameters.extent_mode !=
                    zima::document::ProfileExtentMode::OneSide &&
             parameters.extent_mode !=
                    zima::document::ProfileExtentMode::Symmetric &&
             parameters.end_condition_reverse ==
                    zima::document::EndCondition::ThroughAll);
    };
    if (feature_kind == zima::document::FeatureKind::Extrusion ||
        feature_kind == zima::document::FeatureKind::Thread) {
        dialog->set_extrusion_target_request([this, dialog, assembly_cut] {
            // Placement auto-arms its next empty row, but Up-to is a
            // separate selection command with a different reference owner.
            // Exactly one of those commands may consume the next LMB click.
            pending_primitive_reference_index_.reset();
            primitive_reference_auto_advance_ = false;
            dialog->set_active_reference_index(std::nullopt);
            dialog->clear_reference_highlights();
            extrusion_target_dialog_ = dialog;
            extrusion_target_assembly_cut_ = assembly_cut;
            tree_->setProperty("commandSelectionActive", true);
            viewer_->clear_selection();
            apply_extrusion_target_selection_contract();
            state_->setText(tr("Vyberte cílovou rovinnou plochu ve view."));
        });
        dialog->set_extrusion_target_cancel([this] {
            if (extrusion_target_dialog_ != nullptr)
                finish_extrusion_target_selection();
        });
    }
    if (feature_kind == zima::document::FeatureKind::Extrusion) {
        dialog->set_preview_callback([this, owner_id, assembly_cut,
                                      placement_preview,
                                      prepare_owned_profile_preview,
                                      update_owned_profile_context_preview,
                                      publish_extrusion_extent,
                                      publish_profile_preview_scene,
                                      extrusion_needs_input_bounds](
                                          const auto& preview) {
            const auto resolved_preview = placement_preview
                ? placement_preview(preview) : preview;
            try {
                if (assembly_cut) {
                    const auto* owner = workspace_.open_assembly(owner_id);
                    if (owner == nullptr) return;
                    zima::document::PartDocument preview_document;
                    preview_document.sketches = owner->session.document().sketches;
                    if (property_owned_sketch_draft_) {
                        const auto found = std::ranges::find(preview_document.sketches,
                            property_owned_sketch_draft_->id, &zima::sketcher::Sketch::id);
                        if (found == preview_document.sketches.end()) preview_document.sketches.push_back(*property_owned_sketch_draft_);
                        else *found = *property_owned_sketch_draft_;
                    }
                    prepare_owned_profile_preview(
                        preview_document, resolved_preview);
                    update_owned_profile_context_preview(
                        preview_document, resolved_preview);
                    publish_extrusion_extent(
                        preview_document, resolved_preview);
                    if (extrusion_needs_input_bounds(resolved_preview)) {
                        auto input_document = owner->session.document();
                        if (assembly_cut_rollback_ &&
                            assembly_cut_rollback_->assembly_document_id ==
                                input_document.document_id) {
                            for (auto& component : input_document.components) {
                                const auto found =
                                    assembly_cut_rollback_->
                                        input_component_bodies.find(
                                            component.occurrence_id);
                                if (found != assembly_cut_rollback_->
                                        input_component_bodies.end()) {
                                    component.calculated_source = found->second;
                                }
                            }
                        }
                        const auto input_scene = input_document.build_scene();
                        viewer_->set_transient_edges(
                            preview_document.extrusion_preview_edges(
                                resolved_preview, input_scene));
                    } else {
                        viewer_->set_transient_edges(
                            preview_document.extrusion_preview_edges(
                                resolved_preview));
                    }
                    publish_profile_preview_scene(
                        &preview_document, resolved_preview);
                } else {
                    const auto* owner = workspace_.open_part(owner_id);
                    if (owner == nullptr) return;
                    auto preview_document = owner->session.document();
                    if (property_owned_sketch_draft_) {
                        const auto found = std::ranges::find(preview_document.sketches,
                            property_owned_sketch_draft_->id, &zima::sketcher::Sketch::id);
                        if (found == preview_document.sketches.end()) preview_document.sketches.push_back(*property_owned_sketch_draft_);
                        else *found = *property_owned_sketch_draft_;
                    }
                    prepare_owned_profile_preview(
                        preview_document, resolved_preview);
                    update_owned_profile_context_preview(
                        preview_document, resolved_preview);
                    publish_extrusion_extent(
                        preview_document, resolved_preview);
                    if (extrusion_needs_input_bounds(resolved_preview)) {
                        const zima::kernel::ViewerMesh* input_mesh = nullptr;
                        if (part_rollback_ &&
                            part_rollback_->part_document_id == owner_id &&
                            part_rollback_->input_body) {
                            input_mesh = &part_rollback_->input_body->mesh;
                        } else {
                            const auto& boundaries =
                                owner->session.calculated_boundaries();
                            if (!boundaries.empty()) {
                                input_mesh = &boundaries.back().mesh;
                            }
                        }
                        viewer_->set_transient_edges(input_mesh == nullptr
                            ? preview_document.extrusion_preview_edges(
                                resolved_preview)
                            : preview_document.extrusion_preview_edges(
                                resolved_preview, *input_mesh));
                    } else {
                        viewer_->set_transient_edges(
                            preview_document.extrusion_preview_edges(
                                resolved_preview));
                    }
                    publish_profile_preview_scene(
                        &preview_document, resolved_preview);
                }
                state_->setText(tr("Azurový drát zobrazuje náhled vytažení."));
            } catch (const std::exception& error) {
                // An empty/incomplete owned Sketch prevents only the body
                // wire calculation. The already prepared placement plane,
                // profile offset/extent dimensions and manipulators remain
                // valid editing context and must still replace the previous
                // frame. Skipping this refresh was why a new Extrusion did
                // not react until after returning from Sketcher.
                viewer_->set_transient_edges({});
                publish_profile_preview_scene(nullptr, resolved_preview);
                state_->setText(QString::fromUtf8(error.what()));
            }
        });
    } else if (feature_kind == zima::document::FeatureKind::Revolution) {
        dialog->set_preview_callback([this, owner_id, assembly_cut,
                                      placement_preview,
                                      prepare_owned_profile_preview,
                                      update_owned_profile_context_preview,
                                      publish_revolution_direction,
                                      publish_profile_preview_scene](
                                          const auto& preview) {
            const auto resolved_preview = placement_preview
                ? placement_preview(preview) : preview;
            try {
                if (assembly_cut) {
                    const auto* owner = workspace_.open_assembly(owner_id);
                    if (owner == nullptr) return;
                    zima::document::PartDocument preview_document;
                    preview_document.sketches = owner->session.document().sketches;
                    if (property_owned_sketch_draft_) {
                        const auto found = std::ranges::find(preview_document.sketches,
                            property_owned_sketch_draft_->id, &zima::sketcher::Sketch::id);
                        if (found == preview_document.sketches.end()) preview_document.sketches.push_back(*property_owned_sketch_draft_);
                        else *found = *property_owned_sketch_draft_;
                    }
                    prepare_owned_profile_preview(
                        preview_document, resolved_preview);
                    update_owned_profile_context_preview(
                        preview_document, resolved_preview);
                    publish_revolution_direction(
                        preview_document, resolved_preview);
                    viewer_->set_transient_edges(
                        preview_document.revolution_preview_edges(
                            resolved_preview));
                    publish_profile_preview_scene(
                        &preview_document, resolved_preview);
                } else {
                    const auto* owner = workspace_.open_part(owner_id);
                    if (owner == nullptr) return;
                    auto preview_document = owner->session.document();
                    if (property_owned_sketch_draft_) {
                        const auto found = std::ranges::find(preview_document.sketches,
                            property_owned_sketch_draft_->id, &zima::sketcher::Sketch::id);
                        if (found == preview_document.sketches.end()) preview_document.sketches.push_back(*property_owned_sketch_draft_);
                        else *found = *property_owned_sketch_draft_;
                    }
                    prepare_owned_profile_preview(
                        preview_document, resolved_preview);
                    update_owned_profile_context_preview(
                        preview_document, resolved_preview);
                    publish_revolution_direction(
                        preview_document, resolved_preview);
                    viewer_->set_transient_edges(
                        preview_document.revolution_preview_edges(
                            resolved_preview));
                    publish_profile_preview_scene(
                        &preview_document, resolved_preview);
                }
                state_->setText(tr("Azurový drát zobrazuje náhled rotace."));
            } catch (const std::exception& error) {
                // Revolution has the same partial-preview contract as
                // Extrusion: a missing closed profile/axis may suppress the
                // operation wire, never its live work plane and dimensions.
                viewer_->set_transient_edges({});
                publish_profile_preview_scene(nullptr, resolved_preview);
                state_->setText(QString::fromUtf8(error.what()));
            }
        });
    }
    if (feature_kind == zima::document::FeatureKind::Extrusion ||
        feature_kind == zima::document::FeatureKind::Revolution) {
        dialog->set_edit_sketch_callback(
            [this, owner_id, edit_mode, pending_profile_edit, assembly_cut](
                zima::document::HistoryContainer pending_feature) {
            const bool extrusion = pending_feature.feature_kind ==
                zima::document::FeatureKind::Extrusion;
            const std::string sketch_id = extrusion
                ? pending_feature.extrusion.sketch_id
                : pending_feature.revolution.sketch_id;
            if (!edit_mode) {
                if (!property_owned_sketch_draft_) return;
                auto draft_container =
                    zima::document::PartDocument::create_sketch_container();
                draft_container.id = pending_feature.id;
                draft_container.feature_id = pending_feature.feature_id;
                draft_container.container_origin = pending_feature.container_origin;
                draft_container.name = pending_feature.name;
                draft_container.placement = pending_feature.placement;
                auto draft_sketch = *property_owned_sketch_draft_;
                draft_sketch.owner_container_id = draft_container.id;
                const auto first_reference = std::find_if(
                    pending_feature.placement.references.begin(),
                    pending_feature.placement.references.end(),
                    [](const auto& reference) {
                        return !reference.owner_id.empty();
                    });
                if (first_reference !=
                        pending_feature.placement.references.end() &&
                    first_reference->supports_offset) {
                    draft_sketch.plane = zima::sketcher::SketchPlane::XZ;
                }
                draft_sketch.plane_offset = extrusion
                    ? pending_feature.extrusion.profile_plane_offset
                    : pending_feature.revolution.profile_plane_offset;
                if (!assembly_cut) {
                    auto* target_part = workspace_.open_part(owner_id);
                    if (target_part == nullptr) return;
                    auto next = target_part->session.document();
                    if (std::none_of(next.sketches.begin(), next.sketches.end(),
                            [&](const auto& sketch) {
                                return sketch.id == draft_sketch.id;
                            })) {
                        next.sketches.push_back(std::move(draft_sketch));
                        next.insert_history_entry(
                            zima::document::PartHistoryKind::Feature,
                            draft_container.id);
                        next.history.push_back(std::move(draft_container));
                    }
                    // Resolve the owned Sketch frame before activating Sketcher.
                    const auto calculated = target_part->session.calculated_boundaries();
                    auto reference_geometry =
                        construction_reference_source_geometry(calculated);
                    append_reference_geometry(reference_geometry,
                        next.origin_viewer_mesh().original_references);
                    append_reference_geometry(reference_geometry,
                        next.construction_viewer_mesh().original_references);
                    next.resolve_constructions(reference_geometry);
                    target_part->session.commit(std::move(next), calculated);
                } else {
                    auto* target_assembly = workspace_.open_assembly(owner_id);
                    if (target_assembly == nullptr) return;
                    auto next = target_assembly->session.document();
                    if (std::none_of(next.sketches.begin(), next.sketches.end(),
                            [&](const auto& sketch) {
                                return sketch.id == draft_sketch.id;
                            })) {
                        next.sketches.push_back(std::move(draft_sketch));
                    }
                    next.resolve_constructions();
                    target_assembly->session.commit(std::move(next));
                }
            }
            // A standalone Sketch transformed into its first Extrusion/
            // Revolution already exists in History, so edit_mode is true.
            // It is nevertheless still the same pending profile transaction:
            // preserve every live field (length/end condition/direction and
            // placement orientation) before entering Sketcher, exactly like
            // a brand-new owned profile.
            property_owned_feature_draft_ = pending_feature;
            if (!edit_mode || pending_profile_edit) {
                pending_profile_feature_ = std::move(pending_feature);
            }
            if (property_owned_sketch_draft_) {
                sweep_profile_sketch_draft_ = *property_owned_sketch_draft_;
                embedded_sketch_finished_ = [this, sketch_id](zima::sketcher::Sketch sketch) {
                    property_owned_sketch_draft_ = std::move(sketch);
                    // Reuse the normal return-to-feature transition, keeping
                    // geometry in the pending draft until the feature's OK.
                    active_sketch_id_ = sketch_id;
                    finish_active_sketch();
                };
            }
            active_sketch_id_ = sketch_id;
            selected_sketch_id_ = active_sketch_id_;
            clear_selected_sketch_geometry();
            tree_->clearSelection();
            viewer_->clear_selection();
            refresh_scene();
            // Entering the owned profile from Extrusion/Revolution follows
            // the same camera contract as the standalone SKETCH button. Run
            // it after the feature dialog has finished closing; its teardown
            // refreshes the scene once more and used to leave the camera in
            // the previous 3D view instead of looking along the sketch-plane
            // normal.
            QTimer::singleShot(0, this, [this, sketch_id] {
                if (active_sketch_id_ == sketch_id) align_active_sketch_view();
            });
        });
    }
    if (feature_kind == zima::document::FeatureKind::Extrusion) {
        viewer_->set_extent_manipulator_callbacks(
            [dialog, extent_drag_baseline](const std::string&) {
                extent_drag_baseline->offset = dialog->profile_plane_offset();
                extent_drag_baseline->length = dialog->forward_extent_length();
                extent_drag_baseline->reverse_length =
                    dialog->reverse_extent_length();
                extent_drag_baseline->extent_mode =
                    dialog->profile_extent_mode();
                extent_drag_baseline->reversed = dialog->extrusion_direction_reversed();
            },
            [dialog, extent_drag_baseline](const std::string& key, double coordinate) {
                if (key == "profile_start") {
                    // The purple start point owns the profile-plane offset,
                    // not the extrusion length.  Moving it must translate the
                    // start plane in the dragged screen/world direction while
                    // preserving the entered length; changing both values made
                    // the line appear to react against the pointer.  The end
                    // point remains the independent length manipulator.
                    const double normal_displacement =
                        extent_drag_baseline->reversed ? -coordinate : coordinate;
                    dialog->set_profile_offset_and_forward_length(
                        extent_drag_baseline->offset +
                            zima::sketcher::
                                plane_offset_delta_for_normal_displacement(
                                    extent_drag_baseline->profile_plane,
                                    normal_displacement),
                        extent_drag_baseline->length);
                } else if (key == "length_forward") {
                    if (extent_drag_baseline->extent_mode ==
                            zima::document::ProfileExtentMode::Symmetric) {
                        dialog->set_forward_extent_length(std::abs(coordinate));
                        return;
                    }
                    dialog->set_forward_extent_and_direction(
                        std::abs(coordinate),
                        coordinate < 0.0
                            ? !extent_drag_baseline->reversed
                            : extent_drag_baseline->reversed);
                } else if (key == "length_reverse") {
                    dialog->set_reverse_extent_and_direction(
                        std::abs(coordinate),
                        coordinate < 0.0
                            ? !extent_drag_baseline->reversed
                            : extent_drag_baseline->reversed);
                }
            }, [] {});
    } else if (feature_kind == zima::document::FeatureKind::Revolution) {
        viewer_->set_extent_manipulator_callbacks(
            [dialog, extent_drag_baseline](const std::string&) {
                extent_drag_baseline->length =
                    dialog->forward_extent_length();
                extent_drag_baseline->reverse_length =
                    dialog->reverse_extent_length();
                extent_drag_baseline->extent_mode =
                    dialog->profile_extent_mode();
                extent_drag_baseline->reversed =
                    dialog->extrusion_direction_reversed();
            },
            [dialog, extent_drag_baseline](const std::string& key,
                    double coordinate) {
                if (!key.starts_with("revolution_angle_")) return;
                constexpr double cue_radius = 25.0;
                const double delta_degrees = coordinate / cue_radius *
                    180.0 / std::numbers::pi;
                if (key == "revolution_angle_reverse") {
                    const double signed_magnitude =
                        extent_drag_baseline->reverse_length + delta_degrees;
                    dialog->set_reverse_extent_and_direction(
                        std::clamp(std::abs(signed_magnitude), 0.001,
                            360.0 - extent_drag_baseline->length),
                        signed_magnitude < 0.0
                            ? !extent_drag_baseline->reversed
                            : extent_drag_baseline->reversed);
                    return;
                }
                const double signed_magnitude =
                    extent_drag_baseline->length + delta_degrees;
                const double maximum = extent_drag_baseline->extent_mode ==
                        zima::document::ProfileExtentMode::TwoSides
                    ? 360.0 - extent_drag_baseline->reverse_length
                    : extent_drag_baseline->extent_mode ==
                            zima::document::ProfileExtentMode::Symmetric
                        ? 180.0 : 360.0;
                dialog->set_forward_extent_and_direction(
                    std::clamp(std::abs(signed_magnitude), 0.001, maximum),
                    signed_magnitude < 0.0
                        ? !extent_drag_baseline->reversed
                        : extent_drag_baseline->reversed);
            }, [] {});
    }
    properties_dialog_ = dialog;
    track_tree_edit(dialog);

    const std::string dialog_container_id = initial.id;
    if (!assembly_cut &&
        feature_kind == zima::document::FeatureKind::DrillPoint) {
        drill_point_dialog_ = dialog;
        pending_drill_point_faces_ = initial.drill_point.bottom_faces;
        drill_point_face_selection_active_ = true;
        dialog->set_drill_point_face_callbacks(
            [this](std::size_t index) { remove_drill_point_face(index); },
            [this] {
                drill_point_face_selection_active_ = true;
                refresh_drill_point_selection_ui();
                state_->setText(tr(
                    "Vyberte kruhová dna otvorů pro vrtací špičky."));
            });
        refresh_drill_point_selection_ui();
    }
    if (edit_mode && !assembly_cut &&
        (feature_kind == zima::document::FeatureKind::Fillet ||
         feature_kind == zima::document::FeatureKind::Chamfer)) {
        edge_treatment_selection_ = feature_kind;
        edge_treatment_preview_parameters_ = initial.edge_treatment;
        edge_treatment_preview_owner_id_ = initial.id;
        edge_treatment_hover_seed_.reset();
        viewer_->set_feature_hover_edges({});
        edge_treatment_dialog_ = dialog;
        pending_edge_treatment_edges_ =
            initial.edge_treatment.flattened_edges();
        pending_edge_treatment_groups_ = initial.edge_treatment.routes;
        pending_edge_treatment_seeds_.clear();
        for (const auto& route : pending_edge_treatment_groups_) {
            if (!route.empty()) pending_edge_treatment_seeds_.push_back(route.front());
        }
        dialog->set_edge_groups(pending_edge_treatment_groups_);
        dialog->set_edge_group_callbacks(
            [this](std::size_t group, std::optional<std::size_t> member) {
                remove_edge_treatment_member(group, member);
            },
            [this](std::size_t group) { restore_edge_treatment_route(group); });
        dialog->set_preview_callback([this](const auto& preview) {
            edge_treatment_preview_parameters_ = preview.edge_treatment;
            refresh_edge_treatment_preview();
        });
        refresh_edge_treatment_selection_ui();
    }
    if (edit_mode && !assembly_cut &&
        feature_kind == zima::document::FeatureKind::Shell) {
        shell_dialog_ = dialog;
        pending_shell_faces_ = initial.shell.removed_faces;
        shell_face_selection_active_ = true;
        dialog->set_shell_face_callbacks(
            [this](std::size_t index) { remove_shell_face(index); },
            [this] {
                shell_face_selection_active_ = true;
                refresh_shell_selection_ui();
                state_->setText(tr(
                    "Vyberte plochy, které má Shell otevřít."));
            });
        refresh_shell_selection_ui();
    }
    if (pending_profile_edit) {
        connect(dialog, &QDialog::accepted, this, [this, dialog_container_id] {
            if (pending_profile_feature_ &&
                pending_profile_feature_->id == dialog_container_id) {
                pending_profile_feature_.reset();
            }
            if (pending_profile_transform_original_ &&
                pending_profile_transform_original_->id == dialog_container_id) {
                pending_profile_transform_original_.reset();
            }
        });
    }
    connect(dialog, &QDialog::rejected, this, [this, dialog_container_id] {
        if (!pending_profile_feature_ ||
            pending_profile_feature_->id != dialog_container_id) return;
        if (pending_profile_transform_original_ &&
            pending_profile_transform_original_->id == dialog_container_id) {
            if (auto* target_part = workspace_.open_part(
                    workspace_.active_document_id())) {
                auto next = target_part->session.document();
                if (auto* container = next.find_container(dialog_container_id)) {
                    *container = *pending_profile_transform_original_;
                }
                target_part->session.commit(std::move(next),
                    target_part->session.calculated_boundaries());
            }
            pending_profile_feature_.reset();
            pending_profile_transform_original_.reset();
            return;
        }
        if (auto* target_part = workspace_.open_part(
                workspace_.active_document_id())) {
            auto next = target_part->session.document();
            std::erase_if(next.sketches, [&](const auto& sketch) {
                return sketch.owner_container_id == dialog_container_id;
            });
            std::erase_if(next.history, [&](const auto& container) {
                return container.id == dialog_container_id;
            });
            std::erase_if(next.history_order, [&](const auto& entry) {
                return entry.id == dialog_container_id;
            });
            target_part->session.commit(std::move(next),
                target_part->session.calculated_boundaries());
        } else if (auto* target_assembly = workspace_.open_assembly(
                       workspace_.active_document_id())) {
            auto next = target_assembly->session.document();
            std::erase_if(next.sketches, [&](const auto& sketch) {
                return sketch.owner_container_id == dialog_container_id;
            });
            target_assembly->session.commit(std::move(next));
        }
        pending_profile_feature_.reset();
    });
    if (assembly_cut_index) {
        assembly_cut_rollback_ = AssemblyCutRollbackContext{
            assembly->session.document().document_id, container_id,
            *assembly_cut_index, edited_cut->input_component_bodies};
        refresh_scene();
    }
    if (rollback_boundary) {
        part_rollback_ = PartRollbackContext{
            part->session.document().document_id, *rollback_occurrence,
            rollback_boundary->history_index, rollback_boundary->input_body};
        refresh_scene();
    }
    connect(dialog, &QObject::destroyed, this, [this, dialog_container_id] {
        // SKETCH is a transition into the profile sub-editor, not the end of
        // the container edit session. Keep the rollback input alive while
        // that exact owned Sketch is active; otherwise the teardown refresh
        // replaces the correct pre-feature/wire context with the calculated
        // final solid inside Sketcher.
        bool entering_owned_profile_sketch = false;
        if (!active_sketch_id_.empty()) {
            if (const auto* active_part = workspace_.open_part(
                    workspace_.active_document_id())) {
                entering_owned_profile_sketch = std::any_of(
                    active_part->session.document().sketches.begin(),
                    active_part->session.document().sketches.end(),
                    [&](const auto& sketch) {
                        return sketch.id == active_sketch_id_ &&
                            sketch.owner_container_id == dialog_container_id;
                    });
            } else if (const auto* active_assembly = workspace_.open_assembly(
                           workspace_.active_document_id())) {
                entering_owned_profile_sketch = std::any_of(
                    active_assembly->session.document().sketches.begin(),
                    active_assembly->session.document().sketches.end(),
                    [&](const auto& sketch) {
                        return sketch.id == active_sketch_id_ &&
                            sketch.owner_container_id == dialog_container_id;
                    });
            }
        }
        if (!entering_owned_profile_sketch) {
            property_owned_sketch_draft_.reset();
            property_owned_feature_draft_.reset();
        }
        properties_dialog_ = nullptr;
        edge_treatment_dialog_ = nullptr;
        edge_treatment_selection_.reset();
        edge_treatment_hover_seed_.reset();
        pending_edge_treatment_edges_.clear();
        pending_edge_treatment_groups_.clear();
        pending_edge_treatment_seeds_.clear();
        edge_treatment_preview_owner_id_.clear();
        shell_dialog_ = nullptr;
        shell_face_selection_active_ = false;
        pending_shell_faces_.clear();
        drill_point_dialog_ = nullptr;
        drill_point_face_selection_active_ = false;
        pending_drill_point_faces_.clear();
        extrusion_target_dialog_ = nullptr;
        extrusion_target_assembly_cut_ = false;
        primitive_reference_dialog_ = nullptr;
        pending_primitive_reference_index_.reset();
        primitive_reference_auto_advance_ = false;
        primitive_reference_geometry_ = {};
        primitive_origin_preview_mesh_.reset();
        parameter_dimension_preview_.reset();
        primitive_parameter_owner_id_.clear();
        // Parameter/placement dimensions belong to the feature-properties
        // interaction. In particular, SKETCH transitions into a different
        // editor whose view must contain only Sketch constraints/dimensions.
        construction_dimension_object_id_.clear();
        primitive_translation_dof_ = 3;
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_transient_edges({});
        viewer_->set_transient_dimensions({});
        viewer_->set_extent_manipulator(std::nullopt);
        viewer_->set_operation_direction_indicator(std::nullopt);
        viewer_->set_extent_manipulator_callbacks({}, {}, {});
        viewer_->set_feature_preview_owners({});
        viewer_->set_candidate_filter({});
        viewer_->set_constraint_reference_highlights({}, {});
        viewer_->set_edge_treatment_selection_edges({});
        viewer_->set_feature_hover_edges({});
        if (!entering_owned_profile_sketch) {
            part_rollback_.reset();
            assembly_cut_rollback_.reset();
        }
        refresh_tabs();
        // See the identical guard in show_construction_properties()'s
        // destroyed handler: closing this dialog must not re-fit/zoom the
        // camera to the just-committed (or reverted) feature geometry.
        preserve_view_on_refresh_ = true;
        refresh_scene();
        if (entering_owned_profile_sketch) {
            clear_selected_sketch_geometry();
            tree_->clearSelection();
            viewer_->clear_selection();
        }
    });
    dialog->show();
    // Properties is not a picking command by itself. Keep all model hover
    // disabled until the user explicitly requests one placement/Up-to
    // reference field; that command installs its own exact selection
    // contract and returns here after one confirmed pick.
    if (drill_point_dialog_ != nullptr) {
        refresh_drill_point_selection_ui();
    } else if (shell_dialog_ != nullptr) {
        refresh_shell_selection_ui();
    } else if (edge_treatment_selection_) {
        refresh_edge_treatment_selection_ui();
    } else if (supports_placement_reference_picking(feature_kind) &&
               primitive_reference_dialog_ != nullptr) {
        const auto first = primitive_reference_dialog_->first_empty_position_index();
        if (first < 3)
            start_primitive_reference_selection(first, true);
    } else {
        set_primitive_properties_dimension_selection();
    }
}

} // namespace zima::app

#include "workspace_internal.hpp"
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/workspace/holes_operations.hpp>
#include <zima/document/holes.hpp>

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::show_sketch_properties(const std::string& sketch_id, bool holes_mode) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    if (holes_mode && !part) return;
    const auto& sketches = part != nullptr
        ? part->session.document().sketches : assembly->session.document().sketches;
    const auto found = std::find_if(sketches.begin(), sketches.end(),
        [&](const auto& sketch) { return sketch.id == sketch_id; });
    const bool edit_mode = found != sketches.end();
    if (!sketch_id.empty() && !edit_mode) return;
    auto initial = edit_mode ? *found : zima::sketcher::Sketch::create_default();
    std::optional<zima::document::HistoryContainer> new_sketch_container;
    if (!edit_mode) {
        new_sketch_container = zima::document::PartDocument::create_sketch_container();
        initial.owner_container_id = new_sketch_container->id;
    }
    std::shared_ptr<zima::document::HistoryContainer> holes_feature;
    if (part) {
        const auto* owner = part->session.document().find_container(initial.owner_container_id);
        holes_mode = holes_mode || (owner && owner->feature_kind == zima::document::FeatureKind::Holes);
        if (holes_mode) {
            if (owner && owner->feature_kind != zima::document::FeatureKind::Sketch &&
                owner->feature_kind != zima::document::FeatureKind::Holes) return;
            auto value = owner ? (owner->feature_kind == zima::document::FeatureKind::Holes ? *owner
                : workspace::holes_from_sketch(part->session.document(), initial.id)) : *new_sketch_container;
            if (!owner) {
                value.feature_kind = zima::document::FeatureKind::Holes;
                value.combine_mode = zima::document::CombineMode::Subtract;
                value.name = "Otvory"; value.holes.sketch_id = initial.id;
            }
            initial.name = value.name;
            holes_feature = std::make_shared<zima::document::HistoryContainer>(std::move(value));
            const auto occurrence = resolve_active_occurrence(part->session.document().document_id);
            if (!occurrence) {state_->setText(tr("Nejprve aktivujte přesný výskyt Partu."));return;}
            properties_dialog_instance_path_ = *occurrence;
            if (owner) {
                const auto rollback = part->session.rollback_boundary(owner->id);
                if (!rollback || !rollback->input_body) {
                    state_->setText(tr("Otvory potřebují vypočtené vstupní těleso. Nejprve regenerujte Part."));return;
                }
                part_rollback_ = PartRollbackContext{part->session.document().document_id,
                    *occurrence, rollback->history_index, rollback->input_body};
            }
        }
    }
    auto prepared_sketch = std::make_shared<zima::sketcher::Sketch>(initial);
    zima::document::Placement initial_placement;
    if (part != nullptr) {
        const auto& history = part->session.document().history;
        const auto owner_container = std::find_if(history.begin(), history.end(),
            [&](const auto& container) {
                return container.id == initial.owner_container_id;
            });
        if (owner_container != history.end()) {
            initial_placement = workspace::sketch_properties_placement(*owner_container);
        }
    } else if (assembly != nullptr) {
        if (const auto* cut = assembly->session.document().find_cut(
                initial.owner_container_id)) {
            initial_placement = workspace::sketch_properties_placement(cut->definition);
        } else if (const auto* container = assembly->session.document().find_sketch_container(initial.owner_container_id)) {
            initial_placement = container->placement;
        } else if (pending_profile_feature_ &&
                   pending_profile_feature_->id == initial.owner_container_id) {
            initial_placement = workspace::sketch_properties_placement(*pending_profile_feature_);
        }
    }
    if (!edit_mode && new_sketch_container) {
        initial_placement = new_sketch_container->placement;
    }
    const auto& constructions = part != nullptr
        ? part->session.document().constructions : assembly->session.document().constructions;
    std::vector<SketchPropertiesDialog::PlaneOption> plane_options;
    for (const auto& object : constructions) {
        if (object.kind != zima::document::ConstructionKind::Plane) continue;
        plane_options.push_back(SketchPropertiesDialog::PlaneOption{
            object.entity_id, QString::fromStdString(object.name)});
    }
    const std::string owner_id = workspace_.active_document_id();
    auto* dialog = new SketchPropertiesDialog(
        initial, initial_placement, edit_mode, std::move(plane_options),
        [this, owner_id, edit_mode, new_sketch_container, holes_feature](
            zima::sketcher::Sketch committed,
            zima::document::Placement committed_placement,
            bool enter_sketch) {
            const auto selected_id = committed.id;
            if (holes_feature) {
                auto value = *holes_feature;
                value.name = committed.name; value.placement = std::move(committed_placement);
                static_cast<void>(workspace::commit_holes(workspace_, kernel_, owner_id,
                    std::move(value), std::move(committed)));
            } else {
                static_cast<void>(workspace::commit_sketch_properties(workspace_, kernel_, owner_id,
                    std::move(committed), std::move(committed_placement), new_sketch_container));
            }
            if (enter_sketch) {
                active_sketch_id_ = selected_id;
                clear_selected_sketch_geometry();
                tree_->clearSelection();
                viewer_->clear_selection();
            }
            selected_sketch_id_ = selected_id;
        }, this);
    if (holes_feature) {
        dialog->set_holes_mode(holes_feature->holes.diameter, holes_feature->value_locks,
            [holes_feature](double diameter) { holes_feature->holes.diameter = diameter; },
            [this, dialog, prepared_sketch] {
                sweep_profile_sketch_draft_ = *prepared_sketch;
                embedded_sketch_finished_ = [this, dialog](auto sketch) {
                    properties_dialog_ = dialog; primitive_reference_dialog_ = dialog;
                    dialog->set_pending_sketch(std::move(sketch));
                    dialog->show(); dialog->raise();
                    preserve_view_on_refresh_ = true; refresh_scene();
                };
                pending_primitive_reference_index_.reset(); primitive_reference_auto_advance_ = false;
                dialog->set_active_reference_index(std::nullopt); dialog->clear_reference_highlights();
                set_local_origin_selection_mode(false); local_origin_selection_dialog_ = nullptr;
                primitive_reference_dialog_ = nullptr;
                viewer_->set_constraint_reference_highlights({}, {});
                viewer_->set_feature_preview_owners({});
                viewer_->set_extent_manipulator(std::nullopt);
                viewer_->set_transient_edges({});
                primitive_origin_preview_mesh_.reset(); parameter_dimension_preview_.reset();
                dialog->hide(); properties_dialog_ = nullptr;
                active_sketch_id_ = prepared_sketch->id; selected_sketch_id_ = active_sketch_id_;
                clear_selected_sketch_geometry(); viewer_->clear_selection(); tree_->clearSelection();
                preserve_view_on_refresh_ = true; refresh_scene(); align_active_sketch_view();
            });
    }
    {
        zima::kernel::ViewerReferenceGeometry reference_geometry;
        if (part != nullptr) {
            reference_geometry = construction_reference_source_geometry(
                part->session.calculated_boundaries());
            const auto& document = part->session.document();
            const auto* body = document.body_owner_for_object(initial.owner_container_id.empty()
                ? initial.id : initial.owner_container_id);
            if (!body && new_sketch_container)
                body = document.body_history.find(document.body_history.active_body_id());
            sketch_properties_body_id_ = body ? body->scope.id : std::string{};
            append_reference_geometry(reference_geometry,
                document.origin_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry, document.body_origin_reference_geometry());
            append_reference_geometry(reference_geometry,
                document.construction_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                document.history_origin_reference_geometry_before(
                    initial.owner_container_id));
            if (body) reference_geometry = document.construction_reference_geometry_for(
                body->scope.id, std::move(reference_geometry));
        } else {
            const auto& document = assembly->session.document();
            reference_geometry = document.build_scene().original_references;
            append_reference_geometry(reference_geometry,
                document.origin_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                document.construction_viewer_mesh().original_references);
        }
        primitive_reference_geometry_ = reference_geometry;
        primitive_reference_dialog_ = dialog;
        primitive_parameter_owner_id_ = initial.owner_container_id.empty() ? initial.id : initial.owner_container_id;
        dialog->set_reference_geometry(reference_geometry);
        dialog->set_reference_request_callback(
            [this](std::size_t index) {
                start_primitive_reference_selection(index);
            });
        dialog->set_reference_highlights_changed_callback([this, dialog] {
            viewer_->set_constraint_reference_highlights(
                {}, highlighted_reference_edge_keys(*dialog));
        });
        struct SketchOffsetDragState {
            double current_offset{};
            double baseline_offset{};
            zima::sketcher::SketchPlane plane{
                zima::sketcher::SketchPlane::XY};
        };
        auto sketch_offset_drag = std::make_shared<SketchOffsetDragState>();
        viewer_->set_extent_manipulator_callbacks(
            [sketch_offset_drag](const std::string&) {
                sketch_offset_drag->baseline_offset =
                    sketch_offset_drag->current_offset;
            },
            [dialog, sketch_offset_drag](const std::string& key,
                    double coordinate) {
                if (key != "sketch_plane_offset") return;
                const double delta = zima::sketcher::
                    plane_offset_delta_for_normal_displacement(
                        sketch_offset_drag->plane, coordinate);
                static_cast<void>(dialog->set_inline_parameter_value(
                    "profile_offset",
                    sketch_offset_drag->baseline_offset + delta));
            }, [] {});
        dialog->set_preview_callback([this, sketch_offset_drag, prepared_sketch, holes_feature](
                const zima::sketcher::Sketch& sketch,
                const zima::document::Placement& pending_placement) {
            auto placement = pending_placement;
            // resolve_placement() deliberately treats the complete built-in
            // Origin plane triad as a request for the document identity
            // frame.  That is correct for an ordinary container populated by
            // clicking the whole Origin node, but not for Sketch Properties:
            // here row 0 is an explicitly selected work plane and must remain
            // FRONT while rows 1/2 only finish locating the container.  The
            // committed PartDocument resolver already enforces that stricter
            // Sketch contract.  Give the live preview the same transient
            // row-0 orientation twin so its cyan plane cannot jump back to
            // the identity/last Origin plane while the dialog is open.
            const auto first_position_plane = std::find_if(
                placement.references.begin(), placement.references.end(),
                [](const auto& reference) {
                    return !reference.orientation_only &&
                        reference.supports_offset &&
                        !reference.owner_id.empty();
                });
            if (first_position_plane != placement.references.end()) {
                const auto front_owner = first_position_plane->owner_id;
                const auto front_path = first_position_plane->instance_path;
                const auto front_semantic = first_position_plane->semantic_key;
                const auto is_front_source = [&](const auto& reference) {
                    return reference.owner_id == front_owner &&
                        reference.instance_path == front_path &&
                        reference.semantic_key == front_semantic;
                };
                for (auto& reference : placement.references) {
                    if (reference.orientation_only) continue;
                    reference.orientation_drives_rotation =
                        is_front_source(reference);
                    reference.orientation_role =
                        is_front_source(reference) ? "front" : "none";
                }
                std::erase_if(placement.references,
                    [&](const auto& reference) {
                        return reference.orientation_only &&
                            !is_front_source(reference);
                    });
                auto preview_front = *first_position_plane;
                preview_front.orientation_only = true;
                preview_front.orientation_drives_rotation = true;
                preview_front.orientation_role = "front";
                placement.references.push_back(std::move(preview_front));
            }
            zima::kernel::Vec3 base_rotation;
            bool orientation_from_reference = false;
            const bool placement_valid = zima::document::resolve_placement(
                placement, primitive_reference_geometry_, &base_rotation,
                &orientation_from_reference);
            const auto state = zima::document::point_constraint_state(
                placement.references, primitive_reference_geometry_);
            primitive_translation_dof_ = state.remaining_dof;
            if (primitive_reference_dialog_ != nullptr) {
                primitive_reference_dialog_->set_translation_constraint_state(
                    state, {placement.x, placement.y, placement.z});
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
            }
            // Preview the Sketch in the final container frame. Placement
            // FRONT/BACK and ROTATE are modeling-frame controls here, so the
            // white Sketch wire must follow them live just like an owned
            // Extrusion/Revolution profile does.
            const auto& geometric_placement = placement;

            auto plane = zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Plane);
            if (!sketch.owner_container_id.empty()) {
                plane.id = sketch.owner_container_id;
                plane.entity_id = plane.id + ":entity";
                plane.entity_parent_id = plane.id;
                plane.container_origin =
                    zima::document::create_container_origin(plane.id);
            }
            plane.name = sketch.name;
            plane.origin = {geometric_placement.x, geometric_placement.y,
                            geometric_placement.z};
            plane.rotation = {geometric_placement.rotation_x,
                geometric_placement.rotation_y, geometric_placement.rotation_z};
            plane.absolute_rotation = {geometric_placement.absolute_rotation_x,
                geometric_placement.absolute_rotation_y,
                geometric_placement.absolute_rotation_z};
            plane.orientation_back = false;
            plane.orientation_quarter_turns = 0;
            plane.base_plane = sketch.plane == zima::sketcher::SketchPlane::XY
                ? zima::document::LocalDatumPlane::XY
                : sketch.plane == zima::sketcher::SketchPlane::XZ
                    ? zima::document::LocalDatumPlane::XZ
                    : zima::document::LocalDatumPlane::YZ;
            plane.offset = sketch.plane_offset;
            plane.reference_valid = true;
            zima::document::PartDocument preview_document;
            preview_document.constructions.push_back(plane);
            if (!sketch.owner_container_id.empty()) {
                auto preview_container =
                    zima::document::PartDocument::create_sketch_container();
                preview_container.id = sketch.owner_container_id;
                preview_container.placement = geometric_placement;
                preview_document.history.push_back(std::move(preview_container));
            }
            preview_document.sketches.push_back(sketch);
            preview_document.resolve_constructions(primitive_reference_geometry_);
            auto& resolved_sketch = preview_document.sketches.front();
            // The dialog's already-resolved placement is authoritative for
            // the live preview. A second solve inside preview_document may
            // use a different transient reference set and must not move the
            // visible local Origin away from the X/Y/Z values shown to the
            // user.
            resolved_sketch.resolved_origin = {
                geometric_placement.x + resolved_sketch.resolved_normal.x *
                    resolved_sketch.plane_offset,
                geometric_placement.y + resolved_sketch.resolved_normal.y *
                    resolved_sketch.plane_offset,
                geometric_placement.z + resolved_sketch.resolved_normal.z *
                    resolved_sketch.plane_offset};
            *prepared_sketch = resolved_sketch;
            // The cyan rectangle represents the actual (possibly offset)
            // Sketch work plane.  Do not leave it in the generic container
            // placement frame: the complete built-in Origin triad has a
            // special identity-frame rule there, while an owned Sketch keeps
            // its first selected plane as FRONT.  Rebuild this display-only
            // Plane from the already resolved Sketch frame so the rectangle,
            // offset point and the plane opened by SKETCH are identical.
            auto& resolved_plane = preview_document.constructions.front();
            const zima::kernel::Vec3 local_z{
                -resolved_sketch.resolved_y_axis.x,
                -resolved_sketch.resolved_y_axis.y,
                -resolved_sketch.resolved_y_axis.z};
            const auto resolved_rotation = euler_degrees_from_frame_columns(
                resolved_sketch.resolved_x_axis,
                resolved_sketch.resolved_normal, local_z);
            resolved_plane.base_plane = zima::document::LocalDatumPlane::XZ;
            resolved_plane.rotation = resolved_rotation;
            resolved_plane.absolute_rotation = resolved_rotation;
            resolved_plane.direction = resolved_sketch.resolved_normal;
            resolved_plane.entity_origin = resolved_sketch.resolved_origin;
            resolved_plane.origin = {
                resolved_sketch.resolved_origin.x -
                    resolved_sketch.resolved_normal.x * resolved_sketch.plane_offset,
                resolved_sketch.resolved_origin.y -
                    resolved_sketch.resolved_normal.y * resolved_sketch.plane_offset,
                resolved_sketch.resolved_origin.z -
                    resolved_sketch.resolved_normal.z * resolved_sketch.plane_offset};
            resolved_plane.reference_valid = true;
            primitive_origin_preview_mesh_ =
                preview_document.construction_viewer_mesh(plane.id);
            // Placement dimensions belong to the Sketch Properties
            // interaction and must therefore be present even for a brand-new
            // Sketch whose owning container is not in the real document yet.
            // Keeping them in this transient mesh also gives the SKETCH
            // transition a clean boundary: destroying the properties dialog
            // removes the complete parameter preview before Sketcher opens.
            if (!sketch.owner_container_id.empty()) {
                auto placement_dimensions =
                    zima::document::container_placement_dimensions(
                        sketch.owner_container_id, geometric_placement,
                        primitive_reference_geometry_);
                append_nonzero_parameter_dimensions(
                    primitive_origin_preview_mesh_->dimensions,
                    std::move(placement_dimensions));
            }
            sketch_offset_drag->current_offset =
                resolved_sketch.plane_offset;
            sketch_offset_drag->plane = resolved_sketch.plane;
            const zima::viewer::ExtentManipulator offset_manipulator{
                "sketch_plane_offset", {}, resolved_sketch.resolved_origin,
                resolved_sketch.resolved_normal, 0.0, true};
            if (std::abs(resolved_sketch.plane_offset) > 1.0e-12) {
                const auto start = resolved_sketch.resolved_origin;
                const auto normal = resolved_sketch.resolved_normal;
                const auto base = zima::kernel::Vec3{
                    start.x - normal.x * resolved_sketch.plane_offset,
                    start.y - normal.y * resolved_sketch.plane_offset,
                    start.z - normal.z * resolved_sketch.plane_offset};
                const auto front = resolved_sketch.resolved_x_axis;
                constexpr double witness_length = 8.0;
                zima::kernel::ViewerDimension offset_dimension{
                    base, start,
                    {base.x + front.x * witness_length,
                     base.y + front.y * witness_length,
                     base.z + front.z * witness_length},
                    {start.x + front.x * witness_length,
                     start.y + front.y * witness_length,
                     start.z + front.z * witness_length},
                    resolved_sketch.plane_offset,
                    {sketch.owner_container_id,
                     "parameter:profile_offset", {}}, {}};
                offset_dimension.plane_normal = {
                    normal.y * front.z - normal.z * front.y,
                    normal.z * front.x - normal.x * front.z,
                    normal.x * front.y - normal.y * front.x};
                primitive_origin_preview_mesh_->dimensions.push_back(
                    std::move(offset_dimension));
            }
            // A standalone Sketch container remains visible while its
            // Properties window is open. Only suppress Sketcher's infinite
            // working X/Y cross here; removing the complete viewer_mesh()
            // also removed the actual white sketch curves. Extrusion and
            // Revolution own separate internal sketches and continue to
            // control their normal-view visibility through their history
            // container contract.
            auto sketch_preview = preview_document.sketches.front().viewer_mesh();
            sketch_preview.axes.clear();
            remove_sketch_computation_points(sketch_preview);
            append_mesh(*primitive_origin_preview_mesh_, std::move(sketch_preview));
            sketch_properties_preview_id_ = sketch.id;
            viewer_->set_feature_preview_owners(
                {plane.entity_id, sketch.id});
            zima::kernel::ViewerMesh drilling_preview;
            if (holes_feature) {
                try {
                    drilling_preview = zima::document::holes_preview(*holes_feature, resolved_sketch);
                    primitive_origin_preview_mesh_->dimensions.insert(
                        primitive_origin_preview_mesh_->dimensions.end(),
                        drilling_preview.dimensions.begin(), drilling_preview.dimensions.end());
                } catch (const std::exception&) {
                    // Empty or incomplete drafts have no drilling preview.
                    // OK retains the normal feature validation and message.
                }
            }
            preserve_view_on_refresh_ = true;
            refresh_scene();
            if (holes_feature) viewer_->set_transient_edges(std::move(drilling_preview.edges));
            viewer_->set_extent_manipulator(offset_manipulator);
            if (!pending_primitive_reference_index_) {
                set_primitive_properties_dimension_selection();
            }
        });
    }
    properties_dialog_ = dialog;
    track_tree_edit(dialog);
    if (tree_edit_dialog_ == dialog) {
        tree_edit_sketch_container_ = new_sketch_container;
        if (!tree_edit_sketch_container_ && part) {
            if (const auto* owner = part->session.document().find_container(dialog->pending_value().first.owner_container_id))
                tree_edit_sketch_container_ = *owner;
        }
        if (!tree_edit_sketch_container_ && assembly) {
            if (const auto* owner = assembly->session.document().find_sketch_container(dialog->pending_value().first.owner_container_id))
                tree_edit_sketch_container_ = *owner;
        }
    }

    connect(dialog, &QObject::destroyed, this, [this, holes_feature] {
        if (holes_feature) {
            part_rollback_.reset();
            properties_dialog_instance_path_.clear();
            sweep_profile_sketch_draft_.reset(); embedded_sketch_finished_ = {};
        }
        properties_dialog_ = nullptr;
        primitive_reference_dialog_ = nullptr;
        primitive_parameter_owner_id_.clear();
        pending_primitive_reference_index_.reset();
        primitive_reference_auto_advance_ = false;
        primitive_reference_geometry_ = {};
        primitive_translation_dof_ = 3;
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_candidate_filter({});
        viewer_->set_selection_contract({});
        viewer_->set_constraint_reference_highlights({}, {});
        viewer_->set_feature_preview_owners({});
        viewer_->set_transient_edges({});
        viewer_->set_extent_manipulator(std::nullopt);
        viewer_->set_extent_manipulator_callbacks({}, {}, {});
        primitive_origin_preview_mesh_.reset();
        sketch_properties_preview_id_.clear();
        sketch_properties_body_id_.clear();
        refresh_tabs();
        refresh_scene();
        if (!active_sketch_id_.empty()) {
            // The final dialog-teardown refresh can restore the cyan
            // Container confirmation which opened Properties. Clear after
            // that refresh so Sketcher always starts with bare geometry.
            clear_selected_sketch_geometry();
            tree_->clearSelection();
            viewer_->clear_selection();
            align_active_sketch_view();
        }
    });
    dialog->show();
    const auto first = dialog->first_empty_position_index();
    if (first < 3) {
        start_primitive_reference_selection(first, true);
    } else {
        set_primitive_properties_dimension_selection();
    }
}

void AssemblyWorkspaceWindow::show_sketch_bspline_properties(
    const std::string& sketch_id, const std::string& bspline_id) {
    if (properties_dialog_ != nullptr || sketch_bspline_active_) return;
    if(const auto* sketch=active_sketch();sketch && sketch->find_offset(bspline_id)){show_sketch_offset_properties(bspline_id);return;}
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto spline = std::find_if(sketch->bsplines.begin(), sketch->bsplines.end(),
        [&](const auto& value) { return value.id == bspline_id; });
    if (spline == sketch->bsplines.end()) return;
    std::vector<std::array<double, 2>> points;
    points.reserve(spline->control_point_ids.size());
    for (const auto& point_id : spline->control_point_ids) {
        const auto* point = sketch->find_point(point_id);
        if (point == nullptr) return;
        points.push_back({point->x, point->y});
    }
    auto* dialog = new SketchBSplinePropertiesDialog(
        spline->degree, spline->closed, std::move(points),
        [this, sketch_id, bspline_id](
            unsigned degree, bool closed,
            const std::vector<std::array<double, 2>>& values) {
            if (active_sketch_id_ != sketch_id ||
                !mutate_active_sketch([&](auto& target_sketch) {
                    target_sketch.edit_bspline_properties(bspline_id, degree, closed, values);
                })) throw std::runtime_error("Sketch no longer exists");
        }, this);
    if (!spline->knots.empty()) {
        dialog->set_exact_geometry(sketch->bspline_properties_read_only(bspline_id));
    }
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::show_sketch_text_properties(
    const std::string& sketch_id, const std::string& text_id) {
    if (properties_dialog_ != nullptr || sketch_id.empty() ||
        sketch_id != active_sketch_id_) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == sketch_id; });
    if (sketch == part->session.document().sketches.end()) return;

    const bool edit_mode = !text_id.empty();
    zima::sketcher::SketchText initial;
    std::optional<std::array<double, 2>> anchor;
    if (edit_mode) {
        const auto text = std::find_if(sketch->texts.begin(), sketch->texts.end(),
            [&](const auto& value) { return value.id == text_id; });
        if (text == sketch->texts.end()) return;
        initial = *text;
        anchor = std::array{text->anchor_x, text->anchor_y};
    } else {
        initial = zima::sketcher::Sketch::create_text();
        if(sketch->drawing_template){initial.flipped=true;initial.height=2.5;initial.modeling_geometry=false;}
    }

    cancel_sketch_segment();
    sketch_text_active_ = true;
    set_sketch_placement_selection_contract();
    editing_sketch_text_id_ = text_id;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_external_reference_id_.clear();
    selected_sketch_geometry_ids_.clear();
    selected_sketch_text_id_ = text_id;

    auto* dialog = new SketchTextPropertiesDialog(
        std::move(initial), anchor,
        [this, sketch_id](
            const std::optional<zima::sketcher::SketchText>& preview) {
            if (!preview) {
                viewer_->set_transient_edges({});
                return;
            }
            const auto* target_sketch = active_sketch();
            if (target_sketch == nullptr || target_sketch->id != sketch_id) return;
            viewer_->set_transient_edges(
                sketch_text_preview_edges(*target_sketch, *preview));
        },
        [this, sketch_id, edit_mode](
            zima::sketcher::SketchText committed) {
            const std::string committed_id = committed.id;
            if (active_sketch_id_ != sketch_id ||
                !mutate_active_sketch([&](auto& target_sketch) {
                    if (edit_mode) target_sketch.update_text(std::move(committed));
                    else target_sketch.add_text(std::move(committed));
                })) throw std::runtime_error("Sketch no longer exists");
            selected_sketch_text_id_ = committed_id;
            state_->setText(edit_mode
                ? tr("Text skici byl upraven jako jedna revize.")
                : tr("Text skici byl vytvořen jako jedna revize."));
        }, this, sketch->drawing_template.has_value());
    properties_dialog_ = dialog;
    sketch_text_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
        if (sketch_text_dialog_ == dialog) sketch_text_dialog_ = nullptr;
        sketch_text_active_ = false;
        editing_sketch_text_id_.clear();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
    });
    preserve_view_on_refresh_ = true;
    refresh_scene();
    dialog->show();
    state_->setText(edit_mode
        ? tr("Upravte text; OK změnu uloží a Cancel obnoví původní stav.")
        : tr("Text skici: klikněte na polohu, potom potvrďte OK."));
}

} // namespace zima::app

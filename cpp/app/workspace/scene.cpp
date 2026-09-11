#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::refresh_scene() {
    workspace_.refresh_source_geometry();
    if(measure_action_)measure_action_->setEnabled(workspace_.open_part(workspace_.displayed_document_id())||workspace_.open_assembly(workspace_.displayed_document_id()));
    viewer_->set_dimension_layout_editable((!properties_dialog_||!properties_dialog_->isVisible())&&!sketch_universal_dimension_active_);
    update_assembly_dimension_visibility();
    update_viewer_body_colors();
    update_body_color_actions();
    viewer_->set_active_sketch_owner(active_sketch_id_);
    QScopedValueRollback refreshing_guard(refreshing_scene_, true);
    set_selected_component_origin({});
    update_document_area_visibility();
    tree_->setRootIndex(QModelIndex{});
    tree_->clear();
    update_document_kind_button();
    viewer_->set_transient_point_transform({});
    if (const auto* sketch = active_sketch()) {
        if (const auto* body = sketch_body(*sketch)) {
            const auto frame = container_dimension_frame(body->scope.placement);
            viewer_->set_transient_point_transform([frame](const auto& point) {
                return frame.point(point);
            });
        }
    }
    if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        if (const auto* body = part->session.document().body_history.find(sketch_properties_body_id_)) {
            const auto frame = container_dimension_frame(body->scope.placement);
            viewer_->set_transient_point_transform([frame](const auto& point) { return frame.point(point); });
        }
    }
    int decimal_places = 3;
    if (const auto* active_part =
            workspace_.open_part(workspace_.active_document_id())) {
        decimal_places = document_decimal_places(
            active_part->session.document());
    } else if (const auto* active_assembly =
            workspace_.open_assembly(workspace_.active_document_id())) {
        decimal_places = document_decimal_places(
            active_assembly->session.document());
    }
    setProperty("zimaDocumentDecimalPlaces",decimal_places);
    viewer_->set_dimension_decimal_places(decimal_places);
    const auto construction_mesh = [this](const auto& document, double scene_size,
            const zima::kernel::ViewerReferenceGeometry& reference_geometry) {
        auto mesh = construction_preview_mesh_.has_value()
            ? *construction_preview_mesh_
            : document.construction_viewer_mesh({}, scene_size);
        if constexpr (requires { document.history; }) {
            if (body_dialog_preview_ && primitive_reference_dialog_) {
                if (const auto* body = body_dialog_preview_->body_history.find(body_dialog_step_id_)) {
                    append_nonzero_parameter_dimensions(mesh.dimensions,
                        zima::document::container_placement_dimensions(body->scope.id,
                            body->scope.placement, primitive_reference_geometry_));
                }
            }
            if (!body_dialog_preview_) {
                if (const auto* body=document.body_history.find(construction_dimension_object_id_))
                    append_nonzero_parameter_dimensions(mesh.dimensions,
                        zima::document::container_placement_dimensions(body->scope.id,
                            body->scope.placement, reference_geometry));
            }
            std::unordered_set<std::string> visible_ids;
            for (std::size_t i = 0; i < std::min(document.effective_history_cursor(), document.history_order.size()); ++i)
                visible_ids.insert(document.history_order[i].id);
            for (std::size_t i = 0; i < document.history.size(); ++i) {
                if (part_rollback_ && part_rollback_->part_document_id == document.document_id &&
                    i >= part_rollback_->history_limit) break;
                const auto& feature = document.history[i];
                if (feature.feature_kind != zima::document::FeatureKind::Sweep3D ||
                    feature.suppressed || (!document.history_order.empty() && !visible_ids.contains(feature.id))) continue;
                // The live path already supplies pending geometry and annotations.
                if (construction_parameter_preview_ &&
                    construction_parameter_preview_->id == feature.sweep3d.path.id) continue;
                zima::document::PartDocument path_display;
                path_display.constructions.push_back(sweep_display_path(feature));
                auto path_mesh = path_display.construction_viewer_mesh();
                const bool has_solid_centerline = std::ranges::any_of(reference_geometry.edges,
                    [&](const auto& edge) {
                        return edge.reference.owner_id == feature.id &&
                            edge.reference.semantic_key.starts_with("centerline:from:");
                    });
                for (auto& edge : path_mesh.edges) {
                    // A calculated solid displays its persisted axis. The editable
                    // source curve remains available in the feature preview.
                    if (has_solid_centerline ||
                        !zima::viewer::is_curve3d_edge(edge.reference.semantic_key)) continue;
                    edge.display_owner_id = feature.id;
                    mesh.edges.push_back(std::move(edge));
                }
                if (construction_dimension_object_id_ == feature.id) {
                    const auto radii = zima::document::curve3d_radius_dimensions(
                        path_display.constructions.front());
                    mesh.dimensions.insert(mesh.dimensions.end(), radii.begin(), radii.end());
                }
            }
        }
        const auto* stored_object = document.find_construction(
            construction_dimension_object_id_);
        const bool construction_properties_preview =
            construction_parameter_preview_ &&
                construction_parameter_preview_->id ==
                    construction_dimension_object_id_;
        const auto* object = construction_properties_preview
            ? &*construction_parameter_preview_ : stored_object;
        if (object && object->kind == zima::document::ConstructionKind::Curve3D &&
            !construction_preview_mesh_) {
            const auto radii = zima::document::curve3d_radius_dimensions(*object);
            mesh.dimensions.insert(mesh.dimensions.end(), radii.begin(), radii.end());
        }
        const auto append_dimension = [&](const std::string& owner,
                const char* key, const char* label,
                zima::kernel::Vec3 witness_first,
                zima::kernel::Vec3 witness_second,
                zima::kernel::Vec3 offset, double value) {
            if (std::abs(value) <= visible_parameter_dimension_epsilon) return;
            // Keep every non-degenerate linear dimension in one modeling
            // plane.  The measured segment supplies the first in-plane
            // direction; project the preferred offset perpendicular to it so
            // both witness lines are true perpendiculars.  This also gives a
            // stable plane for an arbitrary point-to-point measurement.
            const zima::kernel::Vec3 measured{
                witness_second.x - witness_first.x,
                witness_second.y - witness_first.y,
                witness_second.z - witness_first.z};
            const auto dot = [](const zima::kernel::Vec3& left,
                                 const zima::kernel::Vec3& right) {
                return left.x * right.x + left.y * right.y +
                    left.z * right.z;
            };
            const double measured_squared = dot(measured, measured);
            const double requested_offset_length = std::sqrt(dot(offset, offset));
            if (measured_squared > 1.0e-18 &&
                requested_offset_length > 1.0e-9) {
                const double parallel = dot(offset, measured) / measured_squared;
                offset = {offset.x - measured.x * parallel,
                    offset.y - measured.y * parallel,
                    offset.z - measured.z * parallel};
                double perpendicular_length = std::sqrt(dot(offset, offset));
                if (perpendicular_length <= 1.0e-9) {
                    const std::array basis{
                        zima::kernel::Vec3{1.0, 0.0, 0.0},
                        zima::kernel::Vec3{0.0, 1.0, 0.0},
                        zima::kernel::Vec3{0.0, 0.0, 1.0}};
                    const auto fallback = *std::min_element(
                        basis.begin(), basis.end(), [&](const auto& left,
                            const auto& right) {
                            return std::abs(dot(left, measured)) <
                                std::abs(dot(right, measured));
                        });
                    const double fallback_parallel =
                        dot(fallback, measured) / measured_squared;
                    offset = {fallback.x - measured.x * fallback_parallel,
                        fallback.y - measured.y * fallback_parallel,
                        fallback.z - measured.z * fallback_parallel};
                    perpendicular_length = std::sqrt(dot(offset, offset));
                }
                const double scale = requested_offset_length /
                    perpendicular_length;
                offset = {offset.x * scale, offset.y * scale, offset.z * scale};
            }
            mesh.dimensions.push_back({witness_first, witness_second,
                {witness_first.x + offset.x, witness_first.y + offset.y,
                    witness_first.z + offset.z},
                {witness_second.x + offset.x, witness_second.y + offset.y,
                    witness_second.z + offset.z}, value,
                {owner, std::string("parameter:") + key, {}}, {}});
            if (measured_squared > 1.0e-18) {
                const zima::kernel::Vec3 normal{
                    measured.y * offset.z - measured.z * offset.y,
                    measured.z * offset.x - measured.x * offset.z,
                    measured.x * offset.y - measured.y * offset.x};
                const double normal_length = std::sqrt(dot(normal, normal));
                if (normal_length > 1.0e-9) {
                    mesh.dimensions.back().plane_normal = {
                        normal.x / normal_length,
                        normal.y / normal_length,
                        normal.z / normal_length};
                }
            }
            static_cast<void>(label);
        };
        if (object != nullptr) {
            zima::document::Placement placement;
            placement.x = object->origin.x;
            placement.y = object->origin.y;
            placement.z = object->origin.z;
            placement.rotation_x = object->rotation.x;
            placement.rotation_y = object->rotation.y;
            placement.rotation_z = object->rotation.z;
            placement.absolute_rotation_x = object->absolute_rotation.x;
            placement.absolute_rotation_y = object->absolute_rotation.y;
            placement.absolute_rotation_z = object->absolute_rotation.z;
            placement.rotation_offset_x = object->rotation_offset_x;
            placement.rotation_offset_y = object->rotation_offset_y;
            placement.rotation_offset_z = object->rotation_offset_z;
            placement.orientation_back = object->orientation_back;
            placement.orientation_quarter_turns =
                object->orientation_quarter_turns;
            placement.references = object->references;
            auto dimensions =
                zima::document::container_placement_dimensions(
                    object->id, placement, construction_properties_preview
                        ? construction_reference_geometry_ : reference_geometry);
            append_nonzero_parameter_dimensions(
                mesh.dimensions, std::move(dimensions));
            if (object->kind == zima::document::ConstructionKind::Axis) {
                const double half = object->display_size * 0.5;
                const auto start = zima::kernel::Vec3{
                    object->origin.x - object->direction.x * half,
                    object->origin.y - object->direction.y * half,
                    object->origin.z - object->direction.z * half};
                const auto end = zima::kernel::Vec3{
                    object->origin.x + object->direction.x * half,
                    object->origin.y + object->direction.y * half,
                    object->origin.z + object->direction.z * half};
                append_dimension(object->id, "length", "L = ",
                    start, end, {5, 5, 0}, object->display_size);
            } else if (object->kind ==
                       zima::document::ConstructionKind::Plane) {
                if (std::abs(object->offset) > 1.0e-12) {
                    append_dimension(object->id, "offset", "Odsazení = ",
                        object->origin, object->entity_origin, {5, 5, 0},
                        object->offset);
                }
            }
        }
        if constexpr (requires { document.history; }) {
            const auto stored_container = std::find_if(document.history.begin(),
                document.history.end(), [&](const auto& value) {
                    return value.id == construction_dimension_object_id_;
                });
            const zima::document::HistoryContainer* container =
                parameter_dimension_preview_ &&
                    parameter_dimension_preview_->id ==
                        construction_dimension_object_id_
                ? &*parameter_dimension_preview_
                : stored_container != document.history.end()
                    ? &*stored_container : nullptr;
            if (container != nullptr) {
                if(active_sketch_id_.empty())zima::document::visit_feature_sketches(*container,[&](const auto& data,std::size_t){
                    const auto sketch=zima::sketcher::Sketch::from_serialized(data);
                    auto display=sketch.viewer_mesh();
                    const auto* body=document.body_owner_for_object(container->id);
                    const auto body_id=body?body->scope.id:document.body_history.active_body_id();
                    if(!body_id.empty())display=document.place_body_mesh(std::move(display),body_id);
                    mesh.dimensions.insert(mesh.dimensions.end(),display.dimensions.begin(),display.dimensions.end());
                });
                const auto placement_reference_geometry =
                    parameter_dimension_preview_ &&
                            parameter_dimension_preview_->id == container->id
                        ? primitive_reference_geometry_
                        : document.construction_reference_geometry_for(
                              container->id, reference_geometry);
                auto placement_dimensions =
                    zima::document::container_placement_dimensions(
                        container->id, container->placement,
                        placement_reference_geometry);
                if (!parameter_dimension_preview_) {
                    if (const auto* body = document.body_owner_for_object(container->id)) {
                        zima::kernel::ViewerMesh display;
                        display.dimensions = std::move(placement_dimensions);
                        placement_dimensions = document.place_body_mesh(
                            std::move(display), body->scope.id).dimensions;
                    }
                }
                append_nonzero_parameter_dimensions(
                    mesh.dimensions, std::move(placement_dimensions));
                const auto origin = zima::kernel::Vec3{
                    container->placement.x, container->placement.y,
                    container->placement.z};
                // Feature dimensions are authored only in local coordinates;
                // this shared frame is their sole conversion into View space.
                const auto dimension_frame =
                    container_dimension_frame(container->placement);
                const auto local = [&](double x, double y, double z) {
                    return dimension_frame.point({x, y, z});
                };
                const auto local_vector = [&](zima::kernel::Vec3 value) {
                    return dimension_frame.vector(value);
                };
                const auto linear = [&](const char* key, const char* label,
                        zima::kernel::Vec3 first, zima::kernel::Vec3 second,
                        zima::kernel::Vec3 offset, double value) {
                    append_dimension(container->id, key, label, first, second,
                        local_vector(offset), value);
                };
                const auto radius = [&](const char* key,
                        zima::kernel::Vec3 center, zima::kernel::Vec3 rim,
                        zima::kernel::Vec3 offset, double value) {
                    if (std::abs(value) <=
                            visible_parameter_dimension_epsilon) return;
                    append_dimension(container->id, key, "", center, rim,
                        local_vector(offset), value);
                    mesh.dimensions.back().kind =
                        zima::kernel::ViewerDimensionKind::Radius;
                };
                using zima::document::FeatureKind;
                if (container->feature_kind == FeatureKind::Box) {
                    const double x = container->box.length * 0.5;
                    const double y = container->box.width * 0.5;
                    const double z = container->box.height * 0.5;
                    linear("length", "Délka = ", local(-x,-y,-z), local(x,-y,-z),
                        {0,-8,0}, container->box.length);
                    linear("width", "Šířka = ", local(-x,-y,-z), local(-x,y,-z),
                        {-8,0,0}, container->box.width);
                    linear("height", "Výška = ", local(-x,-y,-z), local(-x,-y,z),
                        {-8,0,0}, container->box.height);
                } else if (container->feature_kind == FeatureKind::Cylinder) {
                    radius("radius", origin,
                        local(container->cylinder.radius,0,0), {0,6,0},
                        container->cylinder.radius);
                    linear("height", "Výška = ", origin,
                        local(0,0,container->cylinder.height), {8,0,0},
                        container->cylinder.height);
                } else if (container->feature_kind == FeatureKind::ShaftThread) {
                    try {
                        const auto preview=shaft_thread_preview(*container,placement_reference_geometry);
                        mesh.dimensions.insert(mesh.dimensions.end(),preview.dimensions.begin(),preview.dimensions.end());
                    } catch (const std::exception&) { /* Incomplete reference entry has no dimension. */ }
                } else if (container->feature_kind == FeatureKind::Thread) {
                    const double diameter=container->thread.enabled
                        ? container->thread.profile_diameter : container->thread.nominal_diameter;
                    const bool referenced_work_plane = std::any_of(
                        container->placement.references.begin(),
                        container->placement.references.end(),
                        [](const auto& reference) {
                            return !reference.orientation_only &&
                                reference.supports_offset &&
                                !reference.owner_id.empty();
                        });
                    const auto axial = [&](double distance) {
                        if (container->thread.direction == zima::document::ExtrusionDirection::Reverse)
                            distance = -distance;
                        return referenced_work_plane
                            ? local(0,-distance,0)
                            : local(0,0,distance);
                    };
                    const auto diameter_dimension = [&](const char* key, double size,
                            double depth, const std::string& text) {
                        const auto center = axial(depth);
                        const auto radial = local_vector({size*0.5,0,0});
                        const zima::kernel::Vec3 rim{center.x+radial.x,
                            center.y+radial.y,center.z+radial.z};
                        mesh.dimensions.push_back({center, rim, center, rim, size,
                            {container->id,std::string("parameter:")+key,{}},""});
                        auto& dimension=mesh.dimensions.back();
                        dimension.kind=zima::kernel::ViewerDimensionKind::Diameter;
                        dimension.label_prefix="⌀ ";
                        const auto axis_point=axial(1.0);
                        dimension.plane_normal={axis_point.x-origin.x,
                            axis_point.y-origin.y,axis_point.z-origin.z};
                        dimension.display_text_override=text;
                    };
                    const double resolved_length=zima::document::PartDocument::thread_length(*container);
                    const double display_length=std::isfinite(resolved_length) && resolved_length>0
                        ? resolved_length : container->thread.length_forward;
                    const double bore_start=container->thread.chamfer_enabled
                        ? container->thread.chamfer_depth : 0.0;
                    diameter_dimension("bore_diameter", diameter,
                        container->thread.enabled
                            ? std::max(bore_start,display_length*0.5)
                            : bore_start, "");
                    if (container->thread.enabled) {
                        auto& bore_dimension=mesh.dimensions.back();
                        // Opposite leaders keep bore and thread labels distinct
                        // even when looking straight down the opening axis.
                        const auto center=bore_dimension.witness_first;
                        const auto rim=bore_dimension.witness_second;
                        bore_dimension.witness_second={2*center.x-rim.x,
                            2*center.y-rim.y,2*center.z-rim.z};
                        bore_dimension.line_second=bore_dimension.witness_second;
                        bore_dimension.driving=false;
                        bore_dimension.reference.semantic_key="measurement:bore_diameter";
                        diameter_dimension("thread_designation",
                            container->thread.nominal_diameter,
                            display_length,
                            container->thread.designation);
                        if (container->thread.length_end_condition == zima::document::EndCondition::Length)
                        linear("thread_length", "Délka závitu = ", origin,
                            axial(container->thread.length_forward), {8,8,0},
                            container->thread.length_forward);
                    }
                    if (container->thread.end_condition_forward == zima::document::EndCondition::Length) {
                        linear("bore_length", "Hloubka otvoru = ", origin,
                            axial(container->thread.bore_length), {14,14,0},
                            container->thread.bore_length);
                    }
                    const auto cone_angle = [&](const char* key, double degrees,
                            double rim_depth, double rim_radius) {
                        auto angle=opening_cone_angle_dimension(container->id, key,
                            degrees,rim_depth,rim_radius);
                        if (!angle) return;
                        const auto radial=local_vector({1,0,0});
                        const auto along=axial(1.0);
                        const zima::kernel::Vec3 axis{along.x-origin.x,
                            along.y-origin.y,along.z-origin.z};
                        const auto point=[&](const zima::kernel::Vec3& p) {
                            const auto center=axial(p.y);
                            return zima::kernel::Vec3{center.x+radial.x*p.x,
                                center.y+radial.y*p.x,center.z+radial.z*p.x};
                        };
                        angle->witness_first=point(angle->witness_first);
                        angle->witness_second=point(angle->witness_second);
                        angle->line_first=point(angle->line_first);
                        angle->line_second=point(angle->line_second);
                        if (angle->label_position)
                            angle->label_position=point(*angle->label_position);
                        angle->plane_normal={radial.y*axis.z-radial.z*axis.y,
                            radial.z*axis.x-radial.x*axis.z,
                            radial.x*axis.y-radial.y*axis.x};
                        mesh.dimensions.push_back(std::move(*angle));
                    };
                    if (container->thread.chamfer_enabled) {
                        linear("chamfer_depth", "Sražení = ", origin,
                            axial(container->thread.chamfer_depth), {-10,0,0},
                            container->thread.chamfer_depth);
                        const double mouth_radius=diameter*0.5+
                            container->thread.chamfer_depth*std::tan(
                                container->thread.chamfer_angle_degrees*std::numbers::pi/360.0);
                        cone_angle("chamfer_angle", container->thread.chamfer_angle_degrees,
                            0.0, mouth_radius);
                    }
                    if (container->hole.drill_point_enabled &&
                        container->thread.end_condition_forward == zima::document::EndCondition::Length)
                        cone_angle("drill_point_angle", container->hole.drill_point_angle_degrees,
                            container->thread.bore_length, diameter*0.5);
                    if (properties_dialog_==nullptr && opening_component_edit_.first==container->id &&
                        !opening_component_edit_.second.empty()) {
                        const auto& component=opening_component_edit_.second;
                        std::erase_if(mesh.dimensions,[&](const auto& dimension) {
                            if (dimension.reference.owner_id!=container->id) return false;
                            const auto& key=dimension.reference.semantic_key;
                            if (component=="bore") return key!="parameter:bore_length" &&
                                key!="parameter:bore_diameter" && key!="measurement:bore_diameter";
                            if (component=="thread") return key!="parameter:thread_length" &&
                                key!="parameter:thread_designation";
                            if (component=="chamfer") return !key.starts_with("parameter:chamfer_");
                            if (component=="tip") return key!="parameter:drill_point_angle";
                            return false;
                        });
                    }
                } else if (container->feature_kind == FeatureKind::Sphere) {
                    radius("radius", origin,
                        local(container->sphere.radius,0,0), {0,6,0},
                        container->sphere.radius);
                } else if (container->feature_kind == FeatureKind::Cone) {
                    radius("bottom_radius", origin,
                        local(container->cone.bottom_radius,0,0), {0,-8,0},
                        container->cone.bottom_radius);
                    radius("top_radius", local(0,0,container->cone.height),
                        local(container->cone.top_radius,0,container->cone.height),
                        {0,8,0}, container->cone.top_radius);
                    linear("height", "Výška = ", origin,
                        local(0,0,container->cone.height), {8,0,0},
                        container->cone.height);
                } else if (container->feature_kind == FeatureKind::Pyramid) {
                    linear("length", "Délka = ", origin,
                        local(container->pyramid.length,0,0), {0,-8,0},
                        container->pyramid.length);
                    linear("width", "Šířka = ", origin,
                        local(0,container->pyramid.width,0), {-8,0,0},
                        container->pyramid.width);
                    linear("height", "Výška = ", origin,
                        local(0,0,container->pyramid.height), {8,0,0},
                        container->pyramid.height);
                } else if (container->feature_kind == FeatureKind::Wedge) {
                    linear("length", "Délka = ", origin,
                        local(container->wedge.length,0,0), {0,-8,0},
                        container->wedge.length);
                    linear("width", "Šířka = ", origin,
                        local(0,container->wedge.width,0), {-8,0,0},
                        container->wedge.width);
                    linear("height", "Výška = ", origin,
                        local(0,0,container->wedge.height), {8,0,0},
                        container->wedge.height);
                    linear("top_offset", "Posun = ", origin,
                        local(container->wedge.top_offset,0,0), {0,8,0},
                        container->wedge.top_offset);
                } else if (container->feature_kind == FeatureKind::Extrusion) {
                    const auto sketch = std::find_if(document.sketches.begin(),
                        document.sketches.end(), [&](const auto& value) {
                            return value.id == container->extrusion.sketch_id;
                        });
                    if (sketch != document.sketches.end()) {
                        zima::kernel::Vec3 local_normal;
                        zima::kernel::Vec3 local_offset;
                        if (sketch->plane == zima::sketcher::SketchPlane::XY) {
                            local_normal = {0,0,1};
                            local_offset = {0,0,
                                container->extrusion.profile_plane_offset};
                        } else if (sketch->plane ==
                                zima::sketcher::SketchPlane::XZ) {
                            local_normal = {0,1,0};
                            local_offset = {0,
                                container->extrusion.profile_plane_offset,0};
                        } else {
                            local_normal = {1,0,0};
                            local_offset = {
                                container->extrusion.profile_plane_offset,0,0};
                        }
                        const auto start = local(
                            local_offset.x, local_offset.y, local_offset.z);
                        const auto normal_endpoint = local(
                            local_offset.x + local_normal.x,
                            local_offset.y + local_normal.y,
                            local_offset.z + local_normal.z);
                        zima::kernel::Vec3 direction{
                            normal_endpoint.x - start.x,
                            normal_endpoint.y - start.y,
                            normal_endpoint.z - start.z};
                        if (container->extrusion.direction ==
                                zima::document::ExtrusionDirection::Reverse) {
                            direction = {-direction.x, -direction.y, -direction.z};
                        }
                        const auto along = [&](double distance) {
                            return zima::kernel::Vec3{
                                start.x + direction.x * distance,
                                start.y + direction.y * distance,
                                start.z + direction.z * distance};
                        };
                        if (const auto length =
                                extrusion_length_dimension_value(
                                    container->extrusion, false);
                            !primitive_origin_preview_mesh_ && length) {
                            linear("length_forward", "Délka = ", start,
                                along(*length), {8,8,0}, *length);
                        }
                        if (const auto length =
                                extrusion_length_dimension_value(
                                    container->extrusion, true);
                            !primitive_origin_preview_mesh_ && length) {
                            linear(container->extrusion.extent_mode ==
                                    zima::document::ProfileExtentMode::Symmetric
                                    ? "length_forward" : "length_reverse",
                                "Délka 2 = ", start,
                                along(-*length), {-8,-8,0}, *length);
                        }
                        if (extrusion_profile_offset_dimension_value(
                                container->extrusion) && !primitive_origin_preview_mesh_) {
                            const zima::kernel::Vec3 front = sketch->plane ==
                                    zima::sketcher::SketchPlane::YZ
                                ? zima::kernel::Vec3{0,8,0}
                                : zima::kernel::Vec3{8,0,0};
                            linear("profile_offset", "Odsazení = ", origin, start,
                                front, container->extrusion.profile_plane_offset);
                        }
                    }
                } else if (container->feature_kind == FeatureKind::Revolution) {
                    const auto sketch = std::find_if(document.sketches.begin(),
                        document.sketches.end(), [&](const auto& value) {
                            return value.id == container->revolution.sketch_id;
                        });
                    if (sketch != document.sketches.end()) {
                        if (auto frame = revolution_cue_frame(*sketch,
                                container->revolution.axis_segment_id)) {
                            auto axis = frame->axis;
                            if (container->revolution.direction ==
                                    zima::document::ExtrusionDirection::Reverse) {
                                axis = {-axis.x, -axis.y, -axis.z};
                            }
                            const auto& vertex = frame->center;
                            // Keep the readable angular dimension just outside
                            // the purple rotation manipulator. Both arcs stay
                            // in the same operation plane without painting over
                            // one another.
                            constexpr double dimension_radius_scale = 1.28;
                            const zima::kernel::Vec3 radial{
                                frame->radial.x * dimension_radius_scale,
                                frame->radial.y * dimension_radius_scale,
                                frame->radial.z * dimension_radius_scale};
                            const auto ray = zima::kernel::Vec3{
                                vertex.x + radial.x,
                                vertex.y + radial.y,
                                vertex.z + radial.z};
                            const auto perpendicular = zima::kernel::Vec3{
                                axis.y * radial.z - axis.z * radial.y,
                                axis.z * radial.x - axis.x * radial.z,
                                axis.x * radial.y - axis.y * radial.x};
                            const auto append_angle_dimension =
                                [&](double degrees, double sign,
                                    const char* parameter_key) {
                                if (std::abs(degrees) <=
                                        visible_parameter_dimension_epsilon) {
                                    return;
                                }
                                const double signed_angle = degrees * sign;
                                const double angle = signed_angle *
                                    std::numbers::pi / 180.0;
                                const auto other = zima::kernel::Vec3{
                                    vertex.x + radial.x * std::cos(angle) +
                                        perpendicular.x * std::sin(angle),
                                    vertex.y + radial.y * std::cos(angle) +
                                        perpendicular.y * std::sin(angle),
                                    vertex.z + radial.z * std::cos(angle) +
                                        perpendicular.z * std::sin(angle)};
                                mesh.dimensions.push_back({vertex, vertex, ray, other,
                                    degrees,
                                    {container->id, parameter_key, {}},
                                    "", "°"});
                                auto& angle_dimension = mesh.dimensions.back();
                                angle_dimension.kind =
                                    zima::kernel::ViewerDimensionKind::Angular;
                                angle_dimension.plane_normal = axis;
                                angle_dimension.sweep_degrees = signed_angle;
                            };
                            append_angle_dimension(
                                container->revolution.angle_degrees, 1.0,
                                "parameter:angle");
                            if (container->revolution.extent_mode ==
                                    zima::document::ProfileExtentMode::TwoSides) {
                                append_angle_dimension(
                                    container->revolution.angle_reverse, -1.0,
                                    "parameter:length_reverse");
                            } else if (container->revolution.extent_mode ==
                                    zima::document::ProfileExtentMode::Symmetric) {
                                append_angle_dimension(
                                    container->revolution.angle_degrees, -1.0,
                                    "parameter:angle");
                            }
                        }
                        if (std::abs(container->revolution.profile_plane_offset) >
                                1.0e-12 && !primitive_origin_preview_mesh_) {
                            const auto start = sketch->resolved_origin;
                            const auto normal = sketch->resolved_normal;
                            const auto offset = container->revolution.profile_plane_offset;
                            const zima::kernel::Vec3 base{
                                start.x-normal.x*offset, start.y-normal.y*offset,
                                start.z-normal.z*offset};
                            const auto front = sketch->resolved_x_axis;
                            append_dimension(container->id, "profile_offset", "Odsazení = ",
                                base, start, {front.x*8,front.y*8,front.z*8}, offset);
                        }
                    }
                } else if (container->feature_kind == FeatureKind::Shell) {
                    // Shell thickness is anchored to an actual face of the
                    // persisted input body immediately before this feature.
                    // Prefer the first selected opening; a closed Shell uses
                    // the first stable input face. This is viewer data only
                    // and never asks OCCT to rebuild topology on inspection.
                    const auto* part_state =
                        workspace_.open_part(document.document_id);
                    std::size_t calculated_before{};
                    for (auto history = document.history.begin();
                         history != stored_container; ++history) {
                        if (history->feature_kind != FeatureKind::Sketch) {
                            ++calculated_before;
                        }
                    }
                    if (part_state != nullptr && calculated_before > 0 &&
                        part_state->session.calculated_boundaries().size() >=
                            calculated_before) {
                        const auto& input_mesh =
                            part_state->session.calculated_boundaries()
                                [calculated_before - 1].mesh;
                        std::optional<zima::kernel::FaceReference> anchor_face;
                        if (!container->shell.removed_faces.empty()) {
                            anchor_face = container->shell.removed_faces.front();
                        } else {
                            const auto first = std::ranges::find_if(
                                input_mesh.triangle_references,
                                [](const auto& reference) {
                                    return reference.valid();
                                });
                            if (first != input_mesh.triangle_references.end()) {
                                anchor_face = *first;
                            }
                        }
                        const auto same_face = [&](std::size_t triangle) {
                            return anchor_face &&
                                triangle <
                                    input_mesh.triangle_references.size() &&
                                input_mesh.triangle_references[triangle].
                                        owner_id == anchor_face->owner_id &&
                                input_mesh.triangle_references[triangle].
                                        semantic_key ==
                                            anchor_face->semantic_key;
                        };
                        std::optional<std::size_t> anchor_triangle;
                        for (std::size_t triangle = 0;
                             triangle < input_mesh.triangle_references.size();
                             ++triangle) {
                            if (same_face(triangle) &&
                                triangle * 3 + 2 <
                                    input_mesh.triangles.size()) {
                                anchor_triangle = triangle;
                                break;
                            }
                        }
                        if (anchor_triangle && !input_mesh.vertices.empty()) {
                            const auto first_index =
                                input_mesh.triangles[*anchor_triangle * 3];
                            const auto second_index =
                                input_mesh.triangles[*anchor_triangle * 3 + 1];
                            const auto third_index =
                                input_mesh.triangles[*anchor_triangle * 3 + 2];
                            if (first_index < input_mesh.vertices.size() &&
                                second_index < input_mesh.vertices.size() &&
                                third_index < input_mesh.vertices.size()) {
                                const auto& first =
                                    input_mesh.vertices[first_index];
                                const auto& second =
                                    input_mesh.vertices[second_index];
                                const auto& third =
                                    input_mesh.vertices[third_index];
                                const zima::kernel::Vec3 edge_one{
                                    second.x - first.x,
                                    second.y - first.y,
                                    second.z - first.z};
                                const zima::kernel::Vec3 edge_two{
                                    third.x - first.x,
                                    third.y - first.y,
                                    third.z - first.z};
                                zima::kernel::Vec3 normal{
                                    edge_one.y * edge_two.z -
                                        edge_one.z * edge_two.y,
                                    edge_one.z * edge_two.x -
                                        edge_one.x * edge_two.z,
                                    edge_one.x * edge_two.y -
                                        edge_one.y * edge_two.x};
                                const double normal_length = std::hypot(
                                    std::hypot(normal.x, normal.y), normal.z);
                                if (normal_length > 1.0e-12) {
                                    normal = {
                                        normal.x / normal_length,
                                        normal.y / normal_length,
                                        normal.z / normal_length};
                                    zima::kernel::Vec3 lower =
                                        input_mesh.vertices.front();
                                    zima::kernel::Vec3 upper = lower;
                                    for (const auto& vertex :
                                         input_mesh.vertices) {
                                        lower.x = std::min(lower.x, vertex.x);
                                        lower.y = std::min(lower.y, vertex.y);
                                        lower.z = std::min(lower.z, vertex.z);
                                        upper.x = std::max(upper.x, vertex.x);
                                        upper.y = std::max(upper.y, vertex.y);
                                        upper.z = std::max(upper.z, vertex.z);
                                    }
                                    const zima::kernel::Vec3 face_point{
                                        (first.x + second.x + third.x) / 3.0,
                                        (first.y + second.y + third.y) / 3.0,
                                        (first.z + second.z + third.z) / 3.0};
                                    const zima::kernel::Vec3 body_center{
                                        (lower.x + upper.x) * 0.5,
                                        (lower.y + upper.y) * 0.5,
                                        (lower.z + upper.z) * 0.5};
                                    const double toward_body =
                                        normal.x *
                                            (body_center.x - face_point.x) +
                                        normal.y *
                                            (body_center.y - face_point.y) +
                                        normal.z *
                                            (body_center.z - face_point.z);
                                    if (toward_body < 0.0) {
                                        normal = {-normal.x, -normal.y,
                                            -normal.z};
                                    }
                                    const zima::kernel::Vec3 inner_point{
                                        face_point.x + normal.x *
                                            container->shell.thickness,
                                        face_point.y + normal.y *
                                            container->shell.thickness,
                                        face_point.z + normal.z *
                                            container->shell.thickness};
                                    linear("thickness", "Tloušťka = ",
                                        face_point, inner_point, {8,8,8},
                                        container->shell.thickness);
                                }
                            }
                        }
                    }
                } else if (container->feature_kind == FeatureKind::Fillet ||
                           container->feature_kind == FeatureKind::Chamfer) {
                    // Anchor the inspection dimension to the first selected
                    // operational edge at this feature's real input boundary.
                    // The adjacent-face directions were persisted by the
                    // explicit body calculation, so opening/double-clicking
                    // the feature never invokes OCCT or enumerates topology.
                    const auto* part_state =
                        workspace_.open_part(document.document_id);
                    std::size_t calculated_before{};
                    for (auto history = document.history.begin();
                         history != stored_container; ++history) {
                        if (history->feature_kind != FeatureKind::Sketch) {
                            ++calculated_before;
                        }
                    }
                    const auto& routes = container->edge_treatment.routes;
                    // The open feature dialog already publishes the correct
                    // transient route dimensions. Do not stack a second
                    // persisted inspection set on top of it.
                    if (!(edge_treatment_dialog_ != nullptr &&
                            edge_treatment_preview_owner_id_ == container->id) &&
                        part_state != nullptr && calculated_before > 0 &&
                        part_state->session.calculated_boundaries().size() >=
                            calculated_before &&
                        !routes.empty() && !routes.front().empty()) {
                        const auto& input_mesh =
                            part_state->session.calculated_boundaries()
                                [calculated_before - 1].mesh;
                        std::vector<zima::kernel::ViewerEdge> route_edges;
                        route_edges.reserve(routes.front().size());
                        bool complete = true;
                        for (const auto& reference : routes.front()) {
                            const auto edge = std::find_if(input_mesh.edges.begin(),
                                input_mesh.edges.end(), [&](const auto& value) {
                                    return value.reference.owner_id ==
                                            reference.owner_id &&
                                        value.reference.semantic_key ==
                                            reference.semantic_key &&
                                        value.reference.instance_path ==
                                            reference.instance_path;
                                });
                            if (edge == input_mesh.edges.end()) {
                                complete = false;
                                break;
                            }
                            route_edges.push_back(*edge);
                        }
                        // An incomplete route would place R2 at an internal
                        // joint and present a geometrically false dimension.
                        if (complete && !route_edges.empty()) {
                            auto geometry = edge_treatment_preview_wire(
                                {std::move(route_edges)}, container->edge_treatment,
                                container->feature_kind,
                                container->id);
                            mesh.dimensions.insert(mesh.dimensions.end(),
                                std::make_move_iterator(
                                    geometry.dimensions.begin()),
                                std::make_move_iterator(
                                    geometry.dimensions.end()));
                        }
                    }
                }
            }
        }
        if constexpr (requires { document.cuts; }) {
            const auto cut = std::find_if(document.cuts.begin(),
                document.cuts.end(), [&](const auto& value) {
                    return value.definition.id ==
                        construction_dimension_object_id_;
                });
            if (cut != document.cuts.end()) {
                const auto& definition = cut->definition;
                using zima::document::FeatureKind;
                if (definition.feature_kind == FeatureKind::Extrusion) {
                    const auto sketch = std::find_if(document.sketches.begin(),
                        document.sketches.end(), [&](const auto& value) {
                            return value.id == definition.extrusion.sketch_id;
                        });
                    if (sketch != document.sketches.end()) {
                        const auto start = sketch->resolved_origin;
                        if (const auto length =
                                extrusion_length_dimension_value(
                                    definition.extrusion, false);
                            !primitive_origin_preview_mesh_ && length) {
                            auto direction = sketch->resolved_normal;
                            if (definition.extrusion.direction ==
                                    zima::document::ExtrusionDirection::Reverse) {
                                direction = {-direction.x, -direction.y,
                                    -direction.z};
                            }
                            const auto end = zima::kernel::Vec3{
                                start.x + direction.x * *length,
                                start.y + direction.y * *length,
                                start.z + direction.z * *length};
                            append_dimension(definition.id, "length_forward", "Délka = ",
                                start, end, {8,8,0},
                                *length);
                        }
                    }
                } else if (definition.feature_kind == FeatureKind::Revolution) {
                    // Assembly cuts share the same editable angular value;
                    // keep its inspection dimension independent of OCCT body
                    // topology just like Part history containers.
                    if (std::abs(definition.revolution.angle_degrees) >
                            visible_parameter_dimension_epsilon) {
                        zima::kernel::ViewerDimension angle{
                            {definition.placement.x, definition.placement.y,
                             definition.placement.z},
                            {definition.placement.x, definition.placement.y,
                             definition.placement.z},
                            {definition.placement.x + 25.0, definition.placement.y,
                             definition.placement.z},
                            {definition.placement.x,
                             definition.placement.y + 25.0,
                             definition.placement.z},
                            definition.revolution.angle_degrees,
                            {definition.id, "parameter:angle", {}}, "", "°"};
                        angle.kind = zima::kernel::ViewerDimensionKind::Angular;
                        angle.sweep_degrees =
                            definition.revolution.angle_degrees;
                        mesh.dimensions.push_back(std::move(angle));
                    }
                } else if (definition.feature_kind == FeatureKind::Fillet ||
                           definition.feature_kind == FeatureKind::Chamfer) {
                    const zima::kernel::Vec3 start{
                        definition.placement.x, definition.placement.y,
                        definition.placement.z};
                    append_dimension(definition.id, "primary",
                        definition.feature_kind == FeatureKind::Fillet
                            ? "Poloměr = " : "Vzdálenost = ",
                        start, {start.x +
                                    definition.edge_treatment.primary_size,
                                start.y, start.z},
                        {0,6,0}, definition.edge_treatment.primary_size);
                }
            }
        }
        if constexpr (requires { document.sketches; }) if(active_sketch_id_.empty()) {
            const auto sketch = std::find_if(document.sketches.begin(),
                document.sketches.end(), [&](const auto& value) {
                    return value.id == construction_dimension_object_id_ ||
                        value.owner_container_id == construction_dimension_object_id_;
                });
            // Sketch Properties already supplies its pending annotations
            // in the placed preview mesh. Do not overlay old stored values.
            if (sketch != document.sketches.end() && sketch->id != sketch_properties_preview_id_) {
                const auto& shown = property_owned_sketch_draft_ && property_owned_sketch_draft_->id == sketch->id
                    ? *property_owned_sketch_draft_ : *sketch;
                const auto sketch_mesh = shown.viewer_mesh();
                mesh.dimensions.insert(mesh.dimensions.end(),
                    sketch_mesh.dimensions.begin(), sketch_mesh.dimensions.end());
                const auto base = zima::kernel::Vec3{
                    sketch->resolved_origin.x - sketch->resolved_normal.x *
                        sketch->plane_offset,
                    sketch->resolved_origin.y - sketch->resolved_normal.y *
                        sketch->plane_offset,
                    sketch->resolved_origin.z - sketch->resolved_normal.z *
                        sketch->plane_offset};
                // Persisted Sketch inspection uses this generic dimension.
                // An open Sketch/Extrusion/Revolution Properties window
                // publishes its own live plane-aligned offset dimension in
                // primitive_origin_preview_mesh_. Drawing both produced the
                // two identical 5 mm dimensions seen after returning from
                // Sketcher.
                const bool offset_already_shown = std::ranges::any_of(
                    mesh.dimensions, [&](const auto& dimension) {
                        return dimension.reference.owner_id == sketch->owner_container_id &&
                            dimension.reference.semantic_key == "parameter:profile_offset";
                    });
                if (std::abs(sketch->plane_offset) > 1.0e-12 &&
                    !primitive_origin_preview_mesh_ && !offset_already_shown) {
                    // One offset annotation for the owned profile. Its witness
                    // follows the persisted Sketch X axis, so the dimension
                    // lies in the profile's X/normal plane even after rotation.
                    const auto front = sketch->resolved_x_axis;
                    append_dimension(sketch->owner_container_id,
                        "profile_offset", "Odsazení = ", base,
                        sketch->resolved_origin,
                        {front.x * 8.0, front.y * 8.0, front.z * 8.0},
                        sketch->plane_offset);
                }
            }
        }
        if (construction_reference_dialog_ && construction_properties_preview)
            construction_reference_dialog_->filter_parameter_dimensions(mesh.dimensions);
        if (sweep_profile_parent_dialog_ && sweep_profile_sketch_draft_ &&
            sweep_profile_sketch_draft_->id == active_sketch_id_) {
            // Station profiles are a Properties preview, not Sketcher
            // geometry. Keep the path as context, but show only the active
            // Sketch's own curves while editing it.
            const auto profile_preview = [](const auto& item) {
                return item.reference.semantic_key.starts_with("curve:profile:") ||
                    item.reference.semantic_key.starts_with("profile-point:");
            };
            std::erase_if(mesh.edges, profile_preview);
            std::erase_if(mesh.points, profile_preview);
            std::erase_if(mesh.constraint_markers, profile_preview);
        }
        return mesh;
    };
    if (workspace_.size() == 0) {
        workspace_stack_->setCurrentWidget(model_workspace_);
        tree_->setHeaderLabels({tr("DÍL")});
        viewer_->set_mesh({});
        close_document_action_->setEnabled(false);
        save_action_->setEnabled(false);
        save_as_action_->setEnabled(false);
        regenerate_document_action_->setEnabled(false);
        undo_action_->setEnabled(false);
        redo_action_->setEnabled(false);
        sketch_constraints_action_->setEnabled(false);
        sketch_dimensions_action_->setEnabled(false);
        sketch_text_action_->setEnabled(false);
        sketch_external_reference_action_->setEnabled(false);
        sketch_external_profile_action_->setEnabled(false);
        rebuild_application_toolbar();
        return;
    }
    if (auto* drawing =
            workspace_.open_drawing(workspace_.displayed_document_id())) {
        workspace_stack_->setCurrentWidget(drawing_workspace_);
        drawing_workspace_->edit_workspace_document(drawing->document().document_id);
        refresh_drawing_tree();
        active_application_ = ApplicationMode::Drawing;
        insert_action_->setEnabled(false);
        regenerate_action_->setEnabled(false);
        for (auto* action : {box_action_, cylinder_action_, thread_action_, shaft_thread_action_, drill_point_action_, sphere_action_, cone_action_,
                             pyramid_action_, wedge_action_, construction_point_action_,
                             curve_3d_action_,
                             sweep_3d_action_, helical_sweep_action_, sweep2d_action_,
                             construction_axis_action_, construction_plane_action_,
                             sketch_action_, extrusion_action_, revolution_action_,
                             fillet_action_, chamfer_action_, shell_action_,
                             regenerate_part_action_,
                             sketch_normal_view_action_,
                             sketch_flip_view_action_, sketch_rotate_view_action_,
                             sketch_external_reference_action_,
                             sketch_external_profile_action_, sketch_point_action_,
                             sketch_construction_action_, sketch_segment_action_,
                             sketch_polyline_action_,
                             sketch_rectangle_action_, sketch_polygon_action_,
                             sketch_trim_action_,
                             sketch_mirror_action_, sketch_offset_action_,
                             sketch_circle_action_,
                             sketch_arc_action_, sketch_ellipse_action_,
                             sketch_elliptical_arc_action_,
                             sketch_bspline_action_,
                             sketch_interpolating_spline_action_, sketch_text_action_,
                             sketch_constraints_action_,
                             sketch_dimensions_action_, finish_sketch_action_}) {
            action->setEnabled(false);
        }
        save_action_->setEnabled(true);
        save_as_action_->setEnabled(true);
        close_document_action_->setEnabled(true);
        regenerate_document_action_->setEnabled(true);
        undo_action_->setEnabled(false);
        redo_action_->setEnabled(false);
        update_application_actions();
        rebuild_application_toolbar();
        return;
    }
    workspace_stack_->setCurrentWidget(model_workspace_);
    const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) {
        const auto* part = workspace_.open_part(workspace_.displayed_document_id());
        if (part == nullptr) {
            viewer_->set_mesh({});
            state_->setText(tr("Není vybrán zobrazovaný dokument."));
            rebuild_application_toolbar();
            return;
        }
        const auto& document = part->session.document();
        if(!document.sketches.empty() && document.sketches.front().drawing_template) {
            const auto& sketch=document.sketches.front();
            if(active_sketch_id_!=sketch.id) {
                active_sketch_id_=sketch.id;
                QTimer::singleShot(0,this,[this]{if(template_sketch())align_active_sketch_view(true);});
            }
        }
        const auto construction_dimension_geometry =
            part_construction_dimension_geometry(
                document, part->session.calculated_boundaries());
        // Explicitly re-assert Modeling mode every time this Part branch
        // runs (not just on first activation): refresh_scene() runs on
        // every tab switch, and active_application_ otherwise keeps
        // whatever value the previously active tab left it at, so
        // rebuild_application_toolbar() below could render e.g. the
        // Assembly toolbar while a Part tab is actually being displayed.
        active_application_ = ApplicationMode::Modeling;
        tree_->setHeaderLabels({tr("DÍL")});
        const bool active_sweep_profile_sketch = sweep_profile_sketch_draft_ &&
            sweep_profile_sketch_draft_->id == active_sketch_id_;
        if (!active_sketch_id_.empty() &&
            !active_sweep_profile_sketch &&
            std::none_of(document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) { return sketch.id == active_sketch_id_; })) {
            active_sketch_id_.clear();
            cancel_sketch_segment();
        }
        if (!selected_sketch_id_.empty() &&
            !(sweep_profile_sketch_draft_ &&
              sweep_profile_sketch_draft_->id == selected_sketch_id_) &&
            std::none_of(
                document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) { return sketch.id == selected_sketch_id_; })) {
            selected_sketch_id_.clear();
        }
        if (!active_sketch_id_.empty()) {
            const auto found = std::find_if(document.sketches.begin(),
                document.sketches.end(), [&](const auto& sketch) {
                    return sketch.id == active_sketch_id_;
                });
            if (found != document.sketches.end()) populate_sketch_tree(*found);
            else if (active_sweep_profile_sketch)
                populate_sketch_tree(*sweep_profile_sketch_draft_);
        } else {
            auto* root = new QTreeWidgetItem(
                tree_, {QString::fromStdString(part->path.empty() ? document.name : part->path.filename().string())});
            root->setIcon(0, resource_icon("part"));
            root->setData(0, Qt::UserRole, QString::fromStdString(document.document_id));
            root->setData(0, Qt::UserRole + 3, "part-result-body");
            if (document.body_history.active_body_id().empty() && !properties_dialog_) {
                root->setForeground(0, QBrush(QColor("#4DD811")));
                auto font = root->font(0); font.setBold(true); root->setFont(0, font);
            }
            add_part_tree_children(root, body_dialog_preview_ ? *body_dialog_preview_ : document);
            root->setExpanded(true);
            tree_->setRootIndex(QModelIndex{});
        }
        const bool sketch_placement_active = sketch_point_active_ ||
            sketch_segment_active_ || sketch_rectangle_active_ ||
            sketch_polygon_active_ || sketch_circle_active_ ||
            sketch_arc_active_ || sketch_ellipse_active_ ||
            sketch_elliptical_arc_active_ || sketch_bspline_active_;
        // In Sketcher, Selection is one command among the other tools. Its
        // checked state must not disable the picker owned by an active
        // Sketch command (external reference, constraints, dimensions...).
        // Outside Sketcher it remains the ordinary global selection toggle.
        viewer_->set_selection_contract(
            active_sketch_id_.empty() && !selection_action_->isChecked()
            ? std::vector<zima::viewer::CandidateKind>{}
            : sketch_external_reference_active_
                ? sketch_external_profile_active_
                    ? std::vector{zima::viewer::CandidateKind::Edge}
                    : std::vector{zima::viewer::CandidateKind::Edge,
                              zima::viewer::CandidateKind::Vertex,
                              zima::viewer::CandidateKind::Axis,
                              zima::viewer::CandidateKind::Face}
            : sketch_rectangle_axis_selecting_
                ? std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_trim_active_
                ? std::vector{zima::viewer::CandidateKind::SketchTrimPiece}
            : sketch_corner_fillet_active_
                ? std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_mirror_active_
                ? sketch_mirror_selecting_sources_
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchPoint,
                                  zima::viewer::CandidateKind::SketchCurve}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchAxis}
            : sketch_coincident_active_
                ? pending_point_pair_constraint_kind_ !=
                        zima::sketcher::ConstraintKind::Coincident
                    ? (pending_coincident_point_id_.empty()
                        ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                                      zima::viewer::CandidateKind::SketchSegment}
                        : std::vector{zima::viewer::CandidateKind::SketchPoint})
                    : pending_coincident_point_id_.empty()
                        ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                                      zima::viewer::CandidateKind::SketchAxis,
                                      zima::viewer::CandidateKind::SketchExternalReference}
                        : std::vector{zima::viewer::CandidateKind::SketchPoint,
                                      zima::viewer::CandidateKind::SketchAxis,
                                      zima::viewer::CandidateKind::SketchSegment,
                                      zima::viewer::CandidateKind::SketchCurve,
                                      zima::viewer::CandidateKind::SketchExternalReference}
            : sketch_midpoint_active_
                ? pending_midpoint_point_id_.empty()
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_symmetric_active_
                ? pending_symmetric_point_ids_.size() < 2
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_concentric_active_
                ? std::vector{zima::viewer::CandidateKind::SketchCurve}
            : sketch_common_tangent_active_
                ? std::vector{zima::viewer::CandidateKind::SketchCurve}
            : sketch_tangent_active_
                ? pending_tangent_geometry_id_.empty()
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchCurve}
                    : pending_tangent_reference_is_segment_
                        ? std::vector{zima::viewer::CandidateKind::SketchCurve}
                        : pending_tangent_reference_supports_curve_pair_
                            ? std::vector{
                                  zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchCurve}
                        : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_segment_pair_active_
                ? pending_pair_kind_ == zima::sketcher::ConstraintKind::EqualLength &&
                        pending_pair_geometry_id_.empty()
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchCurve}
                    : pending_pair_reference_is_circular_
                        ? std::vector{zima::viewer::CandidateKind::SketchCurve}
                        : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_universal_dimension_active_
                ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                              zima::viewer::CandidateKind::SketchSegment,
                              zima::viewer::CandidateKind::SketchAxis,
                              zima::viewer::CandidateKind::SketchCurve}
            : pending_sketch_dimension_.has_value()
                ? pending_sketch_dimension_->kind ==
                            zima::sketcher::DimensionKind::AngleThreePoint
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                : pending_sketch_dimension_->kind ==
                            zima::sketcher::DimensionKind::Distance &&
                        !pending_sketch_dimension_->geometry_id.empty() &&
                        pending_sketch_dimension_->second_geometry_id.empty()
                    ? std::vector{
                          zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchAxis,
                          zima::viewer::CandidateKind::SketchPoint}
                    : std::vector<zima::viewer::CandidateKind>{}
            : sketch_point_dimension_active_
                ? pending_point_dimension_first_id_.empty()
                    ? pending_point_dimension_kind_ ==
                            zima::sketcher::DimensionKind::Distance
                        ? std::vector{
                              zima::viewer::CandidateKind::SketchPoint,
                              zima::viewer::CandidateKind::SketchSegment,
                              zima::viewer::CandidateKind::SketchCurve,
                              zima::viewer::CandidateKind::SketchAxis,
                              zima::viewer::CandidateKind::SketchExternalReference}
                        : std::vector{
                              zima::viewer::CandidateKind::SketchPoint}
                : !pending_point_dimension_second_id_.empty()
                    ? std::vector{
                          zima::viewer::CandidateKind::SketchPoint,
                          zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchAxis}
                : (pending_point_dimension_kind_ ==
                        zima::sketcher::DimensionKind::DistancePointLine ||
                   pending_point_dimension_kind_ ==
                        zima::sketcher::DimensionKind::DistanceSymmetric)
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchAxis,
                                  zima::viewer::CandidateKind::SketchExternalReference}
                : pending_point_dimension_kind_ ==
                        zima::sketcher::DimensionKind::AngleThreePoint
                    ? pending_point_dimension_vertex_id_.empty()
                        ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                        : std::vector{
                              zima::viewer::CandidateKind::SketchPoint,
                              zima::viewer::CandidateKind::SketchSegment,
                              zima::viewer::CandidateKind::SketchAxis}
                : pending_point_dimension_kind_ ==
                        zima::sketcher::DimensionKind::Distance
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                                  zima::viewer::CandidateKind::SketchExternalReference,
                                  zima::viewer::CandidateKind::SketchAxis}
                    : std::vector{zima::viewer::CandidateKind::SketchPoint,
                                  zima::viewer::CandidateKind::SketchExternalReference,
                                  zima::viewer::CandidateKind::SketchAxis}
            : sketch_line_pair_dimension_active_
                ? pending_line_dimension_reference_id_.empty()
                    ? pending_line_dimension_kind_ ==
                            zima::sketcher::DimensionKind::AngleBetween
                        ? std::vector{
                              zima::viewer::CandidateKind::SketchSegment,
                              zima::viewer::CandidateKind::SketchAxis,
                              zima::viewer::CandidateKind::SketchPoint}
                        : std::vector{
                              zima::viewer::CandidateKind::SketchSegment}
                : pending_line_dimension_kind_ ==
                        zima::sketcher::DimensionKind::AngleBetween
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchAxis}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_placement_active
                ? std::vector{zima::viewer::CandidateKind::SketchAxis,
                              zima::viewer::CandidateKind::SketchSegment,
                              zima::viewer::CandidateKind::SketchPoint,
                              zima::viewer::CandidateKind::SketchCurve,
                              zima::viewer::CandidateKind::SketchExternalReference}
            : extrusion_target_dialog_ != nullptr
                ? std::vector{zima::viewer::CandidateKind::Face,
                              zima::viewer::CandidateKind::Plane}
            : pending_construction_reference_index_ ||
                    pending_primitive_reference_index_
                ? placement_reference_candidate_kinds()
            : active_sketch_id_.empty()
                ? [this] {
                    switch (selection_filter_combo_->currentIndex()) {
                        case 1:
                        case 4:
                            return std::vector{zima::viewer::CandidateKind::Face};
                        case 2:
                            return std::vector{zima::viewer::CandidateKind::Vertex};
                        case 3:
                            return std::vector{zima::viewer::CandidateKind::Axis};
                        default:
                            return std::vector{
                                zima::viewer::CandidateKind::Dimension,
                                zima::viewer::CandidateKind::Container};
                    }
                }()
                : std::vector{zima::viewer::CandidateKind::TemplateImage, zima::viewer::CandidateKind::TemplateRegion,
                              zima::viewer::CandidateKind::SketchSegment,
                              zima::viewer::CandidateKind::SketchPoint,
                              zima::viewer::CandidateKind::SketchAxis,
                              zima::viewer::CandidateKind::Dimension,
                              zima::viewer::CandidateKind::SketchConstraint,
                              zima::viewer::CandidateKind::SketchCurve,
                              zima::viewer::CandidateKind::SketchText,
                              zima::viewer::CandidateKind::SketchExternalReference});
        if (sketch_external_reference_active_) {
            const auto source_owners = sketch_external_reference_source_owners(
                document, active_sketch_id_);
            viewer_->set_candidate_filter(
                [source_owners](const auto& candidate) {
                    const bool stable_geometry =
                        candidate.kind == zima::viewer::CandidateKind::Face
                            ? candidate.geometry ==
                                  zima::viewer::CandidateGeometry::Display
                            : candidate.geometry == zima::viewer::
                                  CandidateGeometry::OriginalReference;
                    return stable_geometry && candidate.instance_path.empty() &&
                        source_owners.contains(candidate.owner_id) &&
                        (candidate.kind == zima::viewer::CandidateKind::Edge ||
                         candidate.kind == zima::viewer::CandidateKind::Vertex ||
                         candidate.kind == zima::viewer::CandidateKind::Axis ||
                         candidate.kind == zima::viewer::CandidateKind::Face);
                });
        } else if (sketch_coincident_active_) {
            const auto owner_id = active_sketch_id_;
            const auto first_id = pending_coincident_point_id_;
            const auto kind = pending_point_pair_constraint_kind_;
            viewer_->set_candidate_filter(
                [owner_id, first_id, kind](const auto& candidate) {
                    if (candidate.owner_id != owner_id) return false;
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchPoint) {
                        return candidate.semantic_key.starts_with("point:") &&
                            candidate.semantic_key.substr(6) != first_id;
                    }
                    if (kind != zima::sketcher::ConstraintKind::Coincident)
                        return first_id.empty() &&
                            candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                            candidate.semantic_key.starts_with("segment:");
                    if (first_id.empty()) {
                        return (candidate.kind ==
                                    zima::viewer::CandidateKind::SketchAxis &&
                                (candidate.semantic_key == "sketch_axis:x" ||
                                 candidate.semantic_key == "sketch_axis:y")) ||
                            (candidate.kind == zima::viewer::CandidateKind::
                                    SketchExternalReference &&
                                candidate.semantic_key.starts_with(
                                    "external_point:"));
                    }
                    if ((first_id == "sketch_axis:x" ||
                         first_id == "sketch_axis:y")) {
                        return candidate.kind ==
                                zima::viewer::CandidateKind::SketchPoint &&
                            candidate.semantic_key.starts_with("point:");
                    }
                    return (candidate.kind ==
                                zima::viewer::CandidateKind::SketchAxis &&
                            (candidate.semantic_key == "sketch_axis:x" ||
                             candidate.semantic_key == "sketch_axis:y")) ||
                        (candidate.kind ==
                                zima::viewer::CandidateKind::SketchSegment &&
                            candidate.semantic_key.starts_with("segment:")) ||
                        (candidate.kind ==
                                zima::viewer::CandidateKind::SketchCurve &&
                            (candidate.semantic_key.starts_with("circle:") ||
                             candidate.semantic_key.starts_with("arc:") ||
                             candidate.semantic_key.starts_with("ellipse:") ||
                             candidate.semantic_key.starts_with(
                                 "elliptical_arc:") ||
                             candidate.semantic_key.starts_with("bspline:"))) ||
                        (candidate.kind == zima::viewer::CandidateKind::
                                SketchExternalReference &&
                            (candidate.semantic_key.starts_with("external_point:") ||
                             candidate.semantic_key.starts_with("external_edge:") ||
                             candidate.semantic_key.starts_with("external_axis:") ||
                             candidate.semantic_key.starts_with("external_face:")));
                });
        } else if (sketch_corner_fillet_active_) {
            const auto owner_id = active_sketch_id_;
            const auto first_id = pending_corner_fillet_segment_id_;
            viewer_->set_candidate_filter(
                [owner_id, first_id](const auto& candidate) {
                    return candidate.owner_id == owner_id &&
                        candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                        candidate.semantic_key.starts_with("segment:") &&
                        (first_id.empty() || candidate.semantic_key.substr(8) != first_id);
                });
        } else if (sketch_rectangle_axis_selecting_) {
            const auto* sketch = active_sketch();
            std::set<std::string> construction_axes;
            if (sketch != nullptr) {
                for (const auto& segment : sketch->segments) {
                    if (segment.construction) construction_axes.insert(segment.id);
                }
            }
            const auto owner_id = active_sketch_id_;
            viewer_->set_candidate_filter(
                [owner_id, construction_axes](const auto& candidate) {
                    return candidate.owner_id == owner_id &&
                        candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                        candidate.semantic_key.starts_with("segment:") &&
                        construction_axes.contains(candidate.semantic_key.substr(8));
                });
        } else if (sketch_symmetric_active_ &&
                   pending_symmetric_point_ids_.size() == 2) {
            set_sketch_symmetric_axis_contract();
        } else if (sketch_concentric_active_) {
            set_sketch_concentric_contract();
        } else if (sketch_common_tangent_active_) {
            set_sketch_common_tangent_contract();
        } else if (sketch_tangent_active_) {
            set_sketch_tangent_contract();
        } else if (sketch_segment_pair_active_) {
            set_sketch_pair_contract();
        } else if (sketch_universal_dimension_active_) {
            set_sketch_universal_dimension_contract();
        } else if (pending_sketch_dimension_ &&
                   pending_sketch_dimension_->kind ==
                       zima::sketcher::DimensionKind::AngleThreePoint) {
            const auto owner_id = active_sketch_id_;
            const auto first = pending_sketch_dimension_->first_point_id;
            const auto vertex = pending_sketch_dimension_->second_point_id;
            const auto third = pending_sketch_dimension_->geometry_id;
            viewer_->set_candidate_filter(
                [owner_id, first, vertex, third](const auto& candidate) {
                    return candidate.owner_id == owner_id &&
                        candidate.kind ==
                            zima::viewer::CandidateKind::SketchPoint &&
                        candidate.semantic_key.starts_with("point:") &&
                        candidate.semantic_key.substr(6) != first &&
                        candidate.semantic_key.substr(6) != vertex &&
                        candidate.semantic_key.substr(6) != third;
                });
        } else if (pending_sketch_dimension_ &&
                   pending_sketch_dimension_->kind ==
                       zima::sketcher::DimensionKind::Distance &&
                   !pending_sketch_dimension_->geometry_id.empty() &&
                   pending_sketch_dimension_->second_geometry_id.empty()) {
            const auto owner_id = active_sketch_id_;
            const auto first_segment = pending_sketch_dimension_->geometry_id;
            viewer_->set_candidate_filter(
                [owner_id, first_segment](const auto& candidate) {
                    if (candidate.owner_id != owner_id) return false;
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchPoint) {
                        return candidate.semantic_key.starts_with("point:");
                    }
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchSegment &&
                        candidate.semantic_key.starts_with("segment:") &&
                        candidate.semantic_key.substr(8) != first_segment) {
                        return true;
                    }
                    return candidate.kind ==
                            zima::viewer::CandidateKind::SketchAxis &&
                        (candidate.semantic_key == "sketch_axis:x" ||
                         candidate.semantic_key == "sketch_axis:y");
                });
        } else if (sketch_point_dimension_active_) {
            const auto owner_id = active_sketch_id_;
            const auto first_id = pending_point_dimension_first_id_;
            const auto second_id = pending_point_dimension_second_id_;
            const auto vertex_id = pending_point_dimension_vertex_id_;
            const auto kind = pending_point_dimension_kind_;
            viewer_->set_candidate_filter(
                [owner_id, first_id, second_id, vertex_id, kind](const auto& candidate) {
                    if (candidate.owner_id != owner_id) return false;
                    if (!second_id.empty()) {
                        if (candidate.kind ==
                                zima::viewer::CandidateKind::SketchPoint) {
                            return candidate.semantic_key.starts_with("point:") &&
                                candidate.semantic_key.substr(6) != first_id &&
                                candidate.semantic_key.substr(6) != second_id;
                        }
                        return (candidate.kind ==
                                    zima::viewer::CandidateKind::SketchSegment &&
                                candidate.semantic_key.starts_with("segment:")) ||
                            (candidate.kind ==
                                    zima::viewer::CandidateKind::SketchAxis &&
                                (candidate.semantic_key == "sketch_axis:x" ||
                                 candidate.semantic_key == "sketch_axis:y"));
                    }
                    if (first_id.empty()) {
                        if (candidate.kind ==
                                zima::viewer::CandidateKind::SketchPoint) {
                            return candidate.semantic_key.starts_with("point:");
                        }
                        if (kind == zima::sketcher::DimensionKind::Distance &&
                            candidate.kind ==
                                zima::viewer::CandidateKind::SketchAxis) {
                            return candidate.semantic_key == "sketch_axis:x" ||
                                candidate.semantic_key == "sketch_axis:y";
                        }
                        if (kind == zima::sketcher::DimensionKind::Distance &&
                            candidate.kind == zima::viewer::CandidateKind::
                                SketchExternalReference) {
                            return candidate.semantic_key ==
                                "external_point:sketch_origin";
                        }
                        return kind ==
                                zima::sketcher::DimensionKind::Distance &&
                            ((candidate.kind == zima::viewer::CandidateKind::
                                    SketchSegment &&
                              candidate.semantic_key.starts_with("segment:")) ||
                             (candidate.kind == zima::viewer::CandidateKind::
                                    SketchCurve &&
                              (candidate.semantic_key.starts_with("circle:") ||
                               candidate.semantic_key.starts_with("arc:"))));
                    }
                    if (kind == zima::sketcher::DimensionKind::DistancePointLine ||
                        kind == zima::sketcher::DimensionKind::DistanceSymmetric) {
                        return (candidate.kind ==
                                    zima::viewer::CandidateKind::SketchSegment &&
                                candidate.semantic_key.starts_with("segment:")) ||
                            (candidate.kind ==
                                 zima::viewer::CandidateKind::SketchAxis &&
                             (candidate.semantic_key == "sketch_axis:x" ||
                              candidate.semantic_key == "sketch_axis:y")) ||
                            (candidate.kind == zima::viewer::CandidateKind::
                                 SketchExternalReference &&
                             (candidate.semantic_key.starts_with("external_edge:") ||
                              candidate.semantic_key.starts_with("external_axis:")));
                    }
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchPoint &&
                        candidate.semantic_key.starts_with("point:")) {
                        return candidate.semantic_key.substr(6) != first_id &&
                            candidate.semantic_key.substr(6) != vertex_id;
                    }
                    if (kind == zima::sketcher::DimensionKind::AngleThreePoint &&
                        !vertex_id.empty()) {
                        return (candidate.kind ==
                                    zima::viewer::CandidateKind::SketchSegment &&
                                candidate.semantic_key.starts_with("segment:")) ||
                            (candidate.kind ==
                                    zima::viewer::CandidateKind::SketchAxis &&
                                (candidate.semantic_key == "sketch_axis:x" ||
                                 candidate.semantic_key == "sketch_axis:y"));
                    }
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchAxis) {
                        if (kind == zima::sketcher::DimensionKind::Distance) {
                            return first_id != "sketch_origin" &&
                                (candidate.semantic_key == "sketch_axis:x" ||
                                 candidate.semantic_key == "sketch_axis:y");
                        }
                        return (kind == zima::sketcher::DimensionKind::DistanceX &&
                                candidate.semantic_key == "sketch_axis:y") ||
                            (kind == zima::sketcher::DimensionKind::DistanceY &&
                             candidate.semantic_key == "sketch_axis:x");
                    }
                    return candidate.kind == zima::viewer::CandidateKind::
                               SketchExternalReference &&
                        candidate.semantic_key.starts_with("external_point:") &&
                        !(first_id == "sketch_origin" &&
                          candidate.semantic_key ==
                              "external_point:sketch_origin");
                });
        } else if (sketch_line_pair_dimension_active_) {
            const auto owner_id = active_sketch_id_;
            const auto reference_id = pending_line_dimension_reference_id_;
            const auto kind = pending_line_dimension_kind_;
            viewer_->set_candidate_filter(
                [owner_id, reference_id, kind](const auto& candidate) {
                    if (candidate.owner_id != owner_id) return false;
                    if (reference_id.empty() &&
                        kind == zima::sketcher::DimensionKind::AngleBetween &&
                        candidate.kind ==
                            zima::viewer::CandidateKind::SketchPoint) {
                        return candidate.semantic_key.starts_with("point:");
                    }
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchSegment &&
                        candidate.semantic_key.starts_with("segment:")) {
                        return reference_id.empty() ||
                            candidate.semantic_key.substr(8) != reference_id;
                    }
                    return kind == zima::sketcher::DimensionKind::AngleBetween &&
                        candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
                        !reference_id.starts_with("sketch_axis:") &&
                        (candidate.semantic_key == "sketch_axis:x" ||
                         candidate.semantic_key == "sketch_axis:y");
                });
        }
        if (part_rollback_ &&
            part_rollback_->part_document_id == document.document_id) {
            auto display = part_rollback_->input_body
                ? part_rollback_->input_body->mesh : zima::kernel::ViewerMesh{};
            if (!document.body_history.bodies().empty() && part_rollback_->history_limit < document.history.size()) {
                const auto& edited = document.history[part_rollback_->history_limit];
                if (const auto* owner = document.body_history.owner(edited.id)) {
                    auto context = document.body_history;
                    context.activate(owner->scope.id);
                    context.set_history_cursor(owner->scope.id, context.rollback_before(edited.id).entry_count);
                    display = part->session.body_context_mesh(&context);
                }
            }
            // Sketcher keeps the complete View context visible. Selection
            // tools narrow what can be confirmed through their candidate
            // contracts; presentation itself is never filtered.
            append_mesh(display, active_part_origins(document));
            append_mesh(display, construction_mesh(
                document, 0.0, construction_dimension_geometry));
            // Rollback supplies only the real body input before the edited
            // feature. The owned profile is persisted ZIMA Sketch data and
            // must be added explicitly while its Sketcher is active.
            if (!active_sketch_id_.empty()) {
                const auto active = std::find_if(document.sketches.begin(),
                    document.sketches.end(), [&](const auto& sketch) {
                        return sketch.id == active_sketch_id_;
                    });
                if (active != document.sketches.end()) {
                    append_mesh(display, sketch_viewer_mesh(*active));
                }

                if (sweep_profile_sketch_draft_ &&
                    sweep_profile_sketch_draft_->id == active_sketch_id_ &&
                    std::ranges::find(document.sketches, active_sketch_id_, &zima::sketcher::Sketch::id) == document.sketches.end()) {
                    append_mesh(display,
                        sketch_viewer_mesh(*sweep_profile_sketch_draft_));
                }
            }
            if (primitive_origin_preview_mesh_) {
                auto preview = *primitive_origin_preview_mesh_;
                auto body_id = sketch_properties_body_id_.empty()
                    ? document.body_history.active_body_id() : sketch_properties_body_id_;
                if (sketch_properties_body_id_.empty() && part_rollback_ && part_rollback_->history_limit < document.history.size()) {
                    if (const auto* owner = document.body_history.owner(document.history[part_rollback_->history_limit].id))
                        body_id = owner->scope.id;
                }
                if (!body_id.empty()) preview = document.place_body_mesh(std::move(preview), body_id);
                append_mesh(display, std::move(preview));
            }
            if (viewer_->reference_visible(
                    zima::viewer::ReferenceVisibility::Origins) &&
                !visible_local_origin_ids_.empty()) {
                append_mesh(display, local_container_context_mesh(
                    document, visible_local_origin_ids_,
                    viewer_->reference_visible(
                        zima::viewer::ReferenceVisibility::Axes),
                    viewer_->reference_visible(
                        zima::viewer::ReferenceVisibility::Planes)));
            }
            if (template_sketch()) display = sketch_viewer_mesh(document.sketches.front());
            viewer_->set_mesh(std::move(display),
                !preserve_view_on_refresh_ && active_sketch_id_.empty());
            preserve_view_on_refresh_ = false;
        } else {
            const auto& calculated = part->session.calculated_boundaries();
            const auto feature_count =
                document.body_operation_count_at_history_cursor();
            const auto displayed_count = std::min(feature_count, calculated.size());
            // The final boundary already contains the complete persisted
            // reference packet. Asking DocumentSession for it returned a full
            // BodyResult copy merely to filter nothing, then copied its mesh
            // once more below. Large imported bodies made every preview scene
            // refresh pay both copies.
            std::optional<zima::kernel::BodyResult> filtered_cursor_body;
            const zima::kernel::BodyResult* cursor_body = nullptr;
            if (!document.body_history.bodies().empty()) {
                filtered_cursor_body.emplace();
                filtered_cursor_body->mesh = part->session.body_context_mesh();
                cursor_body = &*filtered_cursor_body;
            } else if (displayed_count == calculated.size() && !calculated.empty()) {
                cursor_body = &calculated.back();
            } else if (displayed_count > 0) {
                filtered_cursor_body = part->session.calculated_boundary(
                    displayed_count);
                if (filtered_cursor_body) cursor_body = &*filtered_cursor_body;
            }
            zima::kernel::ViewerMesh display = cursor_body != nullptr
                ? cursor_body->mesh : zima::kernel::ViewerMesh{};
            if (body_dialog_context_) {
                display = {};
                if (!calculated.empty()) for (const auto& id : *body_dialog_context_) {
                    const auto found = calculated.back().body_outputs.find(id);
                    if (found != calculated.back().body_outputs.end()) append_mesh(display, found->second->mesh);
                    else if(calculated.back().body_outputs.empty()&&body_dialog_context_->size()==1)append_mesh(display,calculated.back().mesh);
                }
            }
            // Standalone Thread sheets are already part of the calculated
            // boundary. Only legacy cosmetic threads owned by Hole are
            // appended as lightweight presentation edges here.
            std::size_t visible_body_features{};
            std::unordered_set<std::string> visible_history_ids;
            const auto history_cursor=document.effective_history_cursor();
            for (std::size_t index=0; index<history_cursor; ++index)
                visible_history_ids.insert(document.history_order[index].id);
            for (const auto& container : document.history) {
                if (!visible_history_ids.contains(container.id)) continue;
                if (container.feature_kind ==
                    zima::document::FeatureKind::Sketch) continue;
                if (visible_body_features++ >= displayed_count) break;
                auto thread = document.hole_thread_edges(container,
                    cursor_body == nullptr ? nullptr : &cursor_body->mesh);
                display.edges.insert(display.edges.end(),
                    std::make_move_iterator(thread.begin()),
                    std::make_move_iterator(thread.end()));
            }
            for (const auto& sketch : document.sketches) {
                if (sketch.id == sketch_properties_preview_id_) continue;
                if (sketch.id != active_sketch_id_ &&
                    !sketch_visible_outside_sketcher(document, sketch)) continue;
                const auto* displayed_sketch = sketch_trim_active_ &&
                        sketch_trim_preview_ && sketch.id == active_sketch_id_
                    ? &*sketch_trim_preview_ : &sketch;
                auto sketch_mesh = sketch_viewer_mesh(*displayed_sketch);
                if (properties_dialog_ != nullptr && active_sketch_id_.empty()) {
                    remove_sketch_computation_points(sketch_mesh);
                }
                if (!editing_sketch_text_id_.empty() &&
                    sketch.id == active_sketch_id_) {
                    std::erase_if(sketch_mesh.edges, [&](const auto& edge) {
                        const auto text_id = sketch_text_id_from_key(
                            edge.reference.semantic_key);
                        return text_id && *text_id == editing_sketch_text_id_;
                    });
                }
                if (sketch.id != active_sketch_id_) {
                    keep_only_inactive_sketch_profile(sketch_mesh,
                        sketch.owner_container_id.empty()
                            ? sketch.id : sketch.owner_container_id);
                }
                append_mesh(display, std::move(sketch_mesh));
            }

            if (sweep_profile_sketch_draft_ &&
                sweep_profile_sketch_draft_->id == active_sketch_id_ &&
                    std::ranges::find(document.sketches, active_sketch_id_, &zima::sketcher::Sketch::id) == document.sketches.end()) {
                append_mesh(display,
                    sketch_viewer_mesh(*sweep_profile_sketch_draft_));
            }
            append_mesh(display, active_part_origins(document));
            append_mesh(display, construction_mesh(
                document, 0.0, construction_dimension_geometry));
            if (primitive_origin_preview_mesh_) {
                auto preview = *primitive_origin_preview_mesh_;
                auto body_id = sketch_properties_body_id_.empty()
                    ? document.body_history.active_body_id() : sketch_properties_body_id_;
                if (sketch_properties_body_id_.empty() && part_rollback_ && part_rollback_->history_limit < document.history.size()) {
                    if (const auto* owner = document.body_history.owner(document.history[part_rollback_->history_limit].id))
                        body_id = owner->scope.id;
                }
                if (!body_id.empty()) preview = document.place_body_mesh(std::move(preview), body_id);
                append_mesh(display, std::move(preview));
            }
            if (viewer_->reference_visible(
                    zima::viewer::ReferenceVisibility::Origins) &&
                !visible_local_origin_ids_.empty()) {
                append_mesh(display, local_container_context_mesh(
                    document, visible_local_origin_ids_,
                    viewer_->reference_visible(
                        zima::viewer::ReferenceVisibility::Axes),
                    viewer_->reference_visible(
                        zima::viewer::ReferenceVisibility::Planes)));
            }
            if (sketch_external_reference_active_) {
                const auto source_owners = sketch_external_reference_source_owners(
                    document, active_sketch_id_);
                for (const auto& axis : display.original_references.axes) {
                    if (axis.reference.instance_path.empty() &&
                        source_owners.contains(axis.reference.owner_id)) {
                        display.axes.push_back(axis);
                    }
                }
            }
            if (template_sketch()) display = sketch_viewer_mesh(document.sketches.front());
            viewer_->set_mesh(std::move(display),
                !preserve_view_on_refresh_ && active_sketch_id_.empty());
            preserve_view_on_refresh_ = false;
        }
        if (!document.body_history.bodies().empty() && properties_dialog_ == nullptr && active_sketch_id_.empty()) {
            const auto active_body = document.body_history.active_body_id();
            std::map<std::string,std::string> owners;
            for (const auto& body : document.body_history.bodies())
                for (const auto& entry : body.entries) owners.emplace(entry.id, body.scope.id);
            viewer_->set_candidate_filter([active_body, owners = std::move(owners)](const auto& candidate) {
                const auto owner = owners.find(candidate.owner_id);
                return active_body.empty() || owner == owners.end() || owner->second == active_body;
            }, false);
        }
        // set_mesh() intentionally resets stale picking state. Dimension
        // inspection, however, still owns this exact persisted container, so
        // restore its cyan confirmation after the rebuilt scene is installed.
        if (!construction_dimension_object_id_.empty()) {
            if (is_edge_treatment_feature(
                    construction_dimension_object_id_)) {
                viewer_->set_feature_selected_edges({});
                // Inspection reveals the feature dimension but does not
                // select it. The shared dimension colour contract therefore
                // keeps this ordinary, editable dimension yellow; it turns
                // cyan only after an explicit LMB selection.
                viewer_->clear_selection();
            } else {
                // Parameter-dimension inspection must leave the annotations
                // hoverable after any scene rebuild, including an inline
                // value edit made without an open properties dialog.
                // Confirming the owning container here latched a cyan object
                // selection and suppressed all subsequent dimension hover.
                viewer_->clear_selection();
                viewer_->set_feature_selected_edges({});
            }
        }
        state_->setText(document.history.empty()
            ? tr("Nový Part: začněte příkazem Kvádr, jiným tělesem nebo Skica.")
            : tr("Zobrazený Part: %1").arg(QString::fromStdString(document.name)));
        insert_action_->setEnabled(false);
        regenerate_action_->setEnabled(false);
        save_action_->setEnabled(true);
        save_as_action_->setEnabled(true);
        close_document_action_->setEnabled(true);
        regenerate_document_action_->setEnabled(true);
        box_action_->setEnabled(true);
        cylinder_action_->setEnabled(true);
    thread_action_->setEnabled(!document.history.empty());
    shaft_thread_action_->setEnabled(!document.history.empty());
    drill_point_action_->setEnabled(!document.history.empty());
        sphere_action_->setEnabled(true);
        cone_action_->setEnabled(true);
        pyramid_action_->setEnabled(true);
        wedge_action_->setEnabled(true);
        fillet_action_->setEnabled(!document.history.empty());
        chamfer_action_->setEnabled(!document.history.empty());
        shell_action_->setEnabled(!document.history.empty());
        construction_point_action_->setEnabled(true);
        curve_3d_action_->setEnabled(true);
        sweep_3d_action_->setEnabled(true);
        helical_sweep_action_->setEnabled(true);
        sweep2d_action_->setEnabled(true);
        construction_axis_action_->setEnabled(true);
        construction_plane_action_->setEnabled(true);
        extrusion_action_->setEnabled(true);
        revolution_action_->setEnabled(true);
        sketch_action_->setEnabled(true);
        sketch_normal_view_action_->setEnabled(!active_sketch_id_.empty());
        sketch_flip_view_action_->setEnabled(!active_sketch_id_.empty());
        sketch_rotate_view_action_->setEnabled(!active_sketch_id_.empty());
        sketch_external_reference_action_->setEnabled(!active_sketch_id_.empty());
        sketch_external_profile_action_->setEnabled(!active_sketch_id_.empty());
        sketch_point_action_->setEnabled(!active_sketch_id_.empty());
        sketch_construction_action_->setEnabled(!active_sketch_id_.empty());
        sketch_segment_action_->setEnabled(!active_sketch_id_.empty());
        sketch_common_tangent_action_->setEnabled(!active_sketch_id_.empty());
        sketch_polyline_action_->setEnabled(!active_sketch_id_.empty());
        sketch_rectangle_action_->setEnabled(!active_sketch_id_.empty());
        sketch_polygon_action_->setEnabled(!active_sketch_id_.empty());
        sketch_trim_action_->setEnabled(!active_sketch_id_.empty());
        sketch_mirror_action_->setEnabled(
            !active_sketch_id_.empty() && !sketch_mirror_active_);
        sketch_offset_action_->setEnabled(!active_sketch_id_.empty() && !properties_dialog_);
        sketch_circle_action_->setEnabled(!active_sketch_id_.empty());
        sketch_arc_action_->setEnabled(!active_sketch_id_.empty());
        sketch_ellipse_action_->setEnabled(!active_sketch_id_.empty());
        sketch_elliptical_arc_action_->setEnabled(!active_sketch_id_.empty());
        sketch_bspline_action_->setEnabled(!active_sketch_id_.empty());
        sketch_interpolating_spline_action_->setEnabled(!active_sketch_id_.empty());
        sketch_text_action_->setEnabled(!active_sketch_id_.empty());
        sketch_constraints_action_->setEnabled(!active_sketch_id_.empty());
        sketch_dimensions_action_->setEnabled(!active_sketch_id_.empty());
        // H/V can constrain a preselected segment, but without a preselection
        // they deliberately start the two-point direction workflow. Keep both
        // commands available for the whole active-Sketch session; gating them
        // on a selected object made that supported workflow unreachable after
        // an ordinary scene refresh.
        sketch_horizontal_action_->setEnabled(!active_sketch_id_.empty());
        sketch_vertical_action_->setEnabled(!active_sketch_id_.empty());
        sketch_coincident_action_->setEnabled(!active_sketch_id_.empty());
        sketch_midpoint_action_->setEnabled(!active_sketch_id_.empty());
        sketch_symmetric_action_->setEnabled(!active_sketch_id_.empty());
        sketch_concentric_action_->setEnabled(!active_sketch_id_.empty());
        sketch_tangent_action_->setEnabled(!active_sketch_id_.empty());
        sketch_parallel_action_->setEnabled(!active_sketch_id_.empty());
        sketch_perpendicular_action_->setEnabled(!active_sketch_id_.empty());
        sketch_equal_length_action_->setEnabled(!active_sketch_id_.empty());
        const bool segment_or_point = !selected_sketch_segment_id_.empty() ||
            !selected_sketch_point_id_.empty();
        // The general dimension tool starts by selecting its first point, so
        // it must remain available even when the sketch has no preselection.
        sketch_dimension_action_->setEnabled(!active_sketch_id_.empty());
        sketch_universal_dimension_action_->setEnabled(
            !active_sketch_id_.empty());
        sketch_dimension_x_action_->setEnabled(segment_or_point);
        sketch_dimension_y_action_->setEnabled(segment_or_point);
        sketch_point_line_dimension_action_->setEnabled(
            !selected_sketch_point_id_.empty());
        sketch_symmetric_dimension_action_->setEnabled(
            !selected_sketch_point_id_.empty());
        sketch_three_point_angle_dimension_action_->setEnabled(
            !selected_sketch_point_id_.empty());
        sketch_angle_dimension_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_radius_dimension_action_->setEnabled(
            !selected_sketch_circle_id_.empty() || !selected_sketch_arc_id_.empty());
        sketch_diameter_dimension_action_->setEnabled(
            !selected_sketch_circle_id_.empty() || !selected_sketch_arc_id_.empty());
        sketch_ellipse_major_dimension_action_->setEnabled(
            !selected_sketch_ellipse_id_.empty());
        sketch_ellipse_minor_dimension_action_->setEnabled(
            !selected_sketch_ellipse_id_.empty());
        sketch_ellipse_rotation_dimension_action_->setEnabled(
            !selected_sketch_ellipse_id_.empty());
        sketch_fix_point_action_->setEnabled(!selected_sketch_point_id_.empty());
        finish_sketch_action_->setEnabled(!active_sketch_id_.empty());
        regenerate_part_action_->setEnabled(true);
        undo_action_->setEnabled(part->session.can_undo());
        redo_action_->setEnabled(part->session.can_redo());
        configure_sketch_box_selection(!active_sketch_id_.empty());
        update_application_actions();
        rebuild_application_toolbar();
        // The Part branch returns before the common Assembly tail below.
        // Re-assert the command-local Up-to contract after every Part scene
        // and toolbar refresh as well; otherwise a preview refresh leaves the
        // field green but clears all Face/Plane hover candidates.
        if (extrusion_target_dialog_ != nullptr) {
            apply_extrusion_target_selection_contract();
        }
        viewer_->set_container_inspection(properties_dialog_ == nullptr
            ? construction_dimension_object_id_ : std::string{});
        update_section_ui();
        update_measurement_ui();
        return;
    }
    // Explicitly re-assert Assembly mode every time this branch runs (not
    // just on first activation), matching the Part branch above: otherwise
    // active_application_ keeps whatever the previously active tab left it
    // at, and rebuild_application_toolbar() below can render the wrong
    // (e.g. Part-mode) toolbar while an Assembly tab is actually displayed.
    active_application_ = ApplicationMode::Assembly;
    const auto& document = assembly->session.document();
    const auto active_assembly_display = [this](
            const zima::assembly::AssemblyDocument& original) {
        const auto& source=derived_copy_assembly_preview_&&derived_copy_assembly_preview_->document_id==original.document_id
            ? *derived_copy_assembly_preview_ : original;
        const bool active = workspace_.active_document_id() == source.document_id;
        zima::kernel::ViewerMesh mesh;
        if (construction_parameter_preview_ && active) {
            auto displayed = source;
            auto* existing = displayed.find_construction(
                construction_parameter_preview_->id);
            if (existing != nullptr) {
                *existing = *construction_parameter_preview_;
            } else {
                displayed.constructions.push_back(
                    *construction_parameter_preview_);
            }
            mesh = displayed.build_scene();
        } else {
            mesh = source.build_scene();
        }
        if (primitive_origin_preview_mesh_ && active) {
            append_mesh(mesh, *primitive_origin_preview_mesh_);
        }
        return mesh;
    };
    const auto* active_part =
        workspace_.open_part(workspace_.active_document_id());
    const bool active_top_assembly_sketch =
        workspace_.active_document_id() == document.document_id &&
        !active_sketch_id_.empty();
    const auto active_part_occurrence = active_part == nullptr
        ? std::optional<std::string>{}
        : resolve_active_occurrence(active_part->session.document().document_id);
    if (active_part != nullptr && !active_sketch_id_.empty() &&
        !(sweep_profile_sketch_draft_ &&
          sweep_profile_sketch_draft_->id == active_sketch_id_) &&
        std::none_of(active_part->session.document().sketches.begin(),
            active_part->session.document().sketches.end(),
            [&](const auto& sketch) { return sketch.id == active_sketch_id_; })) {
        active_sketch_id_.clear();
        cancel_sketch_segment();
    }
    if (active_part_occurrence && !active_part_occurrence->empty()) {
        const auto top_assembly_id = document.document_id;
        const auto occurrence_path = zima::assembly::InstancePath::decode(
            *active_part_occurrence);
        auto body_frame = container_dimension_frame(zima::document::Placement{});
        if (const auto* sketch = active_sketch())
            if (const auto* body = sketch_body(*sketch))
                body_frame = container_dimension_frame(body->scope.placement);
        if (active_part)
            if (const auto* body = active_part->session.document().body_history.find(sketch_properties_body_id_))
                body_frame = container_dimension_frame(body->scope.placement);
        viewer_->set_transient_point_transform(
            [this, top_assembly_id, occurrence_path, body_frame](const auto& point) {
                return workspace_.occurrence_point_to_scene(
                    top_assembly_id, occurrence_path, body_frame.point(point));
            });
    }
    const zima::sketcher::Sketch* editing_sketch = nullptr;
    if (!active_sketch_id_.empty()) {
        if (active_part != nullptr) {
            const auto& sketches = active_part->session.document().sketches;
            const auto found = std::find_if(sketches.begin(), sketches.end(),
                [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
            if (found != sketches.end()) editing_sketch = &*found;
        } else if (const auto* active_assembly = workspace_.open_assembly(
                       workspace_.active_document_id())) {
            const auto& sketches = active_assembly->session.document().sketches;
            const auto found = std::find_if(sketches.begin(), sketches.end(),
                [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
            if (found != sketches.end()) editing_sketch = &*found;
        }
    }
    if(sweep_profile_sketch_draft_)editing_sketch=active_sketch();
    if (editing_sketch != nullptr) {
        populate_sketch_tree(*editing_sketch);
    } else {
        tree_->setHeaderLabels({tr("SESTAVA")});
        auto* root = new QTreeWidgetItem(
            tree_, {QString::fromStdString(assembly->path.empty() ? document.name : assembly->path.filename().string())});
        root->setIcon(0, resource_icon("assembly"));
        add_assembly_tree_children(root, document.document_id, {});
        root->setExpanded(true);
        tree_->setRootIndex(QModelIndex{});
    }
    const bool sketch_placement_active = sketch_point_active_ ||
        sketch_segment_active_ || sketch_rectangle_active_ ||
        sketch_polygon_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_;
    viewer_->set_selection_contract(active_sketch_id_.empty() && !selection_action_->isChecked()
        ? std::vector<zima::viewer::CandidateKind>{}
        : (active_part != nullptr || active_top_assembly_sketch) && sketch_trim_active_
            ? std::vector{zima::viewer::CandidateKind::SketchTrimPiece}
        : (active_part != nullptr || active_top_assembly_sketch) &&
                sketch_external_reference_active_
            ? sketch_external_profile_active_
                ? std::vector{zima::viewer::CandidateKind::Edge}
                : std::vector{zima::viewer::CandidateKind::Edge,
                          zima::viewer::CandidateKind::Vertex,
                          zima::viewer::CandidateKind::Axis,
                          zima::viewer::CandidateKind::Face}
        : sketch_placement_active
            ? std::vector{zima::viewer::CandidateKind::SketchAxis,
                          zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchPoint,
                          zima::viewer::CandidateKind::SketchCurve,
                          zima::viewer::CandidateKind::SketchExternalReference}
        : extrusion_target_dialog_ != nullptr
            ? std::vector{zima::viewer::CandidateKind::Face,
                          zima::viewer::CandidateKind::Plane}
        : pending_construction_reference_index_ ||
                pending_primitive_reference_index_
            ? placement_reference_candidate_kinds()
        : (active_part != nullptr && !active_sketch_id_.empty()) ||
                active_top_assembly_sketch
            ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchPoint,
                          zima::viewer::CandidateKind::Dimension,
                          zima::viewer::CandidateKind::SketchConstraint,
                          zima::viewer::CandidateKind::SketchCurve,
                          zima::viewer::CandidateKind::SketchText,
                          zima::viewer::CandidateKind::SketchExternalReference}
        : [this] {
            switch (selection_filter_combo_->currentIndex()) {
                case 1:
                case 4:
                    return std::vector{zima::viewer::CandidateKind::Face};
                case 2:
                    return std::vector{zima::viewer::CandidateKind::Vertex};
                case 3:
                    return std::vector{zima::viewer::CandidateKind::Axis};
                default:
                    return std::vector{zima::viewer::CandidateKind::Dimension,
                                       zima::viewer::CandidateKind::Occurrence};
            }
        }());
    if (active_top_assembly_sketch && sketch_external_reference_active_) {
        const auto top_assembly_id = document.document_id;
        viewer_->set_candidate_filter([this, top_assembly_id](const auto& candidate) {
            if (candidate.geometry !=
                    zima::viewer::CandidateGeometry::OriginalReference ||
                candidate.instance_path.empty()) return false;
            try {
                return workspace_.resolve_occurrence(top_assembly_id,
                    zima::assembly::InstancePath::decode(candidate.instance_path))
                    .has_value();
            } catch (const std::invalid_argument&) {
                return false;
            }
        });
    } else if (active_part != nullptr && sketch_external_reference_active_ &&
        active_part_occurrence && !active_part_occurrence->empty()) {
        const auto allowed_local_owners = sketch_external_reference_source_owners(
            active_part->session.document(), active_sketch_id_);
        const auto active_document_id = active_part->session.document().document_id;
        const auto top_assembly_id = document.document_id;
        const auto dependent_path = *active_part_occurrence;
        viewer_->set_candidate_filter(
            [this, allowed_local_owners, active_document_id,
             top_assembly_id, dependent_path](const auto& candidate) {
                if (candidate.geometry !=
                        zima::viewer::CandidateGeometry::OriginalReference ||
                    candidate.instance_path.empty()) return false;
                if (candidate.instance_path == dependent_path) {
                    return allowed_local_owners.contains(candidate.owner_id);
                }
                try {
                    const auto address = workspace_.resolve_occurrence(
                        top_assembly_id, zima::assembly::InstancePath::decode(
                            candidate.instance_path));
                    return address && address->source_kind ==
                            zima::assembly::ComponentSourceKind::Part &&
                        address->source_document_id != active_document_id;
                } catch (const std::invalid_argument&) {
                    return false;
                }
            });
    }
    const bool fit_assembly_view = !preserve_view_on_refresh_ && active_sketch_id_.empty();
    preserve_view_on_refresh_ = false;
    if (assembly_cut_rollback_ &&
        assembly_cut_rollback_->assembly_document_id == document.document_id) {
        auto rollback_document = document;
        for (auto& component : rollback_document.components) {
            const auto found = assembly_cut_rollback_->input_component_bodies.find(
                component.occurrence_id);
            if (found != assembly_cut_rollback_->input_component_bodies.end()) {
                component.calculated_source = found->second;
            }
        }
        auto display=active_assembly_display(rollback_document);
        if(active_top_assembly_sketch) {
            if(const auto* sketch=active_sketch()) append_mesh(display,sketch_viewer_mesh(*sketch));
        }
        viewer_->set_mesh(std::move(display), fit_assembly_view);
    } else if (part_rollback_ && !part_rollback_->instance_path.empty()) {
        const auto* active_part =
            workspace_.open_part(part_rollback_->part_document_id);
        if (active_part != nullptr) {
            zima::kernel::BodyResult boundary = part_rollback_->input_body
                .value_or(zima::kernel::BodyResult{});
            viewer_->set_mesh(workspace_.build_scene_with_part_override(
                document.document_id,
                zima::assembly::InstancePath::decode(part_rollback_->instance_path),
                std::move(boundary)), fit_assembly_view);
        } else {
            auto display = active_assembly_display(document);
            if(section_dialog_&&sweep_profile_sketch_draft_)append_mesh(display,sketch_viewer_mesh(*sweep_profile_sketch_draft_));
            if (workspace_.active_document_id() == document.document_id) {
                for (const auto& sketch : document.sketches) {
                    if (!active_sketch_id_.empty() &&
                        sketch.id != active_sketch_id_) continue;
                    auto mesh = sketch.viewer_mesh();
                    if (properties_dialog_ != nullptr && active_sketch_id_.empty()) {
                        remove_sketch_computation_points(mesh);
                    }
                    if (sketch.id != active_sketch_id_) {
                        keep_only_inactive_sketch_profile(mesh,
                            sketch.owner_container_id.empty()
                                ? sketch.id : sketch.owner_container_id);
                    }
                    append_mesh(display, std::move(mesh));
                }
            }
            viewer_->set_mesh(std::move(display), fit_assembly_view);
        }
    } else {
        const auto* active_assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        if (active_assembly != nullptr &&
            active_assembly->session.document().document_id != document.document_id &&
            !active_occurrence_path_.empty()) {
            viewer_->set_mesh(workspace_.build_scene_with_assembly_override(
                document.document_id,
                zima::assembly::InstancePath::decode(active_occurrence_path_),
                active_assembly->session.document()), fit_assembly_view);
        } else if (active_part_occurrence && !active_part_occurrence->empty()) {
            zima::kernel::BodyResult live_source;
            live_source.mesh = sketch_input_mesh(active_part->session);
            append_mesh(live_source.mesh, active_part_origins(active_part->session.document()));
            if (!visible_local_origin_ids_.empty()) {
                append_mesh(live_source.mesh, local_container_context_mesh(
                    active_part->session.document(), visible_local_origin_ids_,
                    viewer_->reference_visible(zima::viewer::ReferenceVisibility::Axes),
                    viewer_->reference_visible(zima::viewer::ReferenceVisibility::Planes)));
            }
            for (const auto& sketch : active_part->session.document().sketches) {
                if (sketch.id == sketch_properties_preview_id_) continue;
                if (sketch.id != active_sketch_id_ &&
                    !sketch_visible_outside_sketcher(
                        active_part->session.document(), sketch)) continue;
                const auto* displayed_sketch = sketch_trim_active_ &&
                        sketch_trim_preview_ && sketch.id == active_sketch_id_
                    ? &*sketch_trim_preview_ : &sketch;
                auto sketch_mesh = sketch_viewer_mesh(*displayed_sketch);
                if (properties_dialog_ != nullptr && active_sketch_id_.empty()) {
                    remove_sketch_computation_points(sketch_mesh);
                }
                if (sketch.id != active_sketch_id_) {
                    keep_only_inactive_sketch_profile(sketch_mesh,
                        sketch.owner_container_id.empty()
                            ? sketch.id : sketch.owner_container_id);
                }
                append_mesh(live_source.mesh, std::move(sketch_mesh));
            }

            append_mesh(live_source.mesh,
                construction_mesh(active_part->session.document(),
                    zima::document::viewer_mesh_bounds_diagonal(
                        live_source.mesh),
                    part_construction_dimension_geometry(
                        active_part->session.document(),
                        active_part->session.calculated_boundaries())));
            if (primitive_origin_preview_mesh_) {
                auto preview = *primitive_origin_preview_mesh_;
                if (!sketch_properties_body_id_.empty())
                    preview = active_part->session.document().place_body_mesh(
                        std::move(preview), sketch_properties_body_id_);
                append_mesh(live_source.mesh, std::move(preview));
            }
            viewer_->set_mesh(workspace_.build_scene_with_part_override(
                document.document_id,
                zima::assembly::InstancePath::decode(*active_part_occurrence),
                std::move(live_source)), fit_assembly_view);
        } else {
            auto display = active_assembly_display(document);
            if(section_dialog_&&sweep_profile_sketch_draft_)append_mesh(display,sketch_viewer_mesh(*sweep_profile_sketch_draft_));
            if (active_top_assembly_sketch) {
                for (const auto& sketch : document.sketches) {
                    if (sketch.id != active_sketch_id_) continue;
                    const auto* shown = sketch_trim_active_ && sketch_trim_preview_
                        ? &*sketch_trim_preview_ : &sketch;
                    append_mesh(display, sketch_viewer_mesh(*shown));
                }
            }
            viewer_->set_mesh(std::move(display), fit_assembly_view);
        }
    }
    state_->setText(document.components.empty()
        ? has_insertable_component()
            ? tr("Nová sestava: zvolte Vložit otevřený dokument.")
            : tr("Nová sestava: nejprve vytvořte a vypočtěte Part, potom jej zde vložte.")
        : tr("Zobrazená sestava: %1\nAktivní dokument: %2")
            .arg(QString::fromStdString(document.name),
                 QString::fromStdString(workspace_.active_document_id())));
    rebuild_insert_menu();
    // A component may be selected from disk even when no other source
    // document is currently open, matching the Python insertion workflow.
    insert_action_->setEnabled(true);
    regenerate_action_->setEnabled(true);
    save_action_->setEnabled(true);
    save_as_action_->setEnabled(true);
    close_document_action_->setEnabled(true);
    regenerate_document_action_->setEnabled(true);
    box_action_->setEnabled(active_part != nullptr);
    cylinder_action_->setEnabled(active_part != nullptr);
    shaft_thread_action_->setEnabled(active_part != nullptr && !active_part->session.document().history.empty());
    thread_action_->setEnabled(active_part != nullptr &&
        !active_part->session.document().history.empty());
    drill_point_action_->setEnabled(active_part != nullptr &&
        !active_part->session.document().history.empty());
    sphere_action_->setEnabled(active_part != nullptr);
    cone_action_->setEnabled(active_part != nullptr);
    pyramid_action_->setEnabled(active_part != nullptr);
    wedge_action_->setEnabled(active_part != nullptr);
    fillet_action_->setEnabled(active_part != nullptr &&
        !active_part->session.document().history.empty());
    chamfer_action_->setEnabled(active_part != nullptr &&
        !active_part->session.document().history.empty());
    shell_action_->setEnabled(active_part != nullptr &&
        !active_part->session.document().history.empty());
    const bool supports_constructions = active_part != nullptr ||
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr;
    construction_point_action_->setEnabled(supports_constructions);
    curve_3d_action_->setEnabled(supports_constructions);
    sweep_3d_action_->setEnabled(active_part != nullptr);
    helical_sweep_action_->setEnabled(active_part != nullptr);
    sweep2d_action_->setEnabled(active_part != nullptr);
    construction_axis_action_->setEnabled(supports_constructions);
    construction_plane_action_->setEnabled(supports_constructions);
    const bool active_assembly_owner =
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr;
    const bool profile_feature_owner = active_part != nullptr || active_assembly_owner;
    extrusion_action_->setEnabled(profile_feature_owner);
    revolution_action_->setEnabled(profile_feature_owner);
    sketch_action_->setEnabled(active_part != nullptr || active_assembly_owner);
    const bool has_active_part_sketch =
        (active_part != nullptr || active_assembly_owner) &&
        !active_sketch_id_.empty();
    sketch_normal_view_action_->setEnabled(has_active_part_sketch);
    sketch_flip_view_action_->setEnabled(has_active_part_sketch);
    sketch_rotate_view_action_->setEnabled(has_active_part_sketch);
    sketch_external_reference_action_->setEnabled(has_active_part_sketch);
    sketch_external_profile_action_->setEnabled(has_active_part_sketch);
    sketch_point_action_->setEnabled(has_active_part_sketch);
    sketch_construction_action_->setEnabled(has_active_part_sketch);
    sketch_segment_action_->setEnabled(has_active_part_sketch);
    sketch_common_tangent_action_->setEnabled(has_active_part_sketch);
    sketch_polyline_action_->setEnabled(has_active_part_sketch);
    sketch_rectangle_action_->setEnabled(has_active_part_sketch);
    sketch_polygon_action_->setEnabled(has_active_part_sketch);
    sketch_trim_action_->setEnabled(has_active_part_sketch);
    sketch_mirror_action_->setEnabled(
        has_active_part_sketch && !sketch_mirror_active_);
    sketch_offset_action_->setEnabled(has_active_part_sketch && !properties_dialog_);
    sketch_circle_action_->setEnabled(has_active_part_sketch);
    sketch_arc_action_->setEnabled(has_active_part_sketch);
    sketch_ellipse_action_->setEnabled(has_active_part_sketch);
    sketch_elliptical_arc_action_->setEnabled(has_active_part_sketch);
    sketch_bspline_action_->setEnabled(has_active_part_sketch);
    sketch_interpolating_spline_action_->setEnabled(has_active_part_sketch);
    sketch_text_action_->setEnabled(has_active_part_sketch);
    sketch_constraints_action_->setEnabled(has_active_part_sketch);
    sketch_dimensions_action_->setEnabled(has_active_part_sketch);
    const bool has_segment = has_active_part_sketch &&
        !selected_sketch_segment_id_.empty();
    sketch_horizontal_action_->setEnabled(has_active_part_sketch);
    sketch_vertical_action_->setEnabled(has_active_part_sketch);
    sketch_coincident_action_->setEnabled(has_active_part_sketch);
    sketch_midpoint_action_->setEnabled(has_active_part_sketch);
    sketch_symmetric_action_->setEnabled(has_active_part_sketch);
    sketch_concentric_action_->setEnabled(has_active_part_sketch);
    sketch_tangent_action_->setEnabled(has_active_part_sketch);
    sketch_parallel_action_->setEnabled(has_active_part_sketch);
    sketch_perpendicular_action_->setEnabled(has_active_part_sketch);
    sketch_equal_length_action_->setEnabled(has_active_part_sketch);
    sketch_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_universal_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_dimension_x_action_->setEnabled(has_active_part_sketch);
    sketch_dimension_y_action_->setEnabled(has_active_part_sketch);
    sketch_point_line_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_symmetric_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_three_point_angle_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_angle_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_radius_dimension_action_->setEnabled(has_active_part_sketch &&
        (!selected_sketch_circle_id_.empty() || !selected_sketch_arc_id_.empty()));
    sketch_diameter_dimension_action_->setEnabled(has_active_part_sketch &&
        (!selected_sketch_circle_id_.empty() || !selected_sketch_arc_id_.empty()));
    sketch_ellipse_major_dimension_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_ellipse_id_.empty());
    sketch_ellipse_minor_dimension_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_ellipse_id_.empty());
    sketch_ellipse_rotation_dimension_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_ellipse_id_.empty());
    sketch_fix_point_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_point_id_.empty());
    finish_sketch_action_->setEnabled(has_active_part_sketch);
    regenerate_part_action_->setEnabled(active_part != nullptr);
    if (active_part != nullptr) {
        undo_action_->setEnabled(active_part->session.can_undo());
        redo_action_->setEnabled(active_part->session.can_redo());
    } else {
        const auto* active_assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        undo_action_->setEnabled(active_assembly != nullptr &&
                                 active_assembly->session.can_undo());
        redo_action_->setEnabled(active_assembly != nullptr &&
                                 active_assembly->session.can_redo());
    }
    update_application_actions();
    rebuild_application_toolbar();

    if (edge_treatment_selection_) {
        // set_mesh() rebuilds the common candidate stream. Restore the exact
        // active edge command and regenerate its analytical wire from the
        // newly displayed rollback/input body, never from stale final output.
        refresh_edge_treatment_selection_ui();
    }
    if (shell_dialog_ != nullptr) {
        refresh_shell_selection_ui();
    }
    // An Up-to combo change publishes a live extrusion preview immediately.
    // That refresh rebuilds the ordinary Part/Assembly selection contract and
    // set_selection_contract() clears the command-local candidate filter.
    // Restore the active Up-to command last, after every mesh/toolbar refresh,
    // so global Selection state and preview updates can never disable its
    // Face/Plane hover.
    if (extrusion_target_dialog_ != nullptr) {
        apply_extrusion_target_selection_contract();
    }
    // Keep inspection visual only: confirming the container here would
    // suppress subsequent dimension hover and inline editing.
    viewer_->set_container_inspection(
        properties_dialog_ == nullptr ? construction_dimension_object_id_ : std::string{},
        active_part != nullptr ? resolve_active_occurrence(
            active_part->session.document().document_id).value_or(std::string{}) : std::string{});
    configure_sketch_box_selection(has_active_part_sketch);
    update_section_ui();
    update_measurement_ui();
}

} // namespace zima::app

#include "workspace_internal.hpp"

namespace zima::app::workspace_detail {


void append_nonzero_parameter_dimensions(
    std::vector<zima::kernel::ViewerDimension>& target,
    std::vector<zima::kernel::ViewerDimension> source) {
    std::erase_if(source, [](const auto& dimension) {
        return std::abs(dimension.value) <=
            visible_parameter_dimension_epsilon;
    });
    target.insert(target.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end()));
}

std::optional<std::array<double, 2>> sketch_point_reference_position(
    const zima::sketcher::Sketch& sketch, const std::string& reference_id) {
    if (const auto* point = sketch.find_point(reference_id)) {
        return std::array{point->x, point->y};
    }
    if (reference_id == "sketch_origin") {
        return std::array{0.0, 0.0};
    }
    const auto external = std::ranges::find_if(
        sketch.external_references, [&](const auto& reference) {
            return reference.id == reference_id &&
                reference.kind == zima::sketcher::ExternalReferenceKind::Point &&
                reference.cached_points.size() == 1;
        });
    return external == sketch.external_references.end()
        ? std::nullopt
        : std::optional<std::array<double, 2>>{
              external->cached_points.front()};
}

// An axis/construction reference for the universal dimension tool: either a
// global sketch axis, or a native segment explicitly drawn as construction
// (centerline) geometry. Distinguishing this from an ordinary real line lets
// the "line, axis, line" click sequence offer a symmetric dimension.
bool sketch_axis_like_reference(
    const zima::sketcher::Sketch& sketch, const std::string& id) {
    if (id.starts_with("sketch_axis:")) return true;
    const auto segment = std::ranges::find_if(sketch.segments,
        [&](const auto& value) { return value.id == id; });
    return segment != sketch.segments.end() && segment->construction;
}

std::optional<std::array<double, 2>> exact_circle_tangent_contact(
    const zima::sketcher::Sketch& sketch,
    const std::array<double, 2>& start,
    const std::string& circle_id,
    const std::array<double, 2>& hint) {
    const auto circle = std::ranges::find_if(sketch.circles,
        [&](const auto& value) { return value.id == circle_id; });
    if (circle == sketch.circles.end()) return std::nullopt;
    const auto* center = sketch.find_point(circle->center_point_id);
    if (center == nullptr) return std::nullopt;
    const double px = start[0] - center->x;
    const double py = start[1] - center->y;
    const double distance_squared = px * px + py * py;
    const double radius_squared = circle->radius * circle->radius;
    if (distance_squared <= radius_squared + 1.0e-12) return std::nullopt;
    const double radial = radius_squared / distance_squared;
    const double lateral = circle->radius *
        std::sqrt(distance_squared - radius_squared) / distance_squared;
    const std::array first{
        center->x + radial * px - lateral * py,
        center->y + radial * py + lateral * px};
    const std::array second{
        center->x + radial * px + lateral * py,
        center->y + radial * py - lateral * px};
    return std::hypot(first[0] - hint[0], first[1] - hint[1]) <=
            std::hypot(second[0] - hint[0], second[1] - hint[1])
        ? std::optional{first} : std::optional{second};
}

double rounded_to_decimal_places(double value, int decimal_places) {
    const double scale = std::pow(
        10.0, std::clamp(decimal_places, 0, 12));
    const double rounded = std::round(value * scale) / scale;
    return std::abs(rounded) < 0.5 / scale ? 0.0 : rounded;
}

void normalize_owned_profile_front_references(
        std::vector<zima::document::ConstructionReference>& references,
        bool preserve_front_through_origin_triad) {
    const auto first = std::find_if(references.begin(), references.end(),
        [](const auto& reference) {
            return !reference.orientation_only && reference.supports_offset &&
                !reference.owner_id.empty();
        });
    if (first == references.end()) return;
    const auto owner = first->owner_id;
    const auto path = first->instance_path;
    const auto semantic = first->semantic_key;
    const auto same_source = [&](const auto& reference) {
        return reference.owner_id == owner &&
            reference.instance_path == path &&
            reference.semantic_key == semantic;
    };
    const auto second_plane = std::find_if(std::next(first), references.end(),
        [&](const auto& reference) {
            return !reference.orientation_only && reference.supports_offset &&
                !same_source(reference);
        });
    for (auto& reference : references) {
        if (reference.orientation_only) continue;
        const bool front = same_source(reference);
        const bool top = second_plane != references.end() &&
            &reference == &*second_plane;
        reference.orientation_drives_rotation = front || top;
        reference.orientation_role = front ? "front" : top ? "top" : "none";
    }
    std::erase_if(references, [&](const auto& reference) {
        return reference.orientation_only && !same_source(reference);
    });
    if (preserve_front_through_origin_triad) {
        // resolve_placement() deliberately collapses the ordinary three
        // planes of the main Origin to the document identity frame.  An
        // owned Sketch/profile is different: its first positional plane is
        // explicitly its FRONT, also while the feature dialog is only
        // showing a transient preview.  The calculated Sketch path already
        // supplies this non-persisted orientation twin; do the same here so
        // the cyan local origin cannot jump to the last reference until the
        // first profile calculation/drag refreshes it.
        auto explicit_front = *first;
        explicit_front.orientation_only = true;
        explicit_front.orientation_drives_rotation = true;
        explicit_front.orientation_role = "front";
        references.push_back(std::move(explicit_front));
    }
}

zima::kernel::Vec3 euler_degrees_from_frame_columns(
        const zima::kernel::Vec3& x_axis,
        const zima::kernel::Vec3& y_axis,
        const zima::kernel::Vec3& z_axis) {
    const std::array<std::array<double, 3>, 3> matrix{{
        {x_axis.x, y_axis.x, z_axis.x},
        {x_axis.y, y_axis.y, z_axis.y},
        {x_axis.z, y_axis.z, z_axis.z}}};
    constexpr double degrees_per_radian = 180.0 / std::numbers::pi;
    const double ry = std::asin(std::clamp(-matrix[2][0], -1.0, 1.0));
    double rx{};
    double rz{};
    if (std::abs(std::cos(ry)) > 1.0e-10) {
        rx = std::atan2(matrix[2][1], matrix[2][2]);
        rz = std::atan2(matrix[1][0], matrix[0][0]);
    } else {
        rx = std::atan2(
            matrix[0][1] * (ry >= 0.0 ? 1.0 : -1.0), matrix[1][1]);
    }
    return {rx * degrees_per_radian, ry * degrees_per_radian,
        rz * degrees_per_radian};
}

ContainerDimensionFrame container_dimension_frame(
        const zima::document::Placement& placement) {
    constexpr double radians = std::numbers::pi / 180.0;
    const double cx = std::cos(placement.rotation_x * radians);
    const double sx = std::sin(placement.rotation_x * radians);
    const double cy = std::cos(placement.rotation_y * radians);
    const double sy = std::sin(placement.rotation_y * radians);
    const double cz = std::cos(placement.rotation_z * radians);
    const double sz = std::sin(placement.rotation_z * radians);
    return {{placement.x, placement.y, placement.z}, {{
        {{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx}},
        {{sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx}},
        {{-sy, cy * sx, cy * cx}}
    }}};
}

// Every primitive solid shares the universal container placement UI
// (position/orientation reference tables) wired in PrimitivePropertiesDialog.
bool supports_placement_reference_picking(zima::document::FeatureKind kind) {
    using zima::document::FeatureKind;
    return kind == FeatureKind::Box || kind == FeatureKind::Cylinder ||
        kind == FeatureKind::Sphere || kind == FeatureKind::Cone ||
        kind == FeatureKind::Pyramid || kind == FeatureKind::Wedge ||
        kind == FeatureKind::Extrusion || kind == FeatureKind::Revolution ||
        kind == FeatureKind::ImportedStep || kind == FeatureKind::Hole ||
        kind == FeatureKind::Thread;
}

bool sketch_visible_outside_sketcher(
    const zima::document::PartDocument& document,
    const zima::sketcher::Sketch& sketch) {
    const auto* owner = document.find_container(sketch.owner_container_id);
    return owner != nullptr &&
        owner->feature_kind == zima::document::FeatureKind::Sketch;
}

// A picked reference supports an editable offset when it is a planar
// surface: either a solid/sketch Face, or a construction/origin datum Plane
// (Container candidate whose semantic_key resolves to "plane"), matching
// Python's `_reference_supports_offset()` (also true for EntityKind.PLANE).
bool candidate_supports_offset(const zima::viewer::ViewerCandidate& candidate) {
    return candidate.kind == zima::viewer::CandidateKind::Plane ||
        candidate.kind == zima::viewer::CandidateKind::Face;
}

// A picked reference is a directional (planar/linear) candidate when it is a
// Face, Edge or Axis -- including the persisted origin plane/axis overlays,
// whose semantic keys are "origin:plane:*"/"origin:axis:*" -- matching
// Python's `is_orientation_candidate` test in `_add_reference()`.  Such a
// reference simultaneously fixes both position AND, unlike a bare vertex,
// part of the object's orientation.
bool candidate_drives_rotation(const zima::viewer::ViewerCandidate& candidate) {
    return candidate.kind == zima::viewer::CandidateKind::Plane ||
        candidate.kind == zima::viewer::CandidateKind::Face ||
        candidate.kind == zima::viewer::CandidateKind::Edge ||
        candidate.kind == zima::viewer::CandidateKind::Axis ||
        candidate.semantic_key.starts_with("origin:plane:") ||
        candidate.semantic_key.starts_with("origin:axis:");
}

bool placement_angle_uses_reference_correction(
        const std::vector<zima::document::ConstructionReference>& references,
        const zima::kernel::ViewerReferenceGeometry& geometry,
        const zima::kernel::Vec3& origin, std::size_t axis_index) {
    if (axis_index >= 3 || !std::any_of(references.begin(), references.end(),
            [](const auto& reference) {
                return reference.orientation_drives_rotation;
            })) {
        return false;
    }
    // A partially referenced frame mixes both value classes: constrained
    // axes expose corrections, while its remaining DOF stays an absolute
    // angle. Decide per axis exactly like the shared Properties fields and
    // container_placement_dimensions(), not once for the whole frame.
    return zima::document::orientation_constraint_state(
        references, geometry, true, origin).constrained_axes[axis_index];
}

// Whether the classic history-order shortcut ("1st point = origin, 2nd =
// axis direction" for an Axis; "1st point = origin, 2nd/3rd = plane-defining
// points" for a Plane) already has every point it needs -- used to stop
// offering further reference rows once the container's direction/normal is
// fully determined this way, since the generic rotation-DOF count cannot
// see it (a bare point/vertex is never marked orientation-driving).
bool construction_shortcut_satisfied(zima::document::ConstructionKind kind,
    const std::vector<zima::document::ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    const std::size_t required =
        kind == zima::document::ConstructionKind::Axis ? 2
        : kind == zima::document::ConstructionKind::Plane ? 3 : 0;
    if (required == 0 || references.size() < required) return false;
    return std::all_of(references.begin(), references.end(),
        [&](const auto& reference) {
            return !reference.orientation_drives_rotation &&
                zima::document::construction_reference_is_point(
                    reference, geometry);
        });
}

// Assigns the next unused orientation role ("normal" first, then "up") to a
// newly accepted Point position reference that drives rotation, matching
// Python's `_default_orientation_role()`/`_ensure_automatic_orientation_roles()`.
// A Point container has no dedicated orientation-reference table: the same
// position reference simultaneously participates in placement (equations
// solved by `resolve_construction`) and, once marked, in the rotation-DOF
// count via `orientation_constraint_remaining_dof(..., marked_only=true)`.
void assign_automatic_orientation_role(
    zima::document::ConstructionReference& reference,
    const std::vector<zima::document::ConstructionReference>& existing) {
    std::set<std::string> used_roles;
    for (const auto& other : existing) {
        if (other.orientation_drives_rotation) used_roles.insert(other.orientation_role);
    }
    reference.orientation_role = !used_roles.contains("front") ? "front"
        : !used_roles.contains("top") ? "top" : "none";
    reference.orientation_drives_rotation = reference.orientation_role != "none";
}


zima::workspace::NativeTemplateSettings native_template_settings(const ApplicationSettings& settings) {
    return {std::filesystem::u8path(settings.resolved_paths.value("Templates").toStdString()),
        std::filesystem::u8path(settings.part_template.toStdString()),
        std::filesystem::u8path(settings.assembly_template.toStdString()),
        QObject::tr("Těleso 1").toStdString()};
}

zima::document::PartDocument new_part_from_template(const ApplicationSettings& settings) {
    return zima::workspace::part_from_template(native_template_settings(settings));
}

zima::assembly::AssemblyDocument new_assembly_from_template(const ApplicationSettings& settings) {
    return zima::workspace::assembly_from_template(native_template_settings(settings));
}

void append_reference_geometry(
    zima::kernel::ViewerReferenceGeometry& target,
    zima::kernel::ViewerReferenceGeometry source) {
    const auto vertex_offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(
        target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (const auto index : source.triangles) {
        target.triangles.push_back(index + vertex_offset);
    }
    target.triangle_references.insert(target.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
}

void append_mesh(zima::kernel::ViewerMesh& target, zima::kernel::ViewerMesh source) {
    target.images.insert(target.images.end(),std::make_move_iterator(source.images.begin()),std::make_move_iterator(source.images.end()));
    const auto vertex_offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (const auto index : source.triangles) target.triangles.push_back(index + vertex_offset);
    target.triangle_references.insert(target.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
    target.dimensions.insert(target.dimensions.end(),
        source.dimensions.begin(), source.dimensions.end());
    target.constraint_markers.insert(target.constraint_markers.end(),
        source.constraint_markers.begin(), source.constraint_markers.end());
    append_reference_geometry(
        target.original_references, std::move(source.original_references));
}

zima::kernel::ViewerMesh local_origin_display_mesh(
        const zima::kernel::ViewerReferenceGeometry& geometry,
        const std::set<std::string>& visible_origin_ids) {
    zima::kernel::ViewerMesh mesh;
    // history_origin_reference_geometry_before() deliberately carries the
    // same canonical reference geometry as Document Origin.  POČÁTEK is only
    // a presentation of a container-local frame, whose established nominal
    // plane size is 5 versus 10 for Document Origin. Keep that distinction in
    // this display-only copy; reference identity and placement math remain
    // untouched.
    constexpr double local_origin_display_scale = 0.5;
    for (const auto& axis : geometry.axes) {
        if (!visible_origin_ids.contains(axis.reference.owner_id) ||
            !axis.reference.semantic_key.starts_with("origin:")) continue;
        auto displayed = axis;
        displayed.display_length *= local_origin_display_scale;
        mesh.axes.push_back(std::move(displayed));
    }
    for (const auto& point : geometry.points) {
        if (visible_origin_ids.contains(point.reference.owner_id) &&
            (point.reference.semantic_key == "origin:point" ||
             point.reference.semantic_key == "point"))
            mesh.points.push_back(point);
    }
    for (std::size_t triangle = 0;
         triangle + 1 < geometry.triangle_references.size(); triangle += 2) {
        const auto& reference = geometry.triangle_references[triangle];
        if (!visible_origin_ids.contains(reference.owner_id) ||
            !reference.semantic_key.starts_with("origin:plane:") ||
            geometry.triangle_references[triangle + 1] != reference) continue;
        const std::size_t offset = triangle * 3;
        if (offset + 5 >= geometry.triangles.size()) continue;
        const auto a = geometry.triangles[offset];
        const auto b = geometry.triangles[offset + 1];
        const auto c = geometry.triangles[offset + 2];
        const auto d = geometry.triangles[offset + 5];
        if (std::max({a, b, c, d}) >= geometry.vertices.size()) continue;
        zima::kernel::ViewerEdge edge;
        edge.points = {geometry.vertices[a], geometry.vertices[b],
            geometry.vertices[c], geometry.vertices[d], geometry.vertices[a]};
        zima::kernel::Vec3 center;
        for (std::size_t index = 0; index < 4; ++index) {
            center.x += edge.points[index].x;
            center.y += edge.points[index].y;
            center.z += edge.points[index].z;
        }
        center = {center.x/4.0, center.y/4.0, center.z/4.0};
        for (auto& point : edge.points) {
            point = {center.x + (point.x-center.x)*local_origin_display_scale,
                     center.y + (point.y-center.y)*local_origin_display_scale,
                     center.z + (point.z-center.z)*local_origin_display_scale};
        }
        edge.reference = {reference.owner_id, reference.semantic_key,
            reference.instance_path};
        edge.overlay = true;
        mesh.edges.push_back(std::move(edge));
    }
    return mesh;
}

void keep_only_inactive_sketch_profile(
    zima::kernel::ViewerMesh& mesh,
    const std::string& display_owner_id) {
    // Ordinary Part/Assembly view presents a Sketch as its clean result
    // profile. Editing aids belong exclusively to active Sketcher.
    std::erase_if(mesh.edges, [](const auto& edge) {
        const auto& key = edge.reference.semantic_key;
        const bool native_profile = key.starts_with("segment:") ||
            key.starts_with("circle:") || key.starts_with("arc:") ||
            key.starts_with("corner_radius:") ||
            key.starts_with("ellipse:") ||
            key.starts_with("elliptical_arc:") ||
            key.starts_with("bspline:") || key.starts_with("text:");
        return edge.construction || !native_profile;
    });
    // In ordinary Part/Assembly interaction the history Container is the
    // selectable object. Keep the persisted Sketch identity on each edge for
    // Sketcher and direct child-row interaction, while assigning the visible
    // wire to its parent Container for hover/LMB/Tree synchronization.
    for (auto& edge : mesh.edges) {
        edge.display_owner_id = display_owner_id;
    }
    std::erase_if(mesh.points, [](const auto& point) {
        return point.construction ||
            (!point.reference.semantic_key.starts_with("point:") &&
             point.reference.semantic_key !=
                "external_point:sketch_origin");
    });
    // Keep the work-plane origin available for whole-Sketch highlighting,
    // but do not publish it as an ordinary external reference or draw it
    // while the inactive Sketch is idle.  It is a presentation companion of
    // the Sketch container, not an independently selectable object here.
    for (auto& point : mesh.points) {
        if (point.reference.semantic_key ==
            "external_point:sketch_origin") {
            point.reference.semantic_key = "sketch:origin-marker";
            point.always_visible = false;
        }
    }
    mesh.axes.clear();
    mesh.dimensions.clear();
    mesh.constraint_markers.clear();
    mesh.original_references = {};
}

void remove_sketch_computation_points(zima::kernel::ViewerMesh& mesh) {
    // Segment midpoints are live Sketcher inference/picking aids. They are
    // useful only while the Sketch itself is active and must not leak into
    // the intermediate Properties preview of Sketch, Extrusion or
    // Revolution.
    std::erase_if(mesh.points, [](const auto& point) {
        return point.reference.semantic_key.starts_with("sketch_midpoint:");
    });
    std::erase_if(mesh.original_references.points, [](const auto& point) {
        return point.reference.semantic_key.starts_with("sketch_midpoint:");
    });
}

QTreeWidgetItem* add_origin_tree_item(QTreeWidgetItem* parent,
    const std::string& document_id, bool assembly,
    const zima::assembly::InstancePath& instance_path) {
    auto* origin = new QTreeWidgetItem(parent, {
        assembly ? QObject::tr("Počátek sestavy") : QObject::tr("Počátek dílu")});
    origin->setIcon(0, resource_icon("origin"));
    origin->setData(0, Qt::UserRole,
        QString::fromStdString(document_id + ":origin"));
    origin->setData(0, Qt::UserRole + 1,
        QString::fromStdString(instance_path.encoded()));
    origin->setData(0, Qt::UserRole + 3, "document-origin");
    const std::array children{
        std::pair{QObject::tr("Point"), "point"},
        std::pair{QObject::tr("X Axis"), "axis:x"},
        std::pair{QObject::tr("Y Axis"), "axis:y"},
        std::pair{QObject::tr("Z Axis"), "axis:z"},
        std::pair{QObject::tr("XY Plane"), "plane:xy"},
        std::pair{QObject::tr("YZ Plane"), "plane:yz"},
        std::pair{QObject::tr("XZ Plane"), "plane:xz"}};
    for (const auto& [label, key] : children) {
        auto* child = new QTreeWidgetItem(origin, {label});
        child->setIcon(0, resource_icon(
            key == std::string_view("point") ? "point" :
            std::string_view(key).starts_with("axis:") ? "axis" : "plane"));
        child->setData(0, Qt::UserRole,
            QString::fromStdString(document_id + ":origin"));
        child->setData(0, Qt::UserRole + 1,
            QString::fromStdString(instance_path.encoded()));
        child->setData(0, Qt::UserRole + 3, "origin-reference");
        child->setData(0, Qt::UserRole + 5,
            QString::fromStdString(std::string("origin:") + key));
    }
    return origin;
}



// Port of Python's camera_angles_for_view_direction() (viewer.py:264):
// maps a world viewing direction onto (yaw_degrees, pitch_degrees).
std::pair<double, double> camera_angles_for_view_direction(
    const zima::kernel::Vec3& direction) {
    const double length = std::sqrt(direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z);
    if (length <= 1e-12) return {0.0, 0.0};
    const double x = direction.x / length;
    const double y = direction.y / length;
    const double z = direction.z / length;
    const double horizontal = std::hypot(x, y);
    const double yaw = horizontal > 1e-12
        ? std::atan2(x, y) * 180.0 / std::numbers::pi : 0.0;
    const double pitch = std::atan2(-horizontal, -z) * 180.0 / std::numbers::pi;
    return {yaw, pitch};
}

// Port of Python's _camera_roll_for_direction() (app.py:38114): aligns a
// world direction to a requested screen-space angle (used to roll the
// camera so the secondary orientation reference points TOP/BOTTOM/LEFT/RIGHT).
double camera_roll_for_direction(
    const zima::kernel::Vec3& view_direction,
    const zima::kernel::Vec3& world_direction,
    double target_angle_degrees) {
    const auto [yaw_degrees, pitch_degrees] =
        camera_angles_for_view_direction(view_direction);
    const double yaw = yaw_degrees * std::numbers::pi / 180.0;
    const double pitch = pitch_degrees * std::numbers::pi / 180.0;
    const double length = std::sqrt(world_direction.x * world_direction.x +
        world_direction.y * world_direction.y +
        world_direction.z * world_direction.z);
    if (length <= 1e-12) return 0.0;
    const double dx = world_direction.x / length;
    const double dy = world_direction.y / length;
    const double dz = world_direction.z / length;
    const double yaw_x = std::cos(yaw) * dx - std::sin(yaw) * dy;
    const double yaw_y = std::sin(yaw) * dx + std::cos(yaw) * dy;
    const double screen_y = std::cos(pitch) * yaw_y - std::sin(pitch) * dz;
    if (std::hypot(yaw_x, screen_y) <= 1e-9) return 0.0;
    return target_angle_degrees -
        std::atan2(screen_y, yaw_x) * 180.0 / std::numbers::pi;
}


} // namespace zima::app::workspace_detail

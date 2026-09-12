#include "workspace_internal.hpp"

namespace zima::app::workspace_detail {


std::optional<std::string> sketch_text_id_from_key(const std::string& key) {
    return zima::sketcher::text_id_from_viewer_key(key);
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
        if (!result.empty()) {
            throw std::runtime_error(
                "Skica rotace smí obsahovat právě jednu zelenou konstrukční osu.");
        }
        result = segment.id;
    }
    if (result.empty()) {
        throw std::runtime_error(
            "Ve skice rotace nakreslete zelenou konstrukční osu.");
    }
    return result;
}

std::optional<RevolutionCueFrame> revolution_cue_frame(
    const zima::sketcher::Sketch& sketch, const std::string& axis_segment_id) {
    const auto segment = std::find_if(sketch.segments.begin(),
        sketch.segments.end(), [&](const auto& value) {
            return value.id == axis_segment_id && value.construction &&
                value.centerline;
        });
    if (segment == sketch.segments.end()) return std::nullopt;
    const auto* first_point = sketch.find_point(segment->first_point_id);
    const auto* second_point = sketch.find_point(segment->second_point_id);
    if (first_point == nullptr || second_point == nullptr) return std::nullopt;
    const auto first = sketch.world_point(first_point->x, first_point->y);
    const auto second = sketch.world_point(second_point->x, second_point->y);
    zima::kernel::Vec3 axis{second.x - first.x, second.y - first.y,
        second.z - first.z};
    const double axis_norm = std::hypot(std::hypot(axis.x, axis.y), axis.z);
    if (axis_norm <= 1.0e-12) return std::nullopt;
    axis = {axis.x / axis_norm, axis.y / axis_norm, axis.z / axis_norm};

    zima::kernel::Vec3 centroid{};
    std::size_t point_count = 0;
    for (const auto& edge : sketch.viewer_mesh().edges) {
        if (edge.construction || edge.reference.owner_id != sketch.id ||
            edge.reference.semantic_key.starts_with("external_")) {
            continue;
        }
        for (const auto& point : edge.points) {
            centroid.x += point.x;
            centroid.y += point.y;
            centroid.z += point.z;
            ++point_count;
        }
    }
    if (point_count == 0) return std::nullopt;
    centroid = {centroid.x / static_cast<double>(point_count),
        centroid.y / static_cast<double>(point_count),
        centroid.z / static_cast<double>(point_count)};
    const zima::kernel::Vec3 from_axis{centroid.x - first.x,
        centroid.y - first.y, centroid.z - first.z};
    const double axial = from_axis.x * axis.x + from_axis.y * axis.y +
        from_axis.z * axis.z;
    const zima::kernel::Vec3 center{first.x + axis.x * axial,
        first.y + axis.y * axial, first.z + axis.z * axial};
    zima::kernel::Vec3 radial{centroid.x - center.x,
        centroid.y - center.y, centroid.z - center.z};
    double radial_norm = std::hypot(std::hypot(radial.x, radial.y), radial.z);
    if (radial_norm <= 1.0e-12) {
        radial = {
            sketch.resolved_normal.y * axis.z - sketch.resolved_normal.z * axis.y,
            sketch.resolved_normal.z * axis.x - sketch.resolved_normal.x * axis.z,
            sketch.resolved_normal.x * axis.y - sketch.resolved_normal.y * axis.x};
        radial_norm = std::hypot(std::hypot(radial.x, radial.y), radial.z);
    }
    if (radial_norm <= 1.0e-12) return std::nullopt;
    constexpr double cue_radius = 25.0;
    radial = {radial.x / radial_norm * cue_radius,
        radial.y / radial_norm * cue_radius,
        radial.z / radial_norm * cue_radius};
    return RevolutionCueFrame{center, axis, radial};
}

zima::kernel::ViewerMesh local_container_context_mesh(
        const zima::document::PartDocument& document,
        const std::set<std::string>& visible_origin_ids,
        bool show_axes, bool show_planes) {
    auto mesh = local_origin_display_mesh(
        document.history_origin_reference_geometry_before({}),
        visible_origin_ids);
    // Match the fixed full edge length of every local Container Origin
    // plane.  These helpers must not grow with the model or camera fit.
    constexpr double context_extent = 5.0;
    const auto append_axis = [&](const auto& container,
            zima::kernel::Vec3 point, zima::kernel::Vec3 direction) {
        if (!show_axes) return;
        const double length = std::hypot(
            std::hypot(direction.x, direction.y), direction.z);
        if (length <= 1.0e-12) return;
        direction = {direction.x / length, direction.y / length,
            direction.z / length};
        zima::kernel::ViewerAxis axis{point, direction, context_extent,
            {container.id, "axis:primary", {}}};
        axis.label = QObject::tr("Osa prvku").toStdString();
        mesh.axes.push_back(std::move(axis));
    };
    const auto append_plane = [&](const auto& container,
            const zima::kernel::Vec3& center,
            const zima::kernel::Vec3& x,
            const zima::kernel::Vec3& y) {
        if (!show_planes) return;
        const double half = context_extent * 0.5;
        const auto corner = [&](double a, double b) {
            return zima::kernel::Vec3{center.x + a*x.x + b*y.x,
                center.y + a*x.y + b*y.y, center.z + a*x.z + b*y.z};
        };
        zima::kernel::ViewerEdge plane;
        plane.points = {corner(-half, -half), corner(half, -half),
            corner(half, half), corner(-half, half), corner(-half, -half)};
        // MeshView presents every non-Origin work plane through the shared
        // `border` semantic. A custom key left this valid rectangle outside
        // the plane renderer, so POČÁTEK appeared to do nothing.
        plane.reference = {container.id, "border", {}};
        plane.overlay = true;
        mesh.edges.push_back(std::move(plane));
        const auto& points = mesh.edges.back().points;
        const auto offset = static_cast<std::uint32_t>(
            mesh.original_references.vertices.size());
        mesh.original_references.vertices.insert(
            mesh.original_references.vertices.end(), points.begin(),
            points.begin() + 4);
        mesh.original_references.triangles.insert(
            mesh.original_references.triangles.end(),
            {offset, offset + 1, offset + 2,
             offset, offset + 2, offset + 3});
        mesh.original_references.triangle_references.insert(
            mesh.original_references.triangle_references.end(), 2,
            {container.id, "plane", {}});
    };
    for (const auto& container : document.history) {
        if (!visible_origin_ids.contains(container.container_origin.id) ||
            container.suppressed) continue;
        const auto frame = container_dimension_frame(container.placement);
        if (container.feature_kind == zima::document::FeatureKind::Cylinder ||
            container.feature_kind == zima::document::FeatureKind::Cone ||
            container.feature_kind == zima::document::FeatureKind::Hole) {
            append_axis(container, frame.origin, frame.vector({0.0, 0.0, 1.0}));
        }
        if (container.feature_kind == zima::document::FeatureKind::Hole) {
            append_plane(container, frame.origin, frame.vector({1.0, 0.0, 0.0}),
                frame.vector({0.0, 1.0, 0.0}));
        }
        if (container.feature_kind != zima::document::FeatureKind::Extrusion &&
            container.feature_kind != zima::document::FeatureKind::Revolution &&
            container.feature_kind != zima::document::FeatureKind::Sketch) continue;
        const std::string sketch_id = container.feature_kind ==
                zima::document::FeatureKind::Extrusion
            ? container.extrusion.sketch_id
            : container.feature_kind == zima::document::FeatureKind::Revolution
                ? container.revolution.sketch_id : std::string{};
        const auto sketch = std::ranges::find_if(document.sketches,
            [&](const auto& value) {
                return (!sketch_id.empty() && value.id == sketch_id) ||
                    value.owner_container_id == container.id;
            });
        if (sketch == document.sketches.end()) continue;
        append_plane(container, sketch->resolved_origin,
            sketch->resolved_x_axis, sketch->resolved_y_axis);
        if (container.feature_kind == zima::document::FeatureKind::Revolution) {
            if (const auto cue = revolution_cue_frame(
                    *sketch, container.revolution.axis_segment_id))
                append_axis(container, cue->center, cue->axis);
        }
    }
    return mesh;
}

std::optional<std::string> sketch_external_reference_id_from_key(
    const std::string& key) {
    const std::string_view edge_prefix{"external_edge:"};
    const std::string_view point_prefix{"external_point:"};
    const std::string_view axis_prefix{"external_axis:"};
    const std::string_view face_prefix{"external_face:"};
    const auto prefix = key.starts_with(edge_prefix) ? edge_prefix
        : key.starts_with(point_prefix) ? point_prefix
        : key.starts_with(axis_prefix) ? axis_prefix
        : key.starts_with(face_prefix) ? face_prefix : std::string_view{};
    if (prefix.empty() || key.size() == prefix.size()) return std::nullopt;
    const auto suffix = key.find(':', prefix.size());
    return key.substr(prefix.size(), suffix == std::string::npos
        ? std::string::npos : suffix - prefix.size());
}

std::optional<std::string> sketch_keypoint_curve_id(
    const std::string& key) {
    constexpr std::string_view prefix{"sketch_keypoint:"};
    if (!key.starts_with(prefix)) return std::nullopt;
    const auto kind_separator = key.find(':', prefix.size());
    const auto quarter_separator = key.rfind(':');
    if (kind_separator == std::string::npos ||
        quarter_separator == kind_separator) return std::nullopt;
    return key.substr(kind_separator + 1,
        quarter_separator - kind_separator - 1);
}

std::optional<std::pair<std::string,std::string>> common_tangent_supports(
    const zima::sketcher::Sketch& sketch, const std::string& first_support,
    const std::string& second_support, const std::array<double,2>& first,
    const std::array<double,2>& second, double tolerance) {
    const auto a=sketch_keypoint_curve_id(first_support).value_or(first_support);
    const auto b=sketch_keypoint_curve_id(second_support).value_or(second_support);
    if(a.empty() || b.empty() || a==b)return std::nullopt;
    const auto ta=sketch.curve_tangent_at_point(a,first[0],first[1]);
    const auto tb=sketch.curve_tangent_at_point(b,second[0],second[1]);
    const double dx=second[0]-first[0],dy=second[1]-first[1];
    if(!ta || !tb || std::abs(dx*(*ta)[1]-dy*(*ta)[0])>tolerance ||
        std::abs(dx*(*tb)[1]-dy*(*tb)[0])>tolerance)return std::nullopt;
    return std::pair{a,b};
}

std::string apply_sketch_point_snap(zima::sketcher::Sketch& sketch,
    const std::string& point_id, const std::string& support_id,
    const std::optional<zima::sketcher::ConstraintKind>& kind) {
    if (!kind || support_id.empty()) return point_id;
    try {
        if (*kind==zima::sketcher::ConstraintKind::Horizontal || *kind==zima::sketcher::ConstraintKind::Vertical) {
            if (point_id!=support_id) static_cast<void>(sketch.add_point_pair_constraint(support_id,point_id,*kind));
        } else if (*kind == zima::sketcher::ConstraintKind::Coincident) {
            if (point_id == support_id) return point_id;
            if (sketch.find_point(support_id) != nullptr) {
                return sketch.merge_points(support_id, point_id);
            }
            static_cast<void>(sketch.add_point_reference_constraint(
                point_id, support_id));
        } else if (*kind == zima::sketcher::ConstraintKind::PointOnCircle) {
            static_cast<void>(sketch.add_point_on_circle_constraint(
                point_id, support_id));
        } else if (*kind == zima::sketcher::ConstraintKind::PointOnLine) {
            const auto separator = support_id.find("||");
            if (separator == std::string::npos) {
                static_cast<void>(sketch.add_point_on_line_constraint(
                    point_id, support_id));
            } else {
                for (const auto& support : {support_id.substr(0, separator),
                         support_id.substr(separator + 2)}) {
                    const auto* point = sketch.find_point(point_id);
                    if (point != nullptr && sketch.project_point_to_curve(
                            support, point->x, point->y)) {
                        static_cast<void>(sketch.add_point_on_circle_constraint(
                            point_id, support));
                    } else {
                        static_cast<void>(sketch.add_point_on_line_constraint(
                            point_id, support));
                    }
                }
            }
        }
    } catch (const std::invalid_argument&) {
        // Reusing a persisted point may already provide the same relation.
    }
    return point_id;
}

QString sketch_constraint_label(zima::sketcher::ConstraintKind kind) {
    using Kind = zima::sketcher::ConstraintKind;
    switch (kind) {
    case Kind::Horizontal: return QObject::tr("Vodorovnost");
    case Kind::Vertical: return QObject::tr("Svislost");
    case Kind::Coincident: return QObject::tr("Totožnost");
    case Kind::PointReference: return QObject::tr("Reference bodu");
    case Kind::Parallel: return QObject::tr("Rovnoběžnost");
    case Kind::Perpendicular: return QObject::tr("Kolmost");
    case Kind::EqualLength: return QObject::tr("Stejnost");
    case Kind::EqualRadius: return QObject::tr("Stejnost");
    case Kind::PointOnCircle: return QObject::tr("Bod na křivce");
    case Kind::PointOnLine: return QObject::tr("Bod na přímce");
    case Kind::Symmetric: return QObject::tr("Symetrie");
    case Kind::Midpoint: return QObject::tr("Střed");
    case Kind::Concentric: return QObject::tr("Soustřednost");
    case Kind::Tangent: return QObject::tr("Tečnost");
    }
    return QObject::tr("Vazba");
}

QIcon sketch_constraint_tree_icon(zima::sketcher::ConstraintKind kind) {
    QString symbol = QString::fromStdString(
        zima::sketcher::constraint_marker_label(kind));
    // Coincident points merge in the View; use the same C identity symbol
    // as point-on-curve constraints in the constraints list.
    if (kind == zima::sketcher::ConstraintKind::Coincident) symbol = QStringLiteral("C");
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QColor("#7CFF6B"));
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(12);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, symbol);
    return QIcon(pixmap);
}

QString sketch_dimension_label(const zima::sketcher::SketchDimension& dimension) {
    using Kind = zima::sketcher::DimensionKind;
    const auto value = [value = dimension.value](const QString& pattern) {
        return pattern.arg(value, 0, 'f', 3);
    };
    switch (dimension.kind) {
    case Kind::Distance: return value(QObject::tr("Vzdálenost %1 mm"));
    case Kind::DistanceX: return value(QObject::tr("Vodorovná kóta X %1 mm"));
    case Kind::DistanceY: return value(QObject::tr("Svislá kóta Y %1 mm"));
    case Kind::DistancePointLine:
        return value(QObject::tr("Vzdálenost bod–přímka %1 mm"));
    case Kind::DistanceSymmetric:
        return value(QObject::tr("Symetrická kóta ⌀%1 mm"));
    case Kind::DistanceLine:
        return value(QObject::tr("Vzdálenost rovnoběžek %1 mm"));
    case Kind::Radius: return value(QObject::tr("Poloměr R%1 mm"));
    case Kind::Diameter: return value(QObject::tr("Průměr ⌀%1 mm"));
    case Kind::Angle: return value(QObject::tr("Úhlová kóta %1°"));
    case Kind::AngleThreePoint:
        return value(QObject::tr("Tříbodový úhel %1°"));
    case Kind::AngleBetween:
        return value(QObject::tr("Úhel mezi přímkami %1°"));
    case Kind::AngleSymmetric:
        return value(QObject::tr("Symetrický úhel %1°"));
    case Kind::DistanceLineSymmetric:
        return value(QObject::tr("Symetrická vzdálenost přímek %1 mm"));
    case Kind::EllipseMajorRadius:
        return value(QObject::tr("Hlavní poloosa a=%1 mm"));
    case Kind::EllipseMinorRadius:
        return value(QObject::tr("Vedlejší poloosa b=%1 mm"));
    case Kind::EllipseRotation:
        return value(QObject::tr("Natočení elipsy %1°"));
    }
    return QObject::tr("Kóta");
}

std::vector<zima::kernel::ViewerEdge> sketch_text_preview_edges(
    const zima::sketcher::Sketch& sketch,
    const zima::sketcher::SketchText& text) {
    std::vector<zima::kernel::ViewerEdge> edges;
    edges.reserve(text.contours.size());
    for (const auto& contour : text.contours) {
        if (contour.size() < 3) continue;
        zima::kernel::ViewerEdge edge;
        edge.filled_text=!text.modeling_geometry;
        edge.reference={sketch.id,"text:"+text.id+":green",{}};
        edge.points.reserve(contour.size() + 1);
        for (const auto& point : contour) {
            edge.points.push_back(sketch.world_point(point[0], point[1]));
        }
        edge.points.push_back(edge.points.front());
        edges.push_back(std::move(edge));
    }
    return edges;
}


std::optional<SketchPosition> projected_ellipse_minor(
    const SketchPosition& center, const SketchPosition& major,
    const SketchPosition& cursor) {
    const double axis_x = major[0] - center[0];
    const double axis_y = major[1] - center[1];
    const double axis_length = std::hypot(axis_x, axis_y);
    if (axis_length <= 1.0e-9) return std::nullopt;
    const double normal_x = -axis_y / axis_length;
    const double normal_y = axis_x / axis_length;
    const double signed_length =
        (cursor[0] - center[0]) * normal_x +
        (cursor[1] - center[1]) * normal_y;
    if (std::abs(signed_length) <= 1.0e-9) return std::nullopt;
    return SketchPosition{
        center[0] + signed_length * normal_x,
        center[1] + signed_length * normal_y};
}

std::optional<ProjectedEllipsePosition> projected_ellipse_position(
    const SketchPosition& center, const SketchPosition& major,
    const SketchPosition& minor, const SketchPosition& cursor) {
    const double major_x = major[0] - center[0];
    const double major_y = major[1] - center[1];
    const double minor_x = minor[0] - center[0];
    const double minor_y = minor[1] - center[1];
    const double determinant = major_x * minor_y - major_y * minor_x;
    if (std::abs(determinant) <= 1.0e-12) return std::nullopt;
    const double cursor_x = cursor[0] - center[0];
    const double cursor_y = cursor[1] - center[1];
    double cosine =
        (cursor_x * minor_y - cursor_y * minor_x) / determinant;
    double sine =
        (major_x * cursor_y - major_y * cursor_x) / determinant;
    const double scale = std::hypot(cosine, sine);
    if (scale <= 1.0e-12) return std::nullopt;
    cosine /= scale;
    sine /= scale;
    return ProjectedEllipsePosition{{
        center[0] + major_x * cosine + minor_x * sine,
        center[1] + major_y * cosine + minor_y * sine},
        std::atan2(sine, cosine)};
}

zima::kernel::ViewerEdge ellipse_preview_edge(
    const zima::sketcher::Sketch& sketch, const SketchPosition& center,
    const SketchPosition& major, const SketchPosition& minor) {
    zima::kernel::ViewerEdge edge;
    constexpr std::size_t samples = 192;
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    edge.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double parameter = full_turn * static_cast<double>(sample) /
            static_cast<double>(samples);
        const double cosine = std::cos(parameter);
        const double sine = std::sin(parameter);
        edge.points.push_back(sketch.world_point(
            center[0] + (major[0] - center[0]) * cosine +
                (minor[0] - center[0]) * sine,
            center[1] + (major[1] - center[1]) * cosine +
                (minor[1] - center[1]) * sine));
    }
    return edge;
}

zima::document::ConstructionObject sweep_display_path(
    const zima::document::HistoryContainer& container) {
    auto path = container.sweep3d.path;
    path.parent_construction_id.clear();
    path.origin = {container.placement.x,
        container.placement.y, container.placement.z};
    path.entity_origin = path.origin;
    path.rotation = {container.placement.rotation_x,
        container.placement.rotation_y, container.placement.rotation_z};
    path.absolute_rotation = path.rotation;
    path.rotation_offset_x = 0.0;
    path.rotation_offset_y = 0.0;
    path.rotation_offset_z = 0.0;
    path.orientation_back = false;
    path.orientation_quarter_turns = 0;
    path.references.clear();
    path.definition = zima::document::ConstructionDefinition::Absolute;
    return path;
}

} // namespace zima::app::workspace_detail

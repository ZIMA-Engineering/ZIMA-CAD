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

std::set<std::string> sketch_external_reference_source_owners(
    const zima::document::PartDocument& document,
    const std::string& sketch_id) {
    std::size_t first_consumer = document.history.size();
    const auto sketch = std::ranges::find_if(document.sketches,
        [&](const auto& value) { return value.id == sketch_id; });
    for (std::size_t index = 0; index < document.history.size(); ++index) {
        const auto& container = document.history[index];
        const bool extrusion_consumer =
            container.feature_kind == zima::document::FeatureKind::Extrusion &&
            container.extrusion.sketch_id == sketch_id;
        const bool revolution_consumer =
            container.feature_kind == zima::document::FeatureKind::Revolution &&
            container.revolution.sketch_id == sketch_id;
        const bool sweep_consumer=container.feature_kind==zima::document::FeatureKind::Sweep2D&&
            std::ranges::any_of(container.sweep2d.sketches(),[&](const auto& data){return zima::sketcher::Sketch::from_serialized(data).id==sketch_id;});
        if (extrusion_consumer || revolution_consumer || sweep_consumer ||
            (sketch != document.sketches.end() && sketch->owner_container_id == container.id)) {
            first_consumer = std::min(first_consumer, index);
        }
    }
    std::set<std::string> owners;
    if (!document.body_history.bodies().empty()) {
        const auto consumer = first_consumer < document.history.size()
            ? document.history[first_consumer].id : sketch_id;
        const auto* target = document.body_history.owner(consumer);
        if (!target) return owners;
        const auto add_construction = [&](const auto& self, const auto& object) -> void {
            owners.insert(object.id); owners.insert(object.entity_id);
            owners.insert(object.container_origin.id);
            for (const auto& point : object.curve_points) self(self, point);
        };
        for (const auto& body : document.body_history.bodies()) {
            for (const auto& entry : body.entries) {
                if (entry.id == consumer) return owners;
                owners.insert(entry.id);
                if (const auto* object = document.find_construction(entry.id))
                    add_construction(add_construction, *object);
                if (const auto* feature = document.find_container(entry.id)) {
                    owners.insert(feature->container_origin.id);
                    if (feature->feature_kind == zima::document::FeatureKind::Sweep3D)
                        add_construction(add_construction, feature->sweep3d.path);
                }
                for (const auto& source_sketch : document.sketches)
                    if (source_sketch.owner_container_id == entry.id) owners.insert(source_sketch.id);
            }
            if (body.scope.id == target->scope.id) break;
        }
        return owners;
    }
    for (std::size_t index = 0; index < first_consumer; ++index) {
        owners.insert(document.history[index].id);
    }
    for (const auto& construction : document.constructions) {
        owners.insert(construction.id);
    }
    return owners;
}

zima::kernel::ViewerReferenceGeometry sketch_external_reference_source_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    zima::kernel::ViewerReferenceGeometry source;
    if (!calculated_boundaries.empty()) {
        source = calculated_boundaries.back().mesh.original_references;
    }
    append_reference_geometry(source,
        document.construction_viewer_mesh().original_references);
    return source;
}

zima::kernel::ViewerReferenceGeometry construction_reference_source_geometry(
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    zima::kernel::ViewerReferenceGeometry source;
    if (!calculated_boundaries.empty()) {
        source = calculated_boundaries.back().mesh.original_references;
    }
    return source;
}

zima::kernel::ViewerReferenceGeometry part_construction_dimension_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    auto geometry =
        construction_reference_source_geometry(calculated_boundaries);
    append_reference_geometry(
        geometry, document.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry, document.body_origin_reference_geometry());
    append_reference_geometry(
        geometry, document.construction_viewer_mesh().original_references);
    return geometry;
}

bool refresh_sketch_external_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    const auto source = sketch_external_reference_source_geometry(
        document, calculated_boundaries);
    bool changed = false;
    const auto refresh=[&](zima::sketcher::Sketch& sketch) {
        const auto allowed_owners = sketch_external_reference_source_owners(
            document, sketch.id);
        zima::kernel::ViewerReferenceGeometry allowed_source;
        for (const auto& edge : source.edges) {
            if (allowed_owners.contains(edge.reference.owner_id)) {
                allowed_source.edges.push_back(edge);
            }
        }
        for (const auto& point : source.points) {
            if (allowed_owners.contains(point.reference.owner_id)) {
                allowed_source.points.push_back(point);
            }
        }
        for (const auto& axis : source.axes) {
            if (allowed_owners.contains(axis.reference.owner_id)) {
                allowed_source.axes.push_back(axis);
            }
        }
        allowed_source.vertices = source.vertices;
        for (std::size_t triangle = 0;
             triangle < source.triangle_references.size(); ++triangle) {
            const auto& face = source.triangle_references[triangle];
            if (!allowed_owners.contains(face.owner_id)) continue;
            allowed_source.triangle_references.push_back(face);
            allowed_source.triangles.insert(allowed_source.triangles.end(), {
                source.triangles[triangle * 3],
                source.triangles[triangle * 3 + 1],
                source.triangles[triangle * 3 + 2]});
        }
        if (sketch.refresh_external_references(
                document.document_id, document.sketch_reference_geometry_for(sketch, std::move(allowed_source)))) {
            changed = true;
        }
    };
    for(auto& sketch:document.sketches)refresh(sketch);
    for(auto& container:document.history)if(container.feature_kind==zima::document::FeatureKind::Sweep2D)
        for(auto& data:container.sweep2d.sketches()){auto sketch=zima::sketcher::Sketch::from_serialized(data);refresh(sketch);data=sketch.serialized();}
    return changed;
}

bool prune_missing_drill_point_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& boundaries) {
    bool changed = false;
    const auto operations = document.kernel_operations();
    for (std::size_t index = 0; index < operations.size(); ++index) {
        auto* container = document.find_container(operations[index].owner_id);
        if (container == nullptr || container->feature_kind !=
                zima::document::FeatureKind::DrillPoint) continue;
        const auto* available = index == 0 || index - 1 >= boundaries.size()
            ? nullptr
            : &boundaries[index - 1].mesh.original_references
                .triangle_references;
        const auto before = container->drill_point.bottom_faces.size();
        std::erase_if(container->drill_point.bottom_faces,
            [&](const auto& face) {
                return available == nullptr || std::ranges::none_of(
                    *available, [&](const auto& candidate) {
                        return candidate.owner_id == face.owner_id &&
                            candidate.semantic_key == face.semantic_key;
                    });
            });
        changed = changed ||
            container->drill_point.bottom_faces.size() != before;
    }
    return changed;
}

bool refresh_assembly_sketch_external_references(
    zima::assembly::AssemblyDocument& document) {
    const auto source = document.build_scene().original_references;
    bool changed = false;
    for (auto& sketch : document.sketches) {
        std::set<std::string> source_documents;
        for (const auto& reference : sketch.external_references) {
            if (!reference.source_document_id.empty()) {
                source_documents.insert(reference.source_document_id);
            }
        }
        for (const auto& source_document_id : source_documents) {
            changed = sketch.refresh_external_references(
                source_document_id, source) || changed;
        }
    }
    return changed;
}

void populate_external_reference_cache(
    const zima::sketcher::Sketch& sketch,
    zima::sketcher::SketchExternalReference& reference,
    const zima::kernel::ViewerReferenceGeometry& source) {
    const auto matches = [&](const auto& candidate) {
        return candidate.owner_id == reference.source_owner_id &&
            candidate.semantic_key == reference.source_semantic_key &&
            candidate.instance_path == reference.source_instance_path;
    };
    if (reference.kind == zima::sketcher::ExternalReferenceKind::Edge) {
        const auto edge = std::find_if(source.edges.begin(), source.edges.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (edge == source.edges.end()) {
            throw std::runtime_error("Persisted source edge geometry is unavailable");
        }
        reference.exact_spline=sketch.project_external_spline(*edge);
        for (const auto& point : edge->points) {
            const auto local = sketch.local_point(point);
            if (reference.cached_points.empty() || std::hypot(
                    local[0] - reference.cached_points.back()[0],
                    local[1] - reference.cached_points.back()[1]) > 1.0e-9) {
                reference.cached_points.push_back(local);
            }
        }
    } else if (reference.kind == zima::sketcher::ExternalReferenceKind::Point) {
        const auto point = std::find_if(source.points.begin(), source.points.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (point == source.points.end()) {
            throw std::runtime_error("Persisted source point geometry is unavailable");
        }
        reference.cached_points.push_back(sketch.local_point(point->position));
    } else if (reference.kind == zima::sketcher::ExternalReferenceKind::Axis) {
        const auto axis = std::find_if(source.axes.begin(), source.axes.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (axis == source.axes.end()) {
            throw std::runtime_error("Persisted source axis geometry is unavailable");
        }
        const auto projected = sketch.project_external_axis(*axis);
        if (!projected) {
            throw std::runtime_error(
                "Source axis cannot be projected into the Sketch plane");
        }
        reference.cached_points = *projected;
        reference.infinite = true;
    } else {
        const auto projected = sketch.project_external_face_plane(source,
            {reference.source_owner_id, reference.source_semantic_key,
             reference.source_instance_path});
        if (projected) {
            reference.cached_points = *projected;
            reference.infinite = true;
        } else if (const auto intersections = sketch.external_face_reference_paths(source,
                       {reference.source_owner_id, reference.source_semantic_key,
                        reference.source_instance_path})) {
            reference.cached_paths = *intersections;
            reference.infinite = false;
        } else {
            throw std::runtime_error(
                "Source face has no unique intersection with the Sketch plane");
        }
    }
    reference.broken = false;
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

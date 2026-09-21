#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/opening_dimension_policy.hpp>
#include <zima/kernel/dimension_layout.hpp>
namespace zima::document {
// Read-only parameter annotations shared by modeling inspection and Drawings.
// Consume resolved placement data without solving references or bodies.
inline bool has_primitive_parameter_dimensions(FeatureKind kind) {
    return kind==FeatureKind::Box || kind==FeatureKind::Cylinder ||
        kind==FeatureKind::Sphere || kind==FeatureKind::Cone ||
        kind==FeatureKind::Pyramid || kind==FeatureKind::Wedge || kind==FeatureKind::Thread;
}
inline kernel::ViewerMesh primitive_parameter_dimensions(const HistoryContainer& feature) {
    kernel::ViewerMesh mesh;
    const auto* container=&feature;
    constexpr double visible_parameter_dimension_epsilon=1e-12;
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
                const auto origin = zima::kernel::Vec3{
                    container->placement.x, container->placement.y,
                    container->placement.z};
                // Feature dimensions are authored only in local coordinates;
                // this shared frame is their sole conversion into View space.
                const auto dimension_frame =
                    kernel::annotation_frame(origin, {container->placement.rotation_x,container->placement.rotation_y,container->placement.rotation_z});
                const auto local = [&](double x, double y, double z) {
                    return dimension_frame.world({x, y, z});
                };
                const auto local_vector = [&](zima::kernel::Vec3 value) {
                    return kernel::dimension_sub(dimension_frame.world(value), origin);
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

                }
    return mesh;
}
} // namespace zima::document

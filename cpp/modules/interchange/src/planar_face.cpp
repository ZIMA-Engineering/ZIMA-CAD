#include <zima/interchange/planar_face.hpp>

#include <algorithm>
#include <stdexcept>

namespace zima::interchange {

std::string append_planar_face_as_sketch_block(
    const PlanarFaceProfile& profile, zima::sketcher::Sketch& target) {
    if (profile.source_name.empty() || profile.source_face_key.empty() ||
        profile.curves.empty()) {
        throw std::invalid_argument("Zdrojová STEP plocha nemá platnou identitu nebo obrys");
    }
    auto next = target;
    std::vector<std::string> geometry_ids;
    for (const auto& curve : profile.curves) {
        geometry_ids.push_back(std::visit([&](const auto& exact) {
            using Curve = std::decay_t<decltype(exact)>;
            if constexpr (std::is_same_v<Curve, PlanarLine>) {
                return next.add_segment(
                    exact.first[0], exact.first[1], exact.second[0], exact.second[1]);
            } else if constexpr (std::is_same_v<Curve, PlanarCircle>) {
                return next.add_circle(exact.center[0], exact.center[1], exact.radius);
            } else if constexpr (std::is_same_v<Curve, PlanarArc>) {
                return next.add_arc(exact.center[0], exact.center[1],
                    exact.start[0], exact.start[1], exact.end[0], exact.end[1]);
            } else if constexpr (std::is_same_v<Curve, PlanarEllipse>) {
                return next.add_ellipse(exact.center[0], exact.center[1],
                    exact.major_point[0], exact.major_point[1],
                    exact.minor_point[0], exact.minor_point[1]);
            } else {
                return next.add_bspline(
                    exact.control_points, exact.degree, exact.closed);
            }
        }, curve));
    }
    std::vector<std::string> point_ids;
    const auto add_point = [&](const std::string& id) {
        if (std::ranges::find(point_ids, id) == point_ids.end()) point_ids.push_back(id);
    };
    for (const auto& id : geometry_ids) {
        for (const auto& value : next.segments) if (value.id == id) {
            add_point(value.first_point_id); add_point(value.second_point_id);
        }
        for (const auto& value : next.circles) if (value.id == id) add_point(value.center_point_id);
        for (const auto& value : next.arcs) if (value.id == id) {
            add_point(value.center_point_id); add_point(value.start_point_id);
            add_point(value.end_point_id);
        }
        for (const auto& value : next.ellipses) if (value.id == id) {
            add_point(value.center_point_id); add_point(value.major_point_id);
            add_point(value.minor_point_id);
        }
        for (const auto& value : next.bsplines) if (value.id == id) {
            for (const auto& point : value.control_point_ids) add_point(point);
        }
    }
    const auto block = next.add_import_block(
        profile.source_name + " – plocha",
        profile.source_face_key, std::move(geometry_ids), std::move(point_ids));
    target = std::move(next);
    return block;
}

}  // namespace zima::interchange

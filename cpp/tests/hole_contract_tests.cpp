#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <unordered_set>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        zima::kernel::OcctKernel kernel;
        auto document = zima::document::PartDocument::create_default();
        auto base = zima::document::PartDocument::create_box_container();
        base.box = {40.0, 40.0, 40.0};
        document.history.push_back(std::move(base));

        auto hole = zima::document::PartDocument::create_hole_container();
        hole.placement.z = -20.0;
        hole.hole.type = zima::document::HoleType::MetricThread;
        hole.hole.diameter = 8.5;
        hole.hole.bore_length = 35.0;
        hole.hole.entrance_chamfer = 1.0;
        hole.hole.drill_point_enabled = true;
        hole.hole.drill_point_angle_degrees = 118.0;
        hole.hole.thread_enabled = true;
        hole.hole.thread_nominal_diameter = 10.0;
        hole.hole.thread_pitch = 1.5;
        hole.hole.thread_length = 15.0;

        const auto bore_sketch = zima::sketcher::Sketch::from_serialized(
            hole.hole.sketch_serialized);
        const auto chamfer_sketch = zima::sketcher::Sketch::from_serialized(
            hole.hole.chamfer_sketch_serialized);
        const auto tip_sketch = zima::sketcher::Sketch::from_serialized(
            hole.hole.tip_sketch_serialized);
        require(bore_sketch.owner_container_id == hole.id &&
                    chamfer_sketch.owner_container_id == hole.id &&
                    tip_sketch.owner_container_id == hole.id,
                "Hole does not own all three internal Sketches");
        require(bore_sketch.plane == zima::sketcher::SketchPlane::XY &&
                    chamfer_sketch.plane == zima::sketcher::SketchPlane::XZ &&
                    tip_sketch.plane == zima::sketcher::SketchPlane::XZ,
                "Hole Sketches are not attached to their container planes");
        require(std::unordered_set<std::string>{bore_sketch.id,
                    chamfer_sketch.id, tip_sketch.id}.size() == 3,
                "Hole internal Sketch identities are not independent");
        for (const auto& point_id : hole.hole.chamfer_point_ids) {
            require(chamfer_sketch.find_point(point_id) != nullptr,
                "Chamfer Sketch point identity was not persisted");
        }
        for (const auto& point_id : hole.hole.tip_point_ids) {
            require(tip_sketch.find_point(point_id) != nullptr,
                "Tip Sketch point identity was not persisted");
        }
        document.history.push_back(hole);

        const auto operations = document.kernel_operations();
        require(operations.size() == 2 &&
                    operations.back().operation ==
                        zima::kernel::BooleanOperation::Subtract &&
                    std::holds_alternative<zima::kernel::FeatureGroupRequest>(
                        operations.back().primitive),
                "Hole is not one subtractive feature group");
        const auto& group = std::get<zima::kernel::FeatureGroupRequest>(
            operations.back().primitive);
        require(group.children.size() == 3 &&
                    std::holds_alternative<zima::kernel::ExtrusionRequest>(
                        group.children[0]) &&
                    std::holds_alternative<zima::kernel::RevolutionRequest>(
                        group.children[1]) &&
                    std::holds_alternative<zima::kernel::RevolutionRequest>(
                        group.children[2]),
                "Hole is not composed from Extrusion and separate Revolutions");
        const auto boundaries = kernel.evaluate_history(operations);
        require(boundaries.size() == 2 &&
                    boundaries.back().volume > 0.0 &&
                    boundaries.back().volume < boundaries.front().volume,
                "Hole profile did not remove material");
        const auto wire = document.hole_thread_edges(hole);
        require(wire.size() == 4,
                "Cosmetic thread is not two circles and two boundary lines");

        // A selected planar FRONT is the Hole's real circular Sketch plane.
        // In the container frame that plane is XZ and its normal/operation
        // direction is +Y; preview, body calculation and cosmetic thread must
        // all consume that same axis.
        auto referenced_hole = zima::document::PartDocument::create_hole_container();
        zima::document::ConstructionReference support_plane;
        support_plane.owner_id = "support-plane";
        support_plane.semantic_key = "datum:xy";
        support_plane.supports_offset = true;
        support_plane.orientation_role = "front";
        support_plane.orientation_drives_rotation = true;
        referenced_hole.placement.references.push_back(std::move(support_plane));
        referenced_hole.hole.thread_enabled = true;
        const auto referenced_preview =
            document.primitive_preview_edges(referenced_hole);
        require(!referenced_preview.empty(),
                "Referenced Hole preview is missing");
        bool preview_has_axial_y = false;
        for (const auto& edge : referenced_preview) {
            if (edge.points.size() == 2 &&
                std::abs(edge.points[1].y-edge.points[0].y -
                    referenced_hole.hole.bore_length) < 1.0e-9) {
                preview_has_axial_y = true;
            }
        }
        require(preview_has_axial_y,
                "Referenced Hole preview does not follow first-plane normal");
        const auto referenced_operations = [&] {
            auto carrier = zima::document::PartDocument::create_default();
            carrier.history.push_back(referenced_hole);
            return carrier.kernel_operations();
        }();
        const auto& referenced_group =
            std::get<zima::kernel::FeatureGroupRequest>(
                referenced_operations.front().primitive);
        const auto& referenced_bore =
            std::get<zima::kernel::ExtrusionRequest>(
                referenced_group.children.front());
        require(std::abs(referenced_bore.direction.x) < 1.0e-9 &&
                    std::abs(referenced_bore.direction.y-
                        referenced_hole.hole.bore_length) < 1.0e-9 &&
                    std::abs(referenced_bore.direction.z) < 1.0e-9,
                "Referenced Hole body does not follow first-plane normal");
        const auto referenced_wire = document.hole_thread_edges(referenced_hole);
        require(referenced_wire.size() == 4 &&
                    std::abs(referenced_wire.back().points.back().y-
                        referenced_hole.hole.thread_length) < 1.0e-9,
                "Referenced Hole thread does not follow bore axis");

        auto up_to_document = zima::document::PartDocument::create_default();
        auto up_to_base = zima::document::PartDocument::create_box_container();
        up_to_base.box = {40.0, 40.0, 40.0};
        up_to_document.history.push_back(up_to_base);
        auto up_to_hole = zima::document::PartDocument::create_hole_container();
        up_to_hole.placement.z = -20.0;
        up_to_hole.hole.bore_end_condition =
            zima::document::EndCondition::UpTo;
        zima::document::ExtrusionParameters::EndTarget plane_target;
        plane_target.kind = zima::document::EndTargetKind::Plane;
        plane_target.reference = {up_to_base.id, "datum:test-plane", {}};
        plane_target.label = "Test plane";
        plane_target.fallback_origin = {0.0, 0.0, 0.0};
        plane_target.fallback_normal = {0.0, 0.0, 1.0};
        up_to_hole.hole.bore_end_targets = {plane_target};
        up_to_document.history.push_back(up_to_hole);
        const auto up_to_boundaries = kernel.evaluate_history(
            up_to_document.kernel_operations());
        const double removed_up_to = up_to_boundaries.front().volume -
            up_to_boundaries.back().volume;
        require(std::abs(removed_up_to - std::numbers::pi*25.0*20.0) < 1.0,
                "Hole Up-to did not terminate on its persisted target plane");

        const auto path = std::filesystem::temp_directory_path() /
            "zima-cad-hole-only-contract.prtz";
        document.save(path, boundaries);
        const auto loaded = zima::document::PartDocument::load(path);
        std::filesystem::remove(path);
        require(loaded.history.size() == 2 &&
                    loaded.history.back().hole == hole.hole,
                "Hole did not preserve its parameters");
        std::cout << "C++ Hole contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

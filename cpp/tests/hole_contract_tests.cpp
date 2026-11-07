#include "profile_solid_fixture.hpp"
#include <zima/document/hole_profiles.hpp>
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
        auto base = zima::test::rectangular_feature(document,{40.0, 40.0, 40.0});

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

        // A later Hole may use the analytical axis of an existing bore as a
        // positional reference.  The axis fixes the two radial coordinates
        // while retaining the authored station along it.  This exercises the
        // real calculated Hole face instead of a synthetic cylinder packet.
        const auto bore_face = std::ranges::find_if(
            boundaries.back().mesh.original_references.triangle_references,
            [&](const auto& reference) {
                return reference.owner_id == hole.id && reference.surface &&
                    reference.surface->kind ==
                        zima::kernel::SurfaceGeometry::Kind::Cylinder;
            });
        require(bore_face != boundaries.back().mesh.original_references.
                    triangle_references.end(),
                "Calculated Hole did not publish its analytical bore face");
        auto coaxial_hole =
            zima::document::PartDocument::create_hole_container();
        coaxial_hole.hole.diameter = 6.0;
        coaxial_hole.hole.bore_length = 5.0;
        zima::document::update_hole_profiles(coaxial_hole.hole);
        coaxial_hole.placement.x = 12.0;
        coaxial_hole.placement.y = -9.0;
        coaxial_hole.placement.z = 15.0;
        zima::document::ConstructionReference bore_axis;
        bore_axis.owner_id = bore_face->owner_id;
        bore_axis.semantic_key = bore_face->semantic_key;
        bore_axis.instance_path = bore_face->instance_path;
        bore_axis.use_axis = true;
        bore_axis.supports_offset = false;
        coaxial_hole.placement.references = {bore_axis};
        require(zima::document::resolve_placement(coaxial_hole.placement,
                    boundaries.back().mesh.original_references),
                "Hole could not be placed on an existing bore axis");
        require(std::abs(coaxial_hole.placement.x-hole.placement.x) < 1.0e-8 &&
                    std::abs(coaxial_hole.placement.y-hole.placement.y) < 1.0e-8 &&
                    std::abs(coaxial_hole.placement.z-15.0) < 1.0e-8,
                "Bore-axis placement did not retain its axial station");
        auto coaxial_document = document;
        coaxial_document.history.push_back(coaxial_hole);
        const auto coaxial_boundaries = kernel.evaluate_history(
            coaxial_document.kernel_operations());
        require(coaxial_boundaries.size() == 3 &&
                    coaxial_boundaries.back().volume <
                        coaxial_boundaries[1].volume,
                "A Hole placed on a bore axis did not remove material");
        const auto coaxial_path = std::filesystem::temp_directory_path() /
            "zima-cad-hole-axis-contract.prtz";
        coaxial_document.save(coaxial_path, coaxial_boundaries);
        auto coaxial_loaded =
            zima::document::PartDocument::load(coaxial_path);
        std::filesystem::remove(coaxial_path);
        auto* loaded_coaxial =
            coaxial_loaded.find_container(coaxial_hole.id);
        require(loaded_coaxial &&
                    loaded_coaxial->placement.references.size() == 1 &&
                    loaded_coaxial->placement.references.front().use_axis &&
                    std::abs(loaded_coaxial->placement.z-15.0) < 1.0e-8,
                "Bore-axis Hole placement did not survive save/reopen");
        loaded_coaxial->hole.diameter = 5.0;
        zima::document::update_hole_profiles(loaded_coaxial->hole);
        const auto edited_coaxial_boundaries = kernel.evaluate_history(
            coaxial_loaded.kernel_operations());
        require(edited_coaxial_boundaries.size() == 3 &&
                    coaxial_loaded.find_container(coaxial_hole.id)->
                        placement.references.front().use_axis,
                "Editing a bore-axis Hole lost its placement reference");
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
        referenced_hole.hole.bore_end_condition =
            zima::document::EndCondition::ThroughAll;
        referenced_hole.hole.bore_end_condition = zima::document::EndCondition::ThroughAll;
        zima::kernel::ViewerMesh bounded_input;
        bounded_input.vertices = {{-20.0, -2.0, -20.0},
            {20.0, -2.0, -20.0}, {-20.0, 18.0, -20.0},
            {20.0, 18.0, -20.0}, {-20.0, -2.0, 20.0},
            {20.0, -2.0, 20.0}, {-20.0, 18.0, 20.0},
            {20.0, 18.0, 20.0}};
        const auto bounded_preview =
            document.primitive_preview_edges(referenced_hole, bounded_input);
        double bounded_min_y = std::numeric_limits<double>::max();
        double bounded_max_y = std::numeric_limits<double>::lowest();
        for (const auto& edge : bounded_preview) {
            for (const auto& point : edge.points) {
                bounded_min_y = std::min(bounded_min_y, point.y);
                bounded_max_y = std::max(bounded_max_y, point.y);
            }
        }
        require(bounded_min_y >= -3.01 && bounded_max_y <= 19.01 &&
                    bounded_max_y-bounded_min_y < 23.0,
                "Through-all Hole preview did not stop just beyond the input bounds");
        auto angled_hole = referenced_hole;
        angled_hole.placement.rotation_z = 45.0;
        bounded_input.triangles = {
            0,1,3, 0,3,2, 4,6,7, 4,7,5,
            0,4,5, 0,5,1, 2,3,7, 2,7,6,
            0,2,6, 0,6,4, 1,5,7, 1,7,3};
        const auto angled_preview =
            document.primitive_preview_edges(angled_hole, bounded_input);
        const double inverse_sqrt_two = 1.0/std::sqrt(2.0);
        double angled_min = std::numeric_limits<double>::max();
        double angled_max = std::numeric_limits<double>::lowest();
        for (const auto& edge : angled_preview) {
            for (const auto& point : edge.points) {
                const double axial = -point.x*inverse_sqrt_two +
                    point.y*inverse_sqrt_two;
                angled_min = std::min(angled_min, axial);
                angled_max = std::max(angled_max, axial);
            }
        }
        require(angled_max-angled_min < 33.0,
                "Tilted through-all Hole preview used whole-body projection instead of axis crossings");
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
        auto up_to_base = zima::test::rectangular_feature(up_to_document,{40.0, 40.0, 40.0});

        up_to_document.history.push_back(up_to_base);
        auto up_to_hole = zima::document::PartDocument::create_hole_container();
        up_to_hole.placement.z = -20.0;
        up_to_hole.hole.bore_end_condition =
            zima::document::EndCondition::UpTo;
        zima::document::ExtrusionParameters::EndTarget plane_target;
        plane_target.kind = zima::document::EndTargetKind::Plane;
        plane_target.reference = {up_to_document.document_id+":origin", "origin:plane:xy", {}};
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

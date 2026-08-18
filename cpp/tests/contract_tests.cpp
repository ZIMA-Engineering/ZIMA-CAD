#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <cmath>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    try {
        zima::kernel::OcctKernel kernel;
        const auto body = kernel.make_box({100.0, 80.0, 50.0});
        require(std::abs(body.volume - 400000.0) < 1e-6, "Incorrect box volume");
        require(std::abs(body.surface_area - 34000.0) < 1e-6,
                "Incorrect box surface area");
        require(!body.mesh.vertices.empty(), "Viewer mesh is empty");
        require(body.mesh.triangles.size() % 3 == 0, "Invalid triangle indices");
        require(body.mesh.original_references.triangle_references.size() ==
                    body.mesh.original_references.triangles.size() / 3,
                "Viewer triangles and face references are not aligned");
        require(std::all_of(body.mesh.triangle_references.begin(),
                    body.mesh.triangle_references.end(),
                    [](const auto& reference) { return !reference.valid(); }),
                "Calculated result body still owns selectable face references");
        require(std::all_of(body.mesh.edges.begin(), body.mesh.edges.end(),
                    [](const auto& edge) { return !edge.reference.valid(); }),
                "Calculated result body still owns selectable edge references");
        std::set<std::string> box_face_keys;
        for (const auto& reference : body.mesh.original_references.triangle_references) {
            require(reference.owner_id == "box" && reference.valid(),
                    "Primitive triangle lost its persisted owner");
            box_face_keys.insert(reference.semantic_key);
        }
        require(box_face_keys == std::set<std::string>{
                    "x_min", "x_max", "y_min", "y_max", "z_min", "z_max"},
                "Primitive semantic face keys are incomplete");
        std::set<std::string> box_edge_keys;
        for (const auto& edge : body.mesh.original_references.edges) {
            require(edge.reference.owner_id == "box" && edge.points.size() == 2,
                    "Primitive edge lost owner or geometry");
            box_edge_keys.insert(edge.reference.semantic_key);
        }
        require(box_edge_keys.size() == 12,
                "Primitive does not expose twelve unique semantic edges");
        std::set<std::string> box_vertex_keys;
        for (const auto& point : body.mesh.original_references.points) {
            require(point.reference.owner_id == "box",
                    "Primitive vertex lost its persisted owner");
            box_vertex_keys.insert(point.reference.semantic_key);
        }
        require(box_vertex_keys.size() == 8,
                "Primitive does not expose eight unique semantic vertices");
        std::set<std::string> box_axis_keys;
        for (const auto& axis : body.mesh.original_references.axes) {
            require(axis.reference.owner_id == "box" &&
                        std::abs(std::sqrt(
                            axis.direction.x * axis.direction.x +
                            axis.direction.y * axis.direction.y +
                            axis.direction.z * axis.direction.z) - 1.0) < 1.0e-9,
                    "Primitive axis lost owner or unit direction");
            box_axis_keys.insert(axis.reference.semantic_key);
        }
        require(box_axis_keys == std::set<std::string>{
                    "axis:x", "axis:y", "axis:z"},
                "Box does not expose three stable local axes");
        const auto selected_box_edge =
            body.mesh.original_references.edges.front().reference;
        const auto fillet_boundaries = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"fillet", zima::kernel::FilletRequest{
                selected_box_edge,
                zima::kernel::EdgeSelectionOrigin::OriginalEntity, 3.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(fillet_boundaries.size() == 2 &&
                    fillet_boundaries.back().volume < body.volume &&
                    fillet_boundaries.back().volume > body.volume - 1000.0,
                "Original-edge Fillet did not produce a valid bounded solid");
        const auto chamfer_boundaries = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"chamfer", zima::kernel::ChamferRequest{
                selected_box_edge,
                zima::kernel::EdgeSelectionOrigin::OriginalEntity, 3.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(chamfer_boundaries.size() == 2 &&
                    chamfer_boundaries.back().volume < body.volume &&
                    chamfer_boundaries.back().volume > body.volume - 1000.0,
                "Original-edge Chamfer did not produce a valid bounded solid");
        zima::kernel::BoxRequest resized_rotated{135.0, 62.0, 47.0};
        resized_rotated.rotation_degrees = {17.0, 29.0, 41.0};
        const auto regenerated = kernel.evaluate_boxes({
            {"persistent-box", resized_rotated,
             zima::kernel::BooleanOperation::Add},
        });
        std::set<std::string> regenerated_keys;
        for (const auto& reference : regenerated.mesh.original_references.triangle_references) {
            require(reference.owner_id == "persistent-box",
                    "Regeneration changed the persisted face owner");
            regenerated_keys.insert(reference.semantic_key);
        }
        require(regenerated_keys == box_face_keys,
                "Resize/rotation changed primitive semantic face identities");
        std::set<std::string> regenerated_edge_keys;
        for (const auto& edge : regenerated.mesh.original_references.edges) {
            regenerated_edge_keys.insert(edge.reference.semantic_key);
        }
        require(regenerated_edge_keys == box_edge_keys,
                "Resize/rotation changed primitive semantic edge identities");
        require(regenerated.mesh.original_references.axes.size() == 3 &&
                    std::abs(regenerated.mesh.original_references.axes.front().direction.y) > 1.0e-3,
                "Primitive placement was not applied to persisted axes");
        const auto cut_body = kernel.evaluate_boxes({
            {"base", {100.0, 80.0, 50.0}, zima::kernel::BooleanOperation::Add},
            {"cut", {20.0, 20.0, 20.0}, zima::kernel::BooleanOperation::Subtract},
        });
        require(std::abs(cut_body.volume - 392000.0) < 1e-6,
                "Sequential subtract produced incorrect volume");
        const auto boundaries = kernel.evaluate_box_boundaries({
            {"base", {100.0, 80.0, 50.0}, zima::kernel::BooleanOperation::Add},
            {"cut", {20.0, 20.0, 20.0}, zima::kernel::BooleanOperation::Subtract},
        });
        require(boundaries.size() == 2 &&
                    std::abs(boundaries.front().volume - 400000.0) < 1e-6 &&
                    std::abs(boundaries.back().volume - 392000.0) < 1e-6,
                "Explicit calculation did not retain history boundary results");
        std::set<std::string> cut_owners;
        for (const auto& reference : cut_body.mesh.original_references.triangle_references) {
            if (reference.valid()) cut_owners.insert(reference.owner_id);
        }
        require(cut_owners.contains("base") && cut_owners.contains("cut"),
                "Boolean history did not propagate both original face owners");
        std::set<std::string> cut_edge_owners;
        for (const auto& edge : cut_body.mesh.original_references.edges) {
            if (edge.reference.valid()) {
                cut_edge_owners.insert(edge.reference.owner_id);
            }
        }
        require(cut_edge_owners.contains("base") && cut_edge_owners.contains("cut"),
                "Boolean history did not propagate both original edge owners");
        bool rejected_first_subtract = false;
        try {
            static_cast<void>(kernel.evaluate_boxes({
                {"cut", {10.0, 10.0, 10.0},
                 zima::kernel::BooleanOperation::Subtract},
            }));
        } catch (const std::invalid_argument&) {
            rejected_first_subtract = true;
        }
        require(rejected_first_subtract, "Kernel accepted subtract as first operation");
        zima::kernel::BoxRequest first_placed{10.0, 10.0, 10.0};
        zima::kernel::BoxRequest second_placed{10.0, 10.0, 10.0};
        second_placed.translation = {20.0, 0.0, 0.0};
        second_placed.rotation_degrees = {15.0, 25.0, 35.0};
        const auto separated = kernel.evaluate_boxes({
            {"first", first_placed, zima::kernel::BooleanOperation::Add},
            {"second", second_placed, zima::kernel::BooleanOperation::Add},
        });
        require(std::abs(separated.volume - 2000.0) < 1e-6,
                "Placed and rotated boxes produced incorrect union volume");
        zima::kernel::CylinderRequest cylinder;
        cylinder.radius = 10.0;
        cylinder.height = 25.0;
        const auto cylinder_boundaries = kernel.evaluate_history({
            {"cylinder", cylinder, zima::kernel::BooleanOperation::Add},
        });
        require(cylinder_boundaries.size() == 1 &&
                    std::abs(cylinder_boundaries.front().volume -
                             std::numbers::pi * 2500.0) < 1e-5,
                "Cylinder produced incorrect OCCT volume");
        std::set<std::string> cylinder_faces;
        for (const auto& reference :
             cylinder_boundaries.front().mesh.original_references.triangle_references) {
            require(reference.owner_id == "cylinder",
                    "Cylinder face lost its stable owner");
            cylinder_faces.insert(reference.semantic_key);
        }
        require(cylinder_faces == std::set<std::string>{"side", "z_max", "z_min"},
                "Cylinder semantic faces are incomplete");
        std::set<std::string> cylinder_edges;
        bool sampled_circle = false;
        for (const auto& edge : cylinder_boundaries.front().mesh.original_references.edges) {
            cylinder_edges.insert(edge.reference.semantic_key);
            if (edge.reference.semantic_key.starts_with("circle:")) {
                sampled_circle = sampled_circle || edge.points.size() > 16;
            }
        }
        require(cylinder_edges == std::set<std::string>{
                    "circle:z_max", "circle:z_min", "seam"} && sampled_circle,
                "Cylinder edges are not stable selectable viewer polylines");
        require(cylinder_boundaries.front().mesh.original_references.axes.size() == 1 &&
                    cylinder_boundaries.front().mesh.original_references.axes.front()
                        .reference.semantic_key == "axis",
                "Cylinder does not expose its stable center axis");

        auto document = zima::document::PartDocument::create_default();
        require(document.history.empty(), "New Part must have empty history");
        const auto empty_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-empty-contract.zcp.json";
        document.save(empty_path);
        const auto empty_loaded = zima::document::PartDocument::load(empty_path);
        std::filesystem::remove(empty_path);
        require(empty_loaded.history.empty(), "Empty history was not preserved");
        auto first = zima::document::PartDocument::create_box_container();
        first.box.length = 123.5;
        document.history.push_back(first);
        auto second = zima::document::PartDocument::create_box_container();
        second.name = "Cut";
        second.combine_mode = zima::document::CombineMode::Subtract;
        second.box = {10.0, 11.0, 12.0};
        second.placement = {20.0, -5.0, 3.0, 10.0, 20.0, 30.0};
        document.history.push_back(second);
        auto part_sketch = zima::sketcher::Sketch::create_default();
        part_sketch.name = "Profil";
        auto sketch_first = zima::sketcher::Sketch::create_point(0.0, 0.0);
        auto sketch_second = zima::sketcher::Sketch::create_point(10.0, 0.0);
        part_sketch.segments.push_back(zima::sketcher::Sketch::create_segment(
            sketch_first.id, sketch_second.id));
        part_sketch.points.push_back(std::move(sketch_first));
        part_sketch.points.push_back(std::move(sketch_second));
        const auto part_sketch_id = part_sketch.id;
        document.sketches.push_back(std::move(part_sketch));
        const auto path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-contract.zcp.json";
        auto persisted_boundaries =
            kernel.evaluate_history(document.kernel_operations());
        persisted_boundaries.back().mesh.dimensions.push_back({
            {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0},
            {0.0, 5.0, 0.0}, {10.0, 5.0, 0.0}, 10.0,
            {first.id, "dimension:test"}});
        auto stale_document = document;
        stale_document.history.front().box.length += 1.0;
        bool stale_rejected = false;
        try {
            stale_document.save(path, persisted_boundaries);
        } catch (const std::runtime_error&) {
            stale_rejected = true;
        }
        require(stale_rejected,
                "Save accepted calculated geometry from different parameters");
        document.save(path, persisted_boundaries);
        std::vector<zima::kernel::BodyResult> loaded_boundaries;
        const auto loaded = zima::document::PartDocument::load(
            path, &loaded_boundaries);
        std::filesystem::remove(path);
        require(loaded.document_id == document.document_id,
                "Document identity was not preserved");
        require(loaded.history.size() == 2, "History containers were not preserved");
        require(loaded.sketches.size() == 1 &&
                    loaded.sketches.front().id == part_sketch_id &&
                    loaded.sketches.front().segments.size() == 1,
                "Part did not preserve its embedded Sketch graph");
        require(loaded.history.front().id == first.id,
                "Stable container identity was not preserved");
        require(loaded.find_container(first.id) != nullptr,
                "Stable container index lookup failed");
        require(loaded.history_index(first.id) == 0 &&
                    loaded.history_index(second.id) == 1,
                "History rollback boundary lookup failed");
        require(!loaded.history_index("missing-container"),
                "Missing container produced a rollback boundary");
        require(loaded.history.front().box.length == 123.5,
                "Box parameter was not preserved");
        require(loaded.history.back().combine_mode ==
                    zima::document::CombineMode::Subtract,
                "Subtract mode was not preserved");
        require(loaded.history.back().placement.y == -5.0 &&
                    loaded.history.back().placement.rotation_z == 30.0,
                "Container placement was not preserved");
        require(loaded_boundaries.size() == 2 &&
                    loaded_boundaries.back().mesh.original_references.triangle_references.size() ==
                        persisted_boundaries.back().mesh.original_references.triangle_references.size() &&
                    std::abs(loaded_boundaries.back().volume -
                             persisted_boundaries.back().volume) < 1e-6,
                "Calculated viewer packets were not preserved");
        require(loaded_boundaries.back().mesh.dimensions.size() == 1 &&
                    loaded_boundaries.back().mesh.dimensions.front().value == 10.0 &&
                    loaded_boundaries.back().mesh.dimensions.front().reference.owner_id ==
                        first.id,
                "Persisted viewer dimension lost geometry, value, or stable owner");
        require(loaded_boundaries.back().mesh.original_references.axes.size() ==
                    persisted_boundaries.back().mesh.original_references.axes.size() &&
                    loaded_boundaries.back().mesh.original_references.axes.front().reference ==
                        persisted_boundaries.back().mesh.original_references.axes.front().reference,
                "Persisted axes did not survive Part save/load");
        auto cylinder_document = zima::document::PartDocument::create_default();
        auto cylinder_container =
            zima::document::PartDocument::create_cylinder_container();
        cylinder_container.cylinder = {12.0, 34.0};
        cylinder_document.history.push_back(cylinder_container);
        const auto cylinder_results =
            kernel.evaluate_history(cylinder_document.kernel_operations());
        const auto cylinder_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-cylinder-contract.zcp.json";
        cylinder_document.save(cylinder_path, cylinder_results);
        std::vector<zima::kernel::BodyResult> loaded_cylinder_results;
        const auto loaded_cylinder = zima::document::PartDocument::load(
            cylinder_path, &loaded_cylinder_results);
        std::filesystem::remove(cylinder_path);
        require(loaded_cylinder.history.size() == 1 &&
                    loaded_cylinder.history.front().feature_kind ==
                        zima::document::FeatureKind::Cylinder &&
                    loaded_cylinder.history.front().cylinder.radius == 12.0 &&
                    loaded_cylinder_results.size() == 1,
                "Cylinder document did not survive save/load");
        auto extrusion_document = zima::document::PartDocument::create_default();
        auto extrusion_sketch = zima::sketcher::Sketch::create_default();
        extrusion_sketch.name = "Obdélníkový profil";
        static_cast<void>(extrusion_sketch.add_rectangle(0.0, 0.0, 30.0, 20.0));
        const auto extrusion_sketch_id = extrusion_sketch.id;
        extrusion_document.sketches.push_back(std::move(extrusion_sketch));
        auto extrusion_container =
            zima::document::PartDocument::create_extrusion_container(
                extrusion_sketch_id);
        extrusion_container.extrusion.height = 10.0;
        const auto extrusion_container_id = extrusion_container.id;
        extrusion_document.history.push_back(std::move(extrusion_container));
        const auto extrusion_results =
            kernel.evaluate_history(extrusion_document.kernel_operations());
        require(extrusion_results.size() == 1 &&
                    std::abs(extrusion_results.front().volume - 6000.0) < 1.0e-6,
                "Closed Sketch extrusion produced an incorrect solid volume");
        std::set<std::string> extrusion_faces;
        for (const auto& reference : extrusion_results.front().mesh.original_references.triangle_references) {
            require(reference.owner_id == extrusion_container_id && reference.valid(),
                    "Extrusion face lost its stable history owner");
            extrusion_faces.insert(reference.semantic_key);
        }
        require(extrusion_faces == std::set<std::string>{
                    "profile_start", "profile_end", "side:0", "side:1",
                    "side:2", "side:3"},
                "Extrusion does not expose stable start/end/side faces");
        require(extrusion_results.front().mesh.original_references.axes.size() == 1 &&
                    extrusion_results.front().mesh.original_references.axes.front().reference.semantic_key ==
                        "axis" &&
                    extrusion_results.front().mesh.original_references.axes.front().direction.z > 0.999,
                "Extrusion did not persist its normal axis");
        const auto z_bounds = [](const zima::kernel::BodyResult& result) {
            double minimum = std::numeric_limits<double>::infinity();
            double maximum = -std::numeric_limits<double>::infinity();
            for (const auto& point : result.mesh.vertices) {
                minimum = std::min(minimum, point.z);
                maximum = std::max(maximum, point.z);
            }
            return std::array<double, 2>{minimum, maximum};
        };
        auto reverse_extrusion_document = extrusion_document;
        reverse_extrusion_document.history.front().extrusion.direction =
            zima::document::ExtrusionDirection::Reverse;
        const auto reverse_extrusion_results = kernel.evaluate_history(
            reverse_extrusion_document.kernel_operations());
        const auto reverse_bounds = z_bounds(reverse_extrusion_results.front());
        require(std::abs(reverse_bounds[0] + 10.0) < 1.0e-7 &&
                    std::abs(reverse_bounds[1]) < 1.0e-7,
                "Reverse Extrusion is not located behind the Sketch plane");
        auto symmetric_extrusion_document = extrusion_document;
        symmetric_extrusion_document.history.front().extrusion.direction =
            zima::document::ExtrusionDirection::Symmetric;
        const auto symmetric_extrusion_results = kernel.evaluate_history(
            symmetric_extrusion_document.kernel_operations());
        const auto symmetric_bounds = z_bounds(symmetric_extrusion_results.front());
        require(std::abs(symmetric_bounds[0] + 5.0) < 1.0e-7 &&
                    std::abs(symmetric_bounds[1] - 5.0) < 1.0e-7 &&
                    symmetric_extrusion_results.front().source_fingerprint !=
                        extrusion_results.front().source_fingerprint,
                "Symmetric Extrusion is not centered on the Sketch plane");
        const auto extrusion_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-extrusion-contract.zcp.json";
        symmetric_extrusion_document.save(
            extrusion_path, symmetric_extrusion_results);
        std::vector<zima::kernel::BodyResult> loaded_extrusion_results;
        const auto loaded_extrusion = zima::document::PartDocument::load(
            extrusion_path, &loaded_extrusion_results);
        std::filesystem::remove(extrusion_path);
        require(loaded_extrusion.history.size() == 1 &&
                    loaded_extrusion.history.front().feature_kind ==
                        zima::document::FeatureKind::Extrusion &&
                    loaded_extrusion.history.front().extrusion.sketch_id ==
                        extrusion_sketch_id &&
                    loaded_extrusion.history.front().extrusion.direction ==
                        zima::document::ExtrusionDirection::Symmetric &&
                    loaded_extrusion_results.size() == 1,
                "Extrusion document did not survive save/load");
        auto misplaced_extrusion = extrusion_document;
        misplaced_extrusion.history.front().placement.x = 5.0;
        bool misplaced_extrusion_rejected = false;
        try {
            static_cast<void>(misplaced_extrusion.kernel_operations());
        } catch (const std::runtime_error&) {
            misplaced_extrusion_rejected = true;
        }
        require(misplaced_extrusion_rejected,
                "Extrusion silently accepted a second placement");
        auto open_profile_document = zima::document::PartDocument::create_default();
        auto open_profile = zima::sketcher::Sketch::create_default();
        static_cast<void>(open_profile.add_segment(0.0, 0.0, 10.0, 0.0));
        const auto open_profile_id = open_profile.id;
        open_profile_document.sketches.push_back(std::move(open_profile));
        open_profile_document.history.push_back(
            zima::document::PartDocument::create_extrusion_container(open_profile_id));
        bool open_profile_rejected = false;
        try {
            static_cast<void>(open_profile_document.kernel_operations());
        } catch (const std::runtime_error&) {
            open_profile_rejected = true;
        }
        require(open_profile_rejected,
                "Open Sketch profile reached the solid kernel");

        auto circular_document = zima::document::PartDocument::create_default();
        auto circular_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(circular_sketch.add_circle(20.0, 20.0, 5.0));
        const auto circular_sketch_id = circular_sketch.id;
        circular_document.sketches.push_back(std::move(circular_sketch));
        auto circular_base = zima::document::PartDocument::create_box_container();
        circular_base.box = {40.0, 40.0, 10.0};
        circular_document.history.push_back(std::move(circular_base));
        auto circular_cut =
            zima::document::PartDocument::create_extrusion_container(
                circular_sketch_id);
        circular_cut.extrusion.height = 10.0;
        circular_cut.combine_mode = zima::document::CombineMode::Subtract;
        const auto circular_cut_id = circular_cut.id;
        circular_document.history.push_back(std::move(circular_cut));
        const auto circular_results =
            kernel.evaluate_history(circular_document.kernel_operations());
        require(circular_results.size() == 2 &&
                    std::abs(circular_results.back().volume -
                        (16000.0 - 250.0 * std::numbers::pi)) < 1.0e-6,
                "Exact circular Sketch extrusion did not cut the expected volume");
        bool circular_side_found = false;
        for (const auto& reference :
             circular_results.back().mesh.original_references.triangle_references) {
            if (reference.owner_id == circular_cut_id &&
                reference.semantic_key == "side:0") {
                circular_side_found = true;
            }
        }
        require(circular_side_found,
                "Circular extrusion lost its stable cylindrical side reference");
        const auto circular_fingerprint = zima::kernel::history_fingerprint(
            circular_document.kernel_operations(), 2);
        auto resized_circle_document = circular_document;
        resized_circle_document.sketches.front().circles.front().radius = 6.0;
        require(circular_fingerprint != zima::kernel::history_fingerprint(
                    resized_circle_document.kernel_operations(), 2),
                "Circular profile radius is missing from the history fingerprint");
        auto xz_circle_document = zima::document::PartDocument::create_default();
        auto xz_circle_sketch = zima::sketcher::Sketch::create_default();
        xz_circle_sketch.plane = zima::sketcher::SketchPlane::XZ;
        xz_circle_sketch.plane_offset = 4.0;
        static_cast<void>(xz_circle_sketch.add_circle(2.0, 3.0, 3.0));
        const auto xz_circle_sketch_id = xz_circle_sketch.id;
        xz_circle_document.sketches.push_back(std::move(xz_circle_sketch));
        auto xz_circle_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                xz_circle_sketch_id);
        xz_circle_extrusion.extrusion.height = 7.0;
        xz_circle_document.history.push_back(std::move(xz_circle_extrusion));
        const auto xz_circle_results =
            kernel.evaluate_history(xz_circle_document.kernel_operations());
        require(std::abs(xz_circle_results.front().volume -
                    63.0 * std::numbers::pi) < 1.0e-6 &&
                    xz_circle_results.front().mesh.original_references.axes.size() == 1 &&
                    xz_circle_results.front().mesh.original_references.axes.front().direction.y < -0.999,
                "Circular XZ extrusion lost its exact volume or plane normal");
        auto multiple_circle_document = circular_document;
        static_cast<void>(multiple_circle_document.sketches.front().add_circle(
            10.0, 10.0, 2.0));
        bool disjoint_circles_rejected = false;
        try {
            static_cast<void>(multiple_circle_document.kernel_operations());
        } catch (const std::runtime_error&) {
            disjoint_circles_rejected = true;
        }
        require(disjoint_circles_rejected,
                "Disjoint circular profile loops reached OCCT");

        auto holed_profile_document = zima::document::PartDocument::create_default();
        auto holed_profile_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(holed_profile_sketch.add_rectangle(
            0.0, 0.0, 40.0, 30.0));
        static_cast<void>(holed_profile_sketch.add_circle(20.0, 15.0, 5.0));
        const auto holed_profile_sketch_id = holed_profile_sketch.id;
        holed_profile_document.sketches.push_back(std::move(holed_profile_sketch));
        auto holed_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                holed_profile_sketch_id);
        holed_extrusion.extrusion.height = 8.0;
        const auto holed_extrusion_id = holed_extrusion.id;
        holed_profile_document.history.push_back(std::move(holed_extrusion));
        const auto holed_profile_results = kernel.evaluate_history(
            holed_profile_document.kernel_operations());
        require(std::abs(holed_profile_results.front().volume -
                    (1200.0 - 25.0 * std::numbers::pi) * 8.0) < 1.0e-6,
                "Polygon profile with a circular hole has an incorrect volume");
        bool hole_side_found = false;
        for (const auto& reference :
             holed_profile_results.front().mesh.original_references.triangle_references) {
            if (reference.owner_id == holed_extrusion_id &&
                reference.semantic_key == "side:4") {
                hole_side_found = true;
            }
        }
        require(hole_side_found,
                "Inner profile wall lost its stable Extrusion owner");

        auto annulus_document = zima::document::PartDocument::create_default();
        auto annulus_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(annulus_sketch.add_circle(0.0, 0.0, 10.0));
        static_cast<void>(annulus_sketch.add_circle(0.0, 0.0, 4.0));
        const auto annulus_sketch_id = annulus_sketch.id;
        annulus_document.sketches.push_back(std::move(annulus_sketch));
        auto annulus_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                annulus_sketch_id);
        annulus_extrusion.extrusion.height = 5.0;
        annulus_document.history.push_back(std::move(annulus_extrusion));
        const auto annulus_results =
            kernel.evaluate_history(annulus_document.kernel_operations());
        require(std::abs(annulus_results.front().volume -
                    420.0 * std::numbers::pi) < 1.0e-6,
                "Nested circular profile did not produce an exact annulus");

        auto crossing_hole_document = holed_profile_document;
        crossing_hole_document.sketches.front().circles.front().radius = 16.0;
        bool crossing_hole_rejected = false;
        try {
            static_cast<void>(crossing_hole_document.kernel_operations());
        } catch (const std::runtime_error&) {
            crossing_hole_rejected = true;
        }
        require(crossing_hole_rejected,
                "A hole crossing the outer profile reached OCCT");

        auto self_intersecting_document =
            zima::document::PartDocument::create_default();
        auto self_intersecting_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(self_intersecting_sketch.add_segment(
            0.0, 0.0, 20.0, 20.0));
        static_cast<void>(self_intersecting_sketch.add_segment(
            20.0, 20.0, 0.0, 20.0));
        static_cast<void>(self_intersecting_sketch.add_segment(
            0.0, 20.0, 20.0, 0.0));
        static_cast<void>(self_intersecting_sketch.add_segment(
            20.0, 0.0, 0.0, 0.0));
        const auto self_intersecting_sketch_id = self_intersecting_sketch.id;
        self_intersecting_document.sketches.push_back(
            std::move(self_intersecting_sketch));
        self_intersecting_document.history.push_back(
            zima::document::PartDocument::create_extrusion_container(
                self_intersecting_sketch_id));
        bool self_intersection_rejected = false;
        try {
            static_cast<void>(self_intersecting_document.kernel_operations());
        } catch (const std::runtime_error&) {
            self_intersection_rejected = true;
        }
        require(self_intersection_rejected,
                "A self-intersecting profile reached OCCT");

        auto arc_profile_document = zima::document::PartDocument::create_default();
        auto arc_profile_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(arc_profile_sketch.add_arc(
            0.0, 0.0, 0.0, -10.0, 0.0, 10.0));
        static_cast<void>(arc_profile_sketch.add_segment(
            0.0, 10.0, 0.0, -10.0));
        static_cast<void>(arc_profile_sketch.add_circle(
            4.0, 0.0, 2.0));
        const auto arc_profile_sketch_id = arc_profile_sketch.id;
        arc_profile_document.sketches.push_back(std::move(arc_profile_sketch));
        auto arc_profile_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                arc_profile_sketch_id);
        arc_profile_extrusion.extrusion.height = 6.0;
        arc_profile_extrusion.extrusion.direction =
            zima::document::ExtrusionDirection::Symmetric;
        const auto arc_profile_owner = arc_profile_extrusion.id;
        arc_profile_document.history.push_back(std::move(arc_profile_extrusion));
        const auto arc_profile_results =
            kernel.evaluate_history(arc_profile_document.kernel_operations());
        const auto arc_profile_bounds = z_bounds(arc_profile_results.front());
        require(std::abs(arc_profile_results.front().volume -
                    276.0 * std::numbers::pi) < 1.0e-6 &&
                    std::abs(arc_profile_bounds[0] + 3.0) < 1.0e-7 &&
                    std::abs(arc_profile_bounds[1] - 3.0) < 1.0e-7,
                "Exact Arc profile with a circular hole has an incorrect volume "
                "or symmetric extent");
        std::set<std::string> arc_profile_sides;
        for (const auto& reference :
             arc_profile_results.front().mesh.original_references.triangle_references) {
            if (reference.owner_id == arc_profile_owner &&
                reference.semantic_key.starts_with("side:")) {
                arc_profile_sides.insert(reference.semantic_key);
            }
        }
        require(arc_profile_sides ==
                    std::set<std::string>{"side:0", "side:1", "side:2"},
                "Arc, Segment, and circular hole did not retain stable side ownership");
        auto two_arc_document = zima::document::PartDocument::create_default();
        auto two_arc_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(two_arc_sketch.add_arc(
            0.0, 0.0, 0.0, -4.0, 0.0, 4.0));
        static_cast<void>(two_arc_sketch.add_arc(
            0.0, 0.0, 0.0, 4.0, 0.0, -4.0));
        const auto two_arc_sketch_id = two_arc_sketch.id;
        two_arc_document.sketches.push_back(std::move(two_arc_sketch));
        auto two_arc_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                two_arc_sketch_id);
        two_arc_extrusion.extrusion.height = 9.0;
        two_arc_document.history.push_back(std::move(two_arc_extrusion));
        const auto two_arc_results =
            kernel.evaluate_history(two_arc_document.kernel_operations());
        require(std::abs(two_arc_results.front().volume -
                    144.0 * std::numbers::pi) < 1.0e-6,
                "Two exact Arcs did not form a closed circular profile");

        auto ellipse_profile_document =
            zima::document::PartDocument::create_default();
        auto ellipse_profile_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(ellipse_profile_sketch.add_ellipse(
            0.0, 0.0, 10.0, 0.0, 0.0, 4.0));
        const auto ellipse_profile_sketch_id = ellipse_profile_sketch.id;
        ellipse_profile_document.sketches.push_back(
            std::move(ellipse_profile_sketch));
        ellipse_profile_document.history.push_back(
            zima::document::PartDocument::create_extrusion_container(
                ellipse_profile_sketch_id));
        const auto ellipse_profile_results = kernel.evaluate_history(
            ellipse_profile_document.kernel_operations());
        require(std::abs(ellipse_profile_results.front().volume -
                    400.0 * std::numbers::pi) < 1.0e-6,
                "Exact Ellipse extrusion has an incorrect volume");
        auto ellipse_revolution_document =
            zima::document::PartDocument::create_default();
        auto ellipse_revolution_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(ellipse_revolution_sketch.add_ellipse(
            0.0, 20.0, 10.0, 20.0, 0.0, 24.0));
        const auto ellipse_revolution_sketch_id = ellipse_revolution_sketch.id;
        ellipse_revolution_document.sketches.push_back(
            std::move(ellipse_revolution_sketch));
        ellipse_revolution_document.history.push_back(
            zima::document::PartDocument::create_revolution_container(
                ellipse_revolution_sketch_id));
        const auto ellipse_revolution_results = kernel.evaluate_history(
            ellipse_revolution_document.kernel_operations());
        require(std::abs(ellipse_revolution_results.front().volume -
                    1600.0 * std::numbers::pi * std::numbers::pi) < 1.0e-6,
                "Exact Ellipse Revolution has an incorrect toroidal volume");
        auto resized_ellipse = ellipse_profile_document;
        resized_ellipse.sketches.front().ellipses.front().minor_radius = 3.0;
        auto* resized_minor = resized_ellipse.sketches.front().find_point(
            resized_ellipse.sketches.front().ellipses.front().minor_point_id);
        resized_minor->y = 3.0;
        require(zima::kernel::history_fingerprint(
                    resized_ellipse.kernel_operations(), 1) !=
                    ellipse_profile_results.front().source_fingerprint,
                "Ellipse semiaxes are missing from the history fingerprint");
        auto open_spline_document = zima::document::PartDocument::create_default();
        auto open_spline_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(open_spline_sketch.add_bspline({
            {0.0, 0.0}, {10.0, 20.0}, {20.0, -10.0}, {30.0, 0.0}}));
        const auto open_spline_sketch_id = open_spline_sketch.id;
        open_spline_document.sketches.push_back(std::move(open_spline_sketch));
        open_spline_document.history.push_back(
            zima::document::PartDocument::create_extrusion_container(
                open_spline_sketch_id));
        bool open_spline_rejected = false;
        try {
            static_cast<void>(open_spline_document.kernel_operations());
        } catch (const std::runtime_error&) {
            open_spline_rejected = true;
        }
        require(open_spline_rejected,
                "Open B-spline reached solid calculation without a closed exact profile");
        auto closed_spline_document = zima::document::PartDocument::create_default();
        auto closed_spline_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(closed_spline_sketch.add_bspline({
            {-20.0, 0.0}, {-15.0, 15.0}, {0.0, 22.0}, {15.0, 15.0},
            {20.0, 0.0}, {10.0, -18.0}, {-10.0, -18.0}}, 3, true));
        const auto closed_spline_sketch_id = closed_spline_sketch.id;
        closed_spline_document.sketches.push_back(std::move(closed_spline_sketch));
        closed_spline_document.history.push_back(
            zima::document::PartDocument::create_extrusion_container(
                closed_spline_sketch_id));
        const auto closed_spline_results = kernel.evaluate_history(
            closed_spline_document.kernel_operations());
        require(closed_spline_results.size() == 1 &&
                    closed_spline_results.front().volume > 1.0,
                "Exact closed B-spline Extrusion did not produce a solid");
        auto changed_closed_spline = closed_spline_document;
        auto* changed_pole = changed_closed_spline.sketches.front().find_point(
            changed_closed_spline.sketches.front().bsplines.front().control_point_ids[1]);
        changed_pole->y += 1.0;
        require(zima::kernel::history_fingerprint(
                    changed_closed_spline.kernel_operations(), 1) !=
                    closed_spline_results.front().source_fingerprint,
                "B-spline poles are missing from the history fingerprint");

        auto mixed_spline_document = zima::document::PartDocument::create_default();
        auto mixed_spline_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(mixed_spline_sketch.add_bspline({
            {-20.0, 0.0}, {-8.0, 18.0}, {8.0, 18.0}, {20.0, 0.0}}));
        static_cast<void>(mixed_spline_sketch.add_segment(
            20.0, 0.0, 0.0, -20.0));
        static_cast<void>(mixed_spline_sketch.add_segment(
            0.0, -20.0, -20.0, 0.0));
        const auto mixed_spline_sketch_id = mixed_spline_sketch.id;
        mixed_spline_document.sketches.push_back(std::move(mixed_spline_sketch));
        mixed_spline_document.history.push_back(
            zima::document::PartDocument::create_extrusion_container(
                mixed_spline_sketch_id));
        const auto mixed_spline_results = kernel.evaluate_history(
            mixed_spline_document.kernel_operations());
        require(mixed_spline_results.size() == 1 &&
                    mixed_spline_results.front().volume > 1.0,
                "Mixed line/B-spline Extrusion did not produce a solid");

        auto revolution_document = zima::document::PartDocument::create_default();
        auto revolution_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(revolution_sketch.add_rectangle(
            10.0, 5.0, 20.0, 8.0));
        const auto revolution_sketch_id = revolution_sketch.id;
        revolution_document.sketches.push_back(std::move(revolution_sketch));
        auto revolution_container =
            zima::document::PartDocument::create_revolution_container(
                revolution_sketch_id);
        const auto revolution_owner = revolution_container.id;
        revolution_document.history.push_back(std::move(revolution_container));
        const auto revolution_results =
            kernel.evaluate_history(revolution_document.kernel_operations());
        require(revolution_results.size() == 1 &&
                    std::abs(revolution_results.front().volume -
                        390.0 * std::numbers::pi) < 1.0e-6 &&
                    revolution_results.front().mesh.original_references.axes.size() == 1 &&
                    revolution_results.front().mesh.original_references.axes.front().direction.x > 0.999,
                "Full Sketch Revolution has an incorrect volume or axis");
        auto half_revolution_document = revolution_document;
        half_revolution_document.history.front().revolution.angle_degrees = 180.0;
        const auto half_revolution_results = kernel.evaluate_history(
            half_revolution_document.kernel_operations());
        require(std::abs(half_revolution_results.front().volume -
                    195.0 * std::numbers::pi) < 1.0e-6 &&
                    half_revolution_results.front().source_fingerprint !=
                        revolution_results.front().source_fingerprint,
                "Partial Revolution has an incorrect volume or fingerprint");
        std::set<std::string> partial_revolution_faces;
        for (const auto& reference :
             half_revolution_results.front().mesh.original_references.triangle_references) {
            if (reference.owner_id == revolution_owner) {
                partial_revolution_faces.insert(reference.semantic_key);
            }
        }
        require(partial_revolution_faces.contains("profile_start") &&
                    partial_revolution_faces.contains("profile_end"),
                "Partial Revolution lost its start or end profile face");
        const auto revolution_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-revolution-contract.zcp.json";
        half_revolution_document.save(revolution_path, half_revolution_results);
        std::vector<zima::kernel::BodyResult> loaded_revolution_results;
        const auto loaded_revolution = zima::document::PartDocument::load(
            revolution_path, &loaded_revolution_results);
        std::filesystem::remove(revolution_path);
        require(loaded_revolution.history.size() == 1 &&
                    loaded_revolution.history.front().feature_kind ==
                        zima::document::FeatureKind::Revolution &&
                    loaded_revolution.history.front().revolution.axis ==
                        zima::document::RevolutionAxis::SketchX &&
                    loaded_revolution.history.front().revolution.angle_degrees == 180.0 &&
                    loaded_revolution_results.size() == 1,
                "Revolution did not survive save/load");
        auto torus_revolution_document =
            zima::document::PartDocument::create_default();
        auto torus_revolution_sketch =
            zima::sketcher::Sketch::create_default();
        static_cast<void>(torus_revolution_sketch.add_circle(
            10.0, 0.0, 2.0));
        const auto torus_revolution_sketch_id = torus_revolution_sketch.id;
        torus_revolution_document.sketches.push_back(
            std::move(torus_revolution_sketch));
        auto torus_revolution =
            zima::document::PartDocument::create_revolution_container(
                torus_revolution_sketch_id);
        torus_revolution.revolution.axis =
            zima::document::RevolutionAxis::SketchY;
        torus_revolution_document.history.push_back(std::move(torus_revolution));
        const auto torus_revolution_results = kernel.evaluate_history(
            torus_revolution_document.kernel_operations());
        require(std::abs(torus_revolution_results.front().volume -
                    80.0 * std::numbers::pi * std::numbers::pi) < 1.0e-6,
                "Exact circular Revolution did not produce the expected torus");
        auto arc_torus_document = zima::document::PartDocument::create_default();
        auto arc_torus_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(arc_torus_sketch.add_arc(
            10.0, 0.0, 10.0, -2.0, 10.0, 2.0));
        static_cast<void>(arc_torus_sketch.add_arc(
            10.0, 0.0, 10.0, 2.0, 10.0, -2.0));
        const auto arc_torus_sketch_id = arc_torus_sketch.id;
        arc_torus_document.sketches.push_back(std::move(arc_torus_sketch));
        auto arc_torus_revolution =
            zima::document::PartDocument::create_revolution_container(
                arc_torus_sketch_id);
        arc_torus_revolution.revolution.axis =
            zima::document::RevolutionAxis::SketchY;
        arc_torus_document.history.push_back(std::move(arc_torus_revolution));
        const auto arc_torus_results = kernel.evaluate_history(
            arc_torus_document.kernel_operations());
        require(std::abs(arc_torus_results.front().volume -
                    80.0 * std::numbers::pi * std::numbers::pi) < 1.0e-6 &&
                    arc_torus_results.front().source_fingerprint !=
                        torus_revolution_results.front().source_fingerprint,
                "Exact Arc Revolution did not preserve the torus geometry");

        auto curved_holed_revolution_document =
            zima::document::PartDocument::create_default();
        auto curved_holed_revolution_sketch =
            zima::sketcher::Sketch::create_default();
        static_cast<void>(curved_holed_revolution_sketch.add_arc(
            0.0, 0.0, 0.0, -10.0, 0.0, 10.0));
        static_cast<void>(curved_holed_revolution_sketch.add_segment(
            0.0, 10.0, 0.0, -10.0));
        static_cast<void>(curved_holed_revolution_sketch.add_circle(
            4.0, 0.0, 2.0));
        const auto curved_holed_revolution_sketch_id =
            curved_holed_revolution_sketch.id;
        curved_holed_revolution_document.sketches.push_back(
            std::move(curved_holed_revolution_sketch));
        auto curved_holed_revolution =
            zima::document::PartDocument::create_revolution_container(
                curved_holed_revolution_sketch_id);
        curved_holed_revolution.revolution.axis =
            zima::document::RevolutionAxis::SketchY;
        curved_holed_revolution_document.history.push_back(
            std::move(curved_holed_revolution));
        const auto curved_holed_revolution_results = kernel.evaluate_history(
            curved_holed_revolution_document.kernel_operations());
        require(std::abs(curved_holed_revolution_results.front().volume -
                    (4000.0 * std::numbers::pi / 3.0 -
                     32.0 * std::numbers::pi * std::numbers::pi)) < 1.0e-6,
                "Curved Revolution with a circular hole has an incorrect volume");

        auto holed_revolution_document =
            zima::document::PartDocument::create_default();
        auto holed_revolution_sketch =
            zima::sketcher::Sketch::create_default();
        static_cast<void>(holed_revolution_sketch.add_rectangle(
            10.0, 5.0, 20.0, 8.0));
        static_cast<void>(holed_revolution_sketch.add_circle(
            15.0, 6.5, 1.0));
        const auto holed_revolution_sketch_id = holed_revolution_sketch.id;
        holed_revolution_document.sketches.push_back(
            std::move(holed_revolution_sketch));
        holed_revolution_document.history.push_back(
            zima::document::PartDocument::create_revolution_container(
                holed_revolution_sketch_id));
        const auto holed_revolution_results = kernel.evaluate_history(
            holed_revolution_document.kernel_operations());
        require(std::abs(holed_revolution_results.front().volume -
                    (390.0 * std::numbers::pi -
                     13.0 * std::numbers::pi * std::numbers::pi)) < 1.0e-6,
                "Revolution with an inner circular loop has an incorrect volume");
        auto resized_revolution_hole = holed_revolution_document;
        resized_revolution_hole.sketches.front().circles.front().radius = 0.75;
        const auto resized_revolution_operations =
            resized_revolution_hole.kernel_operations();
        require(zima::kernel::history_fingerprint(resized_revolution_operations, 1) !=
                    holed_revolution_results.front().source_fingerprint,
                "Inner Revolution loop is missing from the history fingerprint");
        auto yz_revolution_document =
            zima::document::PartDocument::create_default();
        auto yz_revolution_sketch = zima::sketcher::Sketch::create_default();
        yz_revolution_sketch.plane = zima::sketcher::SketchPlane::YZ;
        yz_revolution_sketch.plane_offset = 3.0;
        static_cast<void>(yz_revolution_sketch.add_rectangle(
            10.0, 5.0, 20.0, 8.0));
        const auto yz_revolution_sketch_id = yz_revolution_sketch.id;
        yz_revolution_document.sketches.push_back(
            std::move(yz_revolution_sketch));
        yz_revolution_document.history.push_back(
            zima::document::PartDocument::create_revolution_container(
                yz_revolution_sketch_id));
        const auto yz_revolution_results = kernel.evaluate_history(
            yz_revolution_document.kernel_operations());
        require(std::abs(yz_revolution_results.front().volume -
                    390.0 * std::numbers::pi) < 1.0e-6 &&
                    yz_revolution_results.front().mesh.original_references.axes.front().direction.y > 0.999,
                "Sketch X Revolution axis was mapped incorrectly on the YZ plane");
        auto xz_revolution_document =
            zima::document::PartDocument::create_default();
        auto xz_revolution_sketch = zima::sketcher::Sketch::create_default();
        xz_revolution_sketch.plane = zima::sketcher::SketchPlane::XZ;
        xz_revolution_sketch.plane_offset = -2.0;
        static_cast<void>(xz_revolution_sketch.add_rectangle(
            5.0, 10.0, 8.0, 20.0));
        const auto xz_revolution_sketch_id = xz_revolution_sketch.id;
        xz_revolution_document.sketches.push_back(
            std::move(xz_revolution_sketch));
        auto xz_revolution =
            zima::document::PartDocument::create_revolution_container(
                xz_revolution_sketch_id);
        xz_revolution.revolution.axis =
            zima::document::RevolutionAxis::SketchY;
        xz_revolution_document.history.push_back(std::move(xz_revolution));
        const auto xz_revolution_results = kernel.evaluate_history(
            xz_revolution_document.kernel_operations());
        require(std::abs(xz_revolution_results.front().volume -
                    390.0 * std::numbers::pi) < 1.0e-6 &&
                    xz_revolution_results.front().mesh.original_references.axes.front().direction.z > 0.999,
                "Sketch Y Revolution axis was mapped incorrectly on the XZ plane");

        zima::document::DocumentSession session(
            zima::document::PartDocument::create_default());
        require(!session.is_dirty() && !session.can_undo(),
                "New document session must start clean");
        auto revision_one = session.document();
        revision_one.history.push_back(
            zima::document::PartDocument::create_box_container());
        zima::kernel::BodyResult revision_one_result;
        revision_one_result.volume = 1.0;
        session.commit(std::move(revision_one), {revision_one_result});
        require(session.revision() == 1 && session.is_dirty() && session.can_undo(),
                "First transaction did not create one dirty revision");
        require(session.calculated_boundaries().size() == 1 &&
                    session.calculated_boundaries().front().volume == 1.0,
                "Document revision did not own its calculated boundary");
        session.mark_saved();
        require(!session.is_dirty(), "Savepoint did not mark current revision clean");
        auto revision_two = session.document();
        revision_two.history.front().name = "Edited";
        zima::kernel::BodyResult revision_two_result;
        revision_two_result.volume = 2.0;
        session.commit(std::move(revision_two), {revision_two_result});
        require(session.is_dirty(), "Second transaction did not invalidate savepoint");
        require(session.undo(), "Undo failed");
        require(!session.is_dirty() && session.document().history.front().name == "Kvádr",
                "Undo did not restore the saved revision");
        require(session.calculated_boundaries().front().volume == 1.0,
                "Undo did not restore revision-owned calculated data");
        require(session.redo(), "Redo failed");
        require(session.is_dirty() &&
                    session.document().history.front().name == "Edited" &&
                    session.calculated_boundaries().front().volume == 2.0,
                "Redo did not restore the edited revision");
        require(session.undo(), "Second Undo failed");
        auto branch = session.document();
        branch.history.front().name = "Branch";
        session.commit(std::move(branch));
        require(!session.can_redo(), "New transaction did not clear the redo branch");
        zima::document::DocumentSession calculated_session(document);
        const auto calculation_revision = calculated_session.revision();
        calculated_session.update_calculated_boundaries(persisted_boundaries);
        require(calculated_session.revision() == calculation_revision &&
                    calculated_session.is_dirty() &&
                    calculated_session.calculated_boundaries().size() == 2,
                "Regeneration created a model revision or lost calculated data");
        calculated_session.mark_saved();
        require(!calculated_session.is_dirty(),
                "Saving regenerated derived data did not update its savepoint");
        std::cout << "C++ document and OCCT contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

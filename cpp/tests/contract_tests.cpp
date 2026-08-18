#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
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
        require(body.mesh.triangle_references.size() ==
                    body.mesh.triangles.size() / 3,
                "Viewer triangles and face references are not aligned");
        std::set<std::string> box_face_keys;
        for (const auto& reference : body.mesh.triangle_references) {
            require(reference.owner_id == "box" && reference.valid(),
                    "Primitive triangle lost its persisted owner");
            box_face_keys.insert(reference.semantic_key);
        }
        require(box_face_keys == std::set<std::string>{
                    "x_min", "x_max", "y_min", "y_max", "z_min", "z_max"},
                "Primitive semantic face keys are incomplete");
        std::set<std::string> box_edge_keys;
        for (const auto& edge : body.mesh.edges) {
            require(edge.reference.owner_id == "box" && edge.points.size() == 2,
                    "Primitive edge lost owner or geometry");
            box_edge_keys.insert(edge.reference.semantic_key);
        }
        require(box_edge_keys.size() == 12,
                "Primitive does not expose twelve unique semantic edges");
        std::set<std::string> box_vertex_keys;
        for (const auto& point : body.mesh.points) {
            require(point.reference.owner_id == "box",
                    "Primitive vertex lost its persisted owner");
            box_vertex_keys.insert(point.reference.semantic_key);
        }
        require(box_vertex_keys.size() == 8,
                "Primitive does not expose eight unique semantic vertices");
        std::set<std::string> box_axis_keys;
        for (const auto& axis : body.mesh.axes) {
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
        zima::kernel::BoxRequest resized_rotated{135.0, 62.0, 47.0};
        resized_rotated.rotation_degrees = {17.0, 29.0, 41.0};
        const auto regenerated = kernel.evaluate_boxes({
            {"persistent-box", resized_rotated,
             zima::kernel::BooleanOperation::Add},
        });
        std::set<std::string> regenerated_keys;
        for (const auto& reference : regenerated.mesh.triangle_references) {
            require(reference.owner_id == "persistent-box",
                    "Regeneration changed the persisted face owner");
            regenerated_keys.insert(reference.semantic_key);
        }
        require(regenerated_keys == box_face_keys,
                "Resize/rotation changed primitive semantic face identities");
        std::set<std::string> regenerated_edge_keys;
        for (const auto& edge : regenerated.mesh.edges) {
            regenerated_edge_keys.insert(edge.reference.semantic_key);
        }
        require(regenerated_edge_keys == box_edge_keys,
                "Resize/rotation changed primitive semantic edge identities");
        require(regenerated.mesh.axes.size() == 3 &&
                    std::abs(regenerated.mesh.axes.front().direction.y) > 1.0e-3,
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
        for (const auto& reference : cut_body.mesh.triangle_references) {
            if (reference.valid()) cut_owners.insert(reference.owner_id);
        }
        require(cut_owners.contains("base") && cut_owners.contains("cut"),
                "Boolean history did not propagate both original face owners");
        std::set<std::string> cut_edge_owners;
        for (const auto& edge : cut_body.mesh.edges) {
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
             cylinder_boundaries.front().mesh.triangle_references) {
            require(reference.owner_id == "cylinder",
                    "Cylinder face lost its stable owner");
            cylinder_faces.insert(reference.semantic_key);
        }
        require(cylinder_faces == std::set<std::string>{"side", "z_max", "z_min"},
                "Cylinder semantic faces are incomplete");
        std::set<std::string> cylinder_edges;
        bool sampled_circle = false;
        for (const auto& edge : cylinder_boundaries.front().mesh.edges) {
            cylinder_edges.insert(edge.reference.semantic_key);
            if (edge.reference.semantic_key.starts_with("circle:")) {
                sampled_circle = sampled_circle || edge.points.size() > 16;
            }
        }
        require(cylinder_edges == std::set<std::string>{
                    "circle:z_max", "circle:z_min", "seam"} && sampled_circle,
                "Cylinder edges are not stable selectable viewer polylines");
        require(cylinder_boundaries.front().mesh.axes.size() == 1 &&
                    cylinder_boundaries.front().mesh.axes.front()
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
                    loaded_boundaries.back().mesh.triangle_references.size() ==
                        persisted_boundaries.back().mesh.triangle_references.size() &&
                    std::abs(loaded_boundaries.back().volume -
                             persisted_boundaries.back().volume) < 1e-6,
                "Calculated viewer packets were not preserved");
        require(loaded_boundaries.back().mesh.dimensions.size() == 1 &&
                    loaded_boundaries.back().mesh.dimensions.front().value == 10.0 &&
                    loaded_boundaries.back().mesh.dimensions.front().reference.owner_id ==
                        first.id,
                "Persisted viewer dimension lost geometry, value, or stable owner");
        require(loaded_boundaries.back().mesh.axes.size() ==
                    persisted_boundaries.back().mesh.axes.size() &&
                    loaded_boundaries.back().mesh.axes.front().reference ==
                        persisted_boundaries.back().mesh.axes.front().reference,
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
        for (const auto& reference : extrusion_results.front().mesh.triangle_references) {
            require(reference.owner_id == extrusion_container_id && reference.valid(),
                    "Extrusion face lost its stable history owner");
            extrusion_faces.insert(reference.semantic_key);
        }
        require(extrusion_faces == std::set<std::string>{
                    "profile_start", "profile_end", "side:0", "side:1",
                    "side:2", "side:3"},
                "Extrusion does not expose stable start/end/side faces");
        require(extrusion_results.front().mesh.axes.size() == 1 &&
                    extrusion_results.front().mesh.axes.front().reference.semantic_key ==
                        "axis" &&
                    extrusion_results.front().mesh.axes.front().direction.z > 0.999,
                "Extrusion did not persist its normal axis");
        const auto extrusion_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-extrusion-contract.zcp.json";
        extrusion_document.save(extrusion_path, extrusion_results);
        std::vector<zima::kernel::BodyResult> loaded_extrusion_results;
        const auto loaded_extrusion = zima::document::PartDocument::load(
            extrusion_path, &loaded_extrusion_results);
        std::filesystem::remove(extrusion_path);
        require(loaded_extrusion.history.size() == 1 &&
                    loaded_extrusion.history.front().feature_kind ==
                        zima::document::FeatureKind::Extrusion &&
                    loaded_extrusion.history.front().extrusion.sketch_id ==
                        extrusion_sketch_id &&
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

#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <BRepPrimAPI_MakeBox.hxx>
#include <STEPControl_Writer.hxx>

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
        const auto step_path = std::filesystem::temp_directory_path() /
            "zima-cad-imported-step-contract.step";
        STEPControl_Writer step_writer;
        require(step_writer.Transfer(BRepPrimAPI_MakeBox(12.0, 8.0, 5.0).Shape(),
                    STEPControl_AsIs) == IFSelect_RetDone &&
                    step_writer.Write(step_path.string().c_str()) == IFSelect_RetDone,
                "STEP contract fixture could not be written");
        auto step_document = zima::document::PartDocument::create_default();
        step_document.history.push_back(
            zima::document::PartDocument::create_imported_step_container(step_path));
        const auto step_boundaries = kernel.evaluate_history(
            step_document.kernel_operations());
        require(step_boundaries.size() == 1 &&
                    std::abs(step_boundaries.back().volume - 480.0) < 1.0e-6 &&
                    step_boundaries.back().mesh.original_references
                        .triangle_references.empty(),
                "Imported STEP exposed traversal indices as persistent references");
        const auto step_document_path = std::filesystem::temp_directory_path() /
            "zima-cad-imported-step-contract.prtz";
        step_document.save(step_document_path, step_boundaries);
        std::vector<zima::kernel::BodyResult> loaded_step_boundaries;
        const auto loaded_step_document = zima::document::PartDocument::load(
            step_document_path, &loaded_step_boundaries);
        std::filesystem::remove(step_document_path);
        std::filesystem::remove(step_path);
        require(loaded_step_document.history.size() == 1 &&
                    loaded_step_document.history.front().feature_kind ==
                        zima::document::FeatureKind::ImportedStep &&
                    loaded_step_boundaries.size() == 1,
                "Imported STEP container did not survive Part save/load");
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
                {selected_box_edge},
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
                {selected_box_edge},
                zima::kernel::EdgeSelectionOrigin::OriginalEntity, 3.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(chamfer_boundaries.size() == 2 &&
                    chamfer_boundaries.back().volume < body.volume &&
                    chamfer_boundaries.back().volume > body.volume - 1000.0,
                "Original-edge Chamfer did not produce a valid bounded solid");
        const auto multi_fillet_boundaries = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"multi-fillet", zima::kernel::FilletRequest{{
                body.mesh.original_references.edges[0].reference,
                body.mesh.original_references.edges[1].reference},
                zima::kernel::EdgeSelectionOrigin::OriginalEntity, 2.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(multi_fillet_boundaries.size() == 2 &&
                    multi_fillet_boundaries.back().volume < body.volume,
                "Multi-edge Fillet did not treat all selected edges");
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
        zima::kernel::SphereRequest sphere;
        sphere.radius = 10.0;
        sphere.translation = {5.0, 6.0, 7.0};
        const auto sphere_boundaries = kernel.evaluate_history({
            {"sphere", sphere, zima::kernel::BooleanOperation::Add},
        });
        require(sphere_boundaries.size() == 1 &&
                    std::abs(sphere_boundaries.front().volume -
                        4.0 * std::numbers::pi * 1000.0 / 3.0) < 1e-5 &&
                    !sphere_boundaries.front().mesh.original_references
                        .triangle_references.empty() &&
                    sphere_boundaries.front().mesh.original_references
                        .triangle_references.front().semantic_key == "surface" &&
                    sphere_boundaries.front().mesh.original_references.axes.size() == 3,
                "Sphere geometry or stable references are incomplete");
        zima::kernel::ConeRequest cone;
        cone.bottom_radius = 10.0;
        cone.top_radius = 5.0;
        cone.height = 20.0;
        const auto cone_boundaries = kernel.evaluate_history({
            {"cone", cone, zima::kernel::BooleanOperation::Add},
        });
        const double cone_volume = std::numbers::pi * 20.0 *
            (100.0 + 50.0 + 25.0) / 3.0;
        require(cone_boundaries.size() == 1 &&
                    std::abs(cone_boundaries.front().volume - cone_volume) < 1e-5 &&
                    cone_boundaries.front().mesh.original_references.axes.size() == 1,
                "Cone geometry or stable axis is incorrect");
        zima::kernel::PyramidRequest pyramid;
        pyramid.length = 30.0; pyramid.width = 20.0; pyramid.height = 40.0;
        const auto pyramid_boundaries = kernel.evaluate_history({
            {"pyramid", pyramid, zima::kernel::BooleanOperation::Add}});
        require(pyramid_boundaries.size() == 1 &&
                    std::abs(pyramid_boundaries.front().volume - 8000.0) < 1e-5 &&
                    pyramid_boundaries.front().mesh.original_references.axes.size() == 3,
                "Pyramid geometry or references are incorrect");
        zima::kernel::WedgeRequest wedge;
        wedge.length = 60.0; wedge.width = 40.0;
        wedge.height = 40.0; wedge.top_offset = 30.0;
        const auto wedge_boundaries = kernel.evaluate_history({
            {"wedge", wedge, zima::kernel::BooleanOperation::Add}});
        require(wedge_boundaries.size() == 1 && wedge_boundaries.front().volume > 0.0 &&
                    wedge_boundaries.front().mesh.original_references.axes.size() == 3,
                "Wedge geometry or references are incorrect");
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
            "zima-cad-cpp-empty-contract.prtz";
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
            "zima-cad-cpp-contract.prtz";
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
            "zima-cad-cpp-cylinder-contract.prtz";
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
        auto sphere_document = zima::document::PartDocument::create_default();
        auto sphere_container = zima::document::PartDocument::create_sphere_container();
        sphere_container.sphere.radius = 22.0;
        sphere_document.history.push_back(sphere_container);
        const auto sphere_results = kernel.evaluate_history(sphere_document.kernel_operations());
        const auto sphere_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-sphere-contract.prtz";
        sphere_document.save(sphere_path, sphere_results);
        std::vector<zima::kernel::BodyResult> loaded_sphere_results;
        const auto loaded_sphere = zima::document::PartDocument::load(
            sphere_path, &loaded_sphere_results);
        std::filesystem::remove(sphere_path);
        require(loaded_sphere.history.front().feature_kind ==
                    zima::document::FeatureKind::Sphere &&
                    loaded_sphere.history.front().sphere.radius == 22.0 &&
                    loaded_sphere_results.size() == 1,
                "Sphere document did not survive save/load");
        auto cone_document = zima::document::PartDocument::create_default();
        auto cone_container = zima::document::PartDocument::create_cone_container();
        cone_container.cone = {18.0, 7.0, 42.0};
        cone_document.history.push_back(cone_container);
        const auto cone_results = kernel.evaluate_history(cone_document.kernel_operations());
        const auto cone_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-cone-contract.prtz";
        cone_document.save(cone_path, cone_results);
        const auto loaded_cone = zima::document::PartDocument::load(cone_path);
        std::filesystem::remove(cone_path);
        require(loaded_cone.history.front().feature_kind ==
                    zima::document::FeatureKind::Cone &&
                    loaded_cone.history.front().cone.top_radius == 7.0,
                "Cone document did not survive save/load");
        auto poly_document = zima::document::PartDocument::create_default();
        auto pyramid_container = zima::document::PartDocument::create_pyramid_container();
        pyramid_container.pyramid = {35.0, 25.0, 45.0};
        auto wedge_container = zima::document::PartDocument::create_wedge_container();
        wedge_container.wedge = {70.0, 30.0, 20.0, 15.0};
        wedge_container.placement.x = 100.0;
        poly_document.history = {pyramid_container, wedge_container};
        const auto poly_results = kernel.evaluate_history(poly_document.kernel_operations());
        const auto poly_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-poly-primitives-contract.prtz";
        poly_document.save(poly_path, poly_results);
        const auto loaded_poly = zima::document::PartDocument::load(poly_path);
        std::filesystem::remove(poly_path);
        require(loaded_poly.history.size() == 2 &&
                    loaded_poly.history[0].feature_kind == zima::document::FeatureKind::Pyramid &&
                    loaded_poly.history[1].feature_kind == zima::document::FeatureKind::Wedge &&
                    loaded_poly.history[1].wedge.top_offset == 15.0,
                "Pyramid/Wedge documents did not survive save/load");
        auto suppression_document = zima::document::PartDocument::create_default();
        suppression_document.history.push_back(
            zima::document::PartDocument::create_box_container());
        suppression_document.history.push_back(
            zima::document::PartDocument::create_cylinder_container());
        const auto unsuppressed_results = kernel.evaluate_history(
            suppression_document.kernel_operations());
        suppression_document.history.back().suppressed = true;
        suppression_document.user_parameters = {
            {"wall_thickness", "2.5 mm"}, {"rib_count", "4"}};
        const auto suppressed_results = kernel.evaluate_history(
            suppression_document.kernel_operations());
        require(suppressed_results.size() == 2 &&
                    suppressed_results.back().volume ==
                        suppressed_results.front().volume &&
                    suppressed_results.back().volume !=
                        unsuppressed_results.back().volume,
                "Suppressed Part feature changed boundary indexing or remained calculated");
        const auto suppression_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-suppression-contract.prtz";
        suppression_document.save(suppression_path, suppressed_results);
        const auto loaded_suppression = zima::document::PartDocument::load(
            suppression_path);
        std::filesystem::remove(suppression_path);
        require(loaded_suppression.history.back().suppressed,
                "Part feature suppression did not survive save/load");
        require(loaded_suppression.user_parameters ==
                    suppression_document.user_parameters,
                "Part user parameters did not survive save/load");
        auto constructions = zima::document::PartDocument::create_default();
        auto point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        point.origin = {1.0, 2.0, 3.0};
        auto axis = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        axis.direction = {1.0, 0.0, 0.0};
        auto plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        plane.display_size = 75.0;
        constructions.constructions = {point, axis, plane};
        const auto construction_mesh = constructions.construction_viewer_mesh();
        require(construction_mesh.points.size() == 1 &&
                    construction_mesh.axes.size() == 1 &&
                    construction_mesh.edges.size() == 1 &&
                    construction_mesh.original_references.points.size() == 1 &&
                    construction_mesh.original_references.points.front().reference.owner_id ==
                        point.id &&
                    construction_mesh.original_references.axes.size() == 1 &&
                    construction_mesh.original_references.axes.front().reference.owner_id ==
                        axis.id &&
                    construction_mesh.original_references.triangle_references.size() == 2,
                "Construction objects did not produce persisted ZIMA references");
        auto second_point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        second_point.origin = {11.0, 2.0, 3.0};
        auto referenced_axis = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        referenced_axis.definition =
            zima::document::ConstructionDefinition::TwoPointAxis;
        referenced_axis.references = {
            {{}, point.id, "point"}, {{}, second_point.id, "point"}};
        constructions.constructions.push_back(second_point);
        constructions.constructions.push_back(referenced_axis);
        constructions.resolve_constructions();
        require(constructions.constructions.back().reference_valid &&
                    constructions.constructions.back().origin.x == point.origin.x &&
                    constructions.constructions.back().origin.y == point.origin.y &&
                    constructions.constructions.back().origin.z == point.origin.z &&
                    constructions.constructions.back().direction.x == 1.0 &&
                    constructions.constructions.back().direction.y == 0.0,
                "Two-point datum axis did not resolve from stable point identities");
        auto cyclic_point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        cyclic_point.definition =
            zima::document::ConstructionDefinition::PointReference;
        cyclic_point.references = {{{}, cyclic_point.id, "point"}};
        constructions.constructions.push_back(cyclic_point);
        constructions.resolve_constructions();
        require(!constructions.constructions.back().reference_valid,
                "Construction dependency accepted a self-cycle");
        constructions.constructions.pop_back();
        auto construction_reference_sketch = zima::sketcher::Sketch::create_default();
        auto point_reference = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Point);
        point_reference.source_document_id = constructions.document_id;
        point_reference.source_owner_id = point.id;
        point_reference.source_semantic_key = "point";
        point_reference.cached_points = {{0.0, 0.0}};
        construction_reference_sketch.add_external_reference(point_reference);
        auto axis_reference = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Axis);
        axis_reference.source_document_id = constructions.document_id;
        axis_reference.source_owner_id = axis.id;
        axis_reference.source_semantic_key = "axis";
        axis_reference.cached_points = {{-1.0, 0.0}, {1.0, 0.0}};
        construction_reference_sketch.add_external_reference(axis_reference);
        auto plane_reference = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Face);
        plane_reference.source_document_id = constructions.document_id;
        plane_reference.source_owner_id = plane.id;
        plane_reference.source_semantic_key = "plane";
        plane_reference.cached_paths = {{{-1.0, -1.0}, {1.0, -1.0},
            {1.0, 1.0}, {-1.0, 1.0}, {-1.0, -1.0}}};
        construction_reference_sketch.add_external_reference(plane_reference);
        require(construction_reference_sketch.refresh_external_references(
                    constructions.document_id,
                    construction_mesh.original_references) &&
                    construction_reference_sketch.external_references[0].cached_points ==
                        std::vector<std::array<double, 2>>{{1.0, 2.0}} &&
                    !construction_reference_sketch.external_references[0].broken &&
                    !construction_reference_sketch.external_references[1].broken &&
                    !construction_reference_sketch.external_references[2].broken,
                "Construction external references did not resolve from viewer data");
        const auto construction_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-constructions-contract.prtz";
        constructions.save(construction_path);
        const auto loaded_constructions =
            zima::document::PartDocument::load(construction_path);
        std::filesystem::remove(construction_path);
        require(loaded_constructions.constructions.size() == 5 &&
                    loaded_constructions.constructions[0].origin.z == 3.0 &&
                    loaded_constructions.constructions[2].display_size == 75.0 &&
                    loaded_constructions.constructions.back().definition ==
                        zima::document::ConstructionDefinition::TwoPointAxis &&
                    loaded_constructions.constructions.back().references[0].owner_id ==
                        point.id,
                "Construction objects did not survive save/load");
        auto extrusion_document = zima::document::PartDocument::create_default();
        auto extrusion_sketch = zima::sketcher::Sketch::create_default();
        extrusion_sketch.name = "Obdélníkový profil";
        const auto extrusion_segment_ids =
            extrusion_sketch.add_rectangle(0.0, 0.0, 30.0, 20.0);
        std::vector<std::string> extrusion_point_ids;
        for (const auto& point : extrusion_sketch.points) {
            extrusion_point_ids.push_back(point.id);
        }
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
        require(extrusion_faces.contains("start") &&
                    extrusion_faces.contains("end") &&
                    std::ranges::all_of(extrusion_segment_ids,
                        [&](const auto& id) {
                            return extrusion_faces.contains("generated:" + id);
                        }),
                "Extrusion does not expose stable start/end/side faces");
        std::set<std::string> extrusion_edges;
        for (const auto& edge :
             extrusion_results.front().mesh.original_references.edges) {
            extrusion_edges.insert(edge.reference.semantic_key);
        }
        std::set<std::string> extrusion_vertices;
        for (const auto& vertex :
             extrusion_results.front().mesh.original_references.points) {
            extrusion_vertices.insert(vertex.reference.semantic_key);
        }
        require(std::ranges::all_of(extrusion_segment_ids, [&](const auto& id) {
                    return extrusion_edges.contains("start:" + id) &&
                        extrusion_edges.contains("end:" + id);
                }) &&
                std::ranges::all_of(extrusion_point_ids, [&](const auto& id) {
                    return extrusion_edges.contains("generated:" + id) &&
                        extrusion_vertices.contains("start:" + id) &&
                        extrusion_vertices.contains("end:" + id);
                }),
                "Extrusion topology did not preserve curve/point ancestry");
        auto extrusion_reference_sketch = zima::sketcher::Sketch::create_default();
        auto inherited_edge_reference =
            zima::sketcher::Sketch::create_external_reference(
                zima::sketcher::ExternalReferenceKind::Edge);
        inherited_edge_reference.source_document_id = extrusion_document.document_id;
        inherited_edge_reference.source_owner_id = extrusion_container_id;
        inherited_edge_reference.source_semantic_key =
            "start:" + extrusion_segment_ids.front();
        inherited_edge_reference.cached_points = {{-1.0, -1.0}, {-2.0, -2.0}};
        extrusion_reference_sketch.add_external_reference(
            inherited_edge_reference);
        require(extrusion_reference_sketch.refresh_external_references(
                    extrusion_document.document_id,
                    extrusion_results.front().mesh.original_references) &&
                    !extrusion_reference_sketch.external_references.front().broken &&
                    extrusion_reference_sketch.external_references.front()
                        .source_semantic_key ==
                        "start:" + extrusion_segment_ids.front(),
                "Sketch external reference lost inherited Extrusion ancestry");
        require(zima::sketcher::Sketch::from_serialized(
                    extrusion_reference_sketch.serialized())
                    .external_references ==
                extrusion_reference_sketch.external_references,
                "Inherited Extrusion reference did not survive Sketch persistence");
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
        const auto referenced_face_z = [](const zima::kernel::BodyResult& result,
                                          const std::string& semantic_key) {
            const auto& references = result.mesh.original_references;
            double sum{};
            std::size_t count{};
            for (std::size_t triangle = 0;
                 triangle < references.triangle_references.size(); ++triangle) {
                if (references.triangle_references[triangle].semantic_key !=
                    semantic_key) continue;
                for (int corner = 0; corner < 3; ++corner) {
                    sum += references.vertices[
                        references.triangles[triangle * 3 + corner]].z;
                    ++count;
                }
            }
            require(count != 0, "Requested semantic cap face is missing");
            return sum / static_cast<double>(count);
        };
        require(std::abs(referenced_face_z(extrusion_results.front(), "start")) <
                    1.0e-7 &&
                std::abs(referenced_face_z(extrusion_results.front(), "end") -
                    10.0) < 1.0e-7,
                "Forward Extrusion swapped semantic start/end caps");
        auto reverse_extrusion_document = extrusion_document;
        reverse_extrusion_document.history.front().extrusion.direction =
            zima::document::ExtrusionDirection::Reverse;
        const auto reverse_extrusion_results = kernel.evaluate_history(
            reverse_extrusion_document.kernel_operations());
        const auto reverse_bounds = z_bounds(reverse_extrusion_results.front());
        require(std::abs(reverse_bounds[0] + 10.0) < 1.0e-7 &&
                    std::abs(reverse_bounds[1]) < 1.0e-7 &&
                    std::abs(referenced_face_z(
                        reverse_extrusion_results.front(), "start") + 10.0) <
                        1.0e-7 &&
                    std::abs(referenced_face_z(
                        reverse_extrusion_results.front(), "end")) < 1.0e-7,
                "Reverse Extrusion is not located behind the Sketch plane");
        auto symmetric_extrusion_document = extrusion_document;
        symmetric_extrusion_document.history.front().extrusion.direction =
            zima::document::ExtrusionDirection::Symmetric;
        const auto symmetric_extrusion_results = kernel.evaluate_history(
            symmetric_extrusion_document.kernel_operations());
        const auto symmetric_bounds = z_bounds(symmetric_extrusion_results.front());
        require(std::abs(symmetric_bounds[0] + 5.0) < 1.0e-7 &&
                    std::abs(symmetric_bounds[1] - 5.0) < 1.0e-7 &&
                    std::abs(referenced_face_z(
                        symmetric_extrusion_results.front(), "start") + 5.0) <
                        1.0e-7 &&
                    std::abs(referenced_face_z(
                        symmetric_extrusion_results.front(), "end") - 5.0) <
                        1.0e-7 &&
                    symmetric_extrusion_results.front().source_fingerprint !=
                        extrusion_results.front().source_fingerprint,
                "Symmetric Extrusion is not centered on the Sketch plane");
        const auto extrusion_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-extrusion-contract.prtz";
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

        // One uninterrupted parity slice: Sketch -> Extrusion -> Fillet ->
        // source-Sketch edit -> regeneration -> save/reload.  The Fillet must
        // continue to resolve the generated edge through its Sketch-point
        // ancestry instead of an OCCT edge position.
        auto parity_document = zima::document::PartDocument::create_default();
        auto parity_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(parity_sketch.add_rectangle(0.0, 0.0, 24.0, 16.0));
        const auto parity_sketch_id = parity_sketch.id;
        const auto parity_point_id = parity_sketch.points.front().id;
        parity_document.sketches.push_back(std::move(parity_sketch));
        auto parity_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                parity_sketch_id);
        parity_extrusion.extrusion.height = 12.0;
        parity_document.history.push_back(parity_extrusion);
        const auto parity_extruded = kernel.evaluate_history(
            parity_document.kernel_operations());
        const auto parity_edge = std::ranges::find_if(
            parity_extruded.back().mesh.original_references.edges,
            [&](const auto& edge) {
                return edge.reference.owner_id == parity_extrusion.id &&
                    edge.reference.semantic_key ==
                        "generated:" + parity_point_id;
            });
        require(parity_edge !=
                    parity_extruded.back().mesh.original_references.edges.end(),
                "Parity workflow could not resolve the point-generated edge");
        auto parity_fillet =
            zima::document::PartDocument::create_fillet_container(
                {parity_edge->reference});
        parity_fillet.edge_treatment.size = 1.5;
        parity_document.history.push_back(parity_fillet);
        const auto parity_filleted = kernel.evaluate_history(
            parity_document.kernel_operations());
        require(parity_filleted.size() == 2 &&
                    parity_filleted.back().volume < parity_extruded.back().volume,
                "Parity workflow Fillet did not consume inherited edge identity");
        for (auto& point : parity_document.sketches.front().points) {
            if (point.x > 12.0) point.x += 6.0;
        }
        const auto parity_regenerated = kernel.evaluate_history(
            parity_document.kernel_operations());
        require(parity_regenerated.size() == 2 &&
                    parity_regenerated.back().volume >
                        parity_filleted.back().volume &&
                    std::ranges::any_of(
                        parity_regenerated.back().mesh.original_references.edges,
                        [&](const auto& edge) {
                            return edge.reference.owner_id == parity_extrusion.id &&
                                edge.reference.semantic_key ==
                                    "generated:" + parity_point_id;
                        }),
                "Sketch edit broke downstream inherited Fillet ancestry");
        const auto parity_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-part-parity-slice.prtz";
        parity_document.save(parity_path, parity_regenerated);
        std::vector<zima::kernel::BodyResult> loaded_parity_results;
        const auto loaded_parity = zima::document::PartDocument::load(
            parity_path, &loaded_parity_results);
        std::filesystem::remove(parity_path);
        require(loaded_parity.history.size() == 2 &&
                    loaded_parity_results.size() == 2 &&
                    loaded_parity.history.back().feature_kind ==
                        zima::document::FeatureKind::Fillet,
                "Complete Part parity workflow did not survive save/reload");
        auto up_to_document = zima::document::PartDocument::create_default();
        auto up_to_base = zima::document::PartDocument::create_box_container();
        up_to_base.box = {20.0, 20.0, 10.0};
        up_to_document.history.push_back(up_to_base);
        auto up_to_sketch = zima::sketcher::Sketch::create_default();
        up_to_sketch.plane_offset = 10.0;
        static_cast<void>(up_to_sketch.add_rectangle(0.0, 0.0, 10.0, 10.0));
        const auto up_to_sketch_id = up_to_sketch.id;
        up_to_document.sketches.push_back(std::move(up_to_sketch));
        auto target_plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        target_plane.origin = {0.0, 0.0, 30.0};
        const auto target_plane_id = target_plane.id;
        up_to_document.constructions.push_back(target_plane);
        auto up_to = zima::document::PartDocument::create_extrusion_container(
            up_to_sketch_id);
        up_to.extrusion.extent = zima::document::ExtrusionExtent::UpToPlane;
        up_to.extrusion.target_face = {target_plane_id, "plane", {}};
        up_to.extrusion.target_plane_origin = target_plane.origin;
        up_to.extrusion.target_plane_normal = target_plane.direction;
        up_to_document.history.push_back(up_to);
        const auto up_to_preview = up_to_document.extrusion_preview_edges(up_to);
        require(!up_to_preview.empty() && std::any_of(
                    up_to_preview.begin(), up_to_preview.end(), [](const auto& edge) {
                        return std::any_of(edge.points.begin(), edge.points.end(),
                            [](const auto& point) {
                                return std::abs(point.z - 30.0) < 1e-9;
                            });
                    }),
                "Up-to-plane cyan wire does not terminate on its target");
        const auto up_to_results =
            kernel.evaluate_history(up_to_document.kernel_operations());
        require(up_to_results.size() == 2 &&
                    std::abs(up_to_results.back().volume - 6000.0) < 1e-6,
                "Up-to-plane Extrusion did not clip at the datum plane");
        auto inclined_document = up_to_document;
        inclined_document.constructions.front().direction = {-0.5, 0.0, 1.0};
        inclined_document.history.back().extrusion.target_plane_normal =
            {-0.5, 0.0, 1.0};
        const auto inclined_results =
            kernel.evaluate_history(inclined_document.kernel_operations());
        require(std::abs(inclined_results.back().volume - 6250.0) < 1e-5,
                "Inclined Up-to plane was flattened or clipped on the wrong side");
        auto face_target_document = zima::document::PartDocument::create_default();
        auto face_target_base = zima::document::PartDocument::create_box_container();
        face_target_base.box = {20.0, 20.0, 10.0};
        const auto face_target_base_id = face_target_base.id;
        face_target_document.history.push_back(face_target_base);
        auto face_target_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(face_target_sketch.add_rectangle(0.0, 0.0, 10.0, 10.0));
        const auto face_target_sketch_id = face_target_sketch.id;
        face_target_document.sketches.push_back(std::move(face_target_sketch));
        auto face_target_cut = zima::document::PartDocument::create_extrusion_container(
            face_target_sketch_id);
        face_target_cut.combine_mode = zima::document::CombineMode::Subtract;
        face_target_cut.extrusion.extent =
            zima::document::ExtrusionExtent::UpToPlane;
        face_target_cut.extrusion.target_face = {
            face_target_base_id, "z_max", {}};
        face_target_cut.extrusion.target_plane_origin = {0.0, 0.0, 10.0};
        face_target_cut.extrusion.target_plane_normal = {0.0, 0.0, 1.0};
        face_target_document.history.push_back(face_target_cut);
        const auto face_target_results =
            kernel.evaluate_history(face_target_document.kernel_operations());
        require(std::abs(face_target_results.back().volume - 3000.0) < 1e-6,
                "Up-to stable original face did not resolve at its history boundary");
        auto curved_target_document = zima::document::PartDocument::create_default();
        auto curved_target_sphere =
            zima::document::PartDocument::create_sphere_container();
        curved_target_sphere.sphere.radius = 20.0;
        const auto curved_target_owner = curved_target_sphere.id;
        curved_target_document.history.push_back(curved_target_sphere);
        const auto sphere_boundary =
            kernel.evaluate_history(curved_target_document.kernel_operations()).front();
        std::vector<zima::kernel::Vec3> sphere_triangles;
        const auto& sphere_references = sphere_boundary.mesh.original_references;
        for (std::size_t triangle = 0;
             triangle < sphere_references.triangle_references.size(); ++triangle) {
            const auto& reference = sphere_references.triangle_references[triangle];
            if (reference.owner_id != curved_target_owner ||
                reference.semantic_key != "surface") continue;
            for (int corner = 0; corner < 3; ++corner) {
                sphere_triangles.push_back(sphere_references.vertices[
                    sphere_references.triangles[triangle * 3 + corner]]);
            }
        }
        auto curved_target_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(curved_target_sketch.add_rectangle(
            1.0, 1.0, 10.0, 10.0));
        const auto curved_target_sketch_id = curved_target_sketch.id;
        curved_target_document.sketches.push_back(std::move(curved_target_sketch));
        auto curved_target_cut =
            zima::document::PartDocument::create_extrusion_container(
                curved_target_sketch_id);
        curved_target_cut.combine_mode = zima::document::CombineMode::Subtract;
        curved_target_cut.extrusion.extent =
            zima::document::ExtrusionExtent::UpToSurface;
        curved_target_cut.extrusion.target_face = {
            curved_target_owner, "surface", {}};
        curved_target_cut.extrusion.target_surface_triangles = sphere_triangles;
        curved_target_document.history.push_back(curved_target_cut);
        const auto curved_preview =
            curved_target_document.extrusion_preview_edges(curved_target_cut);
        double curved_minimum_z = std::numeric_limits<double>::infinity();
        double curved_maximum_z = -std::numeric_limits<double>::infinity();
        for (const auto& edge : curved_preview) {
            if (edge.reference.semantic_key != "preview:end") continue;
            for (const auto& point : edge.points) {
                curved_minimum_z = std::min(curved_minimum_z, point.z);
                curved_maximum_z = std::max(curved_maximum_z, point.z);
            }
        }
        require(curved_maximum_z - curved_minimum_z > 2.0,
                "Curved Up-to preview was flattened to one scalar endpoint");
        const auto curved_target_results =
            kernel.evaluate_history(curved_target_document.kernel_operations());
        require(curved_target_results.size() == 2 &&
                    curved_target_results.back().volume > 0.0 &&
                    curved_target_results.back().volume < sphere_boundary.volume,
                "Up-to curved surface did not cut the source sphere");

        auto ellipse_target_document = zima::document::PartDocument::create_default();
        auto ellipse_target_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(ellipse_target_sketch.add_ellipse(
            0.0, 0.0, 20.0, 0.0, 0.0, 10.0));
        const auto ellipse_target_sketch_id = ellipse_target_sketch.id;
        ellipse_target_document.sketches.push_back(std::move(ellipse_target_sketch));
        auto ellipse_target_feature =
            zima::document::PartDocument::create_extrusion_container(
                ellipse_target_sketch_id);
        ellipse_target_feature.extrusion.height = 30.0;
        const auto ellipse_target_owner = ellipse_target_feature.id;
        ellipse_target_document.history.push_back(ellipse_target_feature);
        const auto ellipse_boundary = kernel.evaluate_history(
            ellipse_target_document.kernel_operations()).front();
        std::vector<zima::kernel::Vec3> ellipse_side_triangles;
        std::string ellipse_side_key;
        const auto& ellipse_references = ellipse_boundary.mesh.original_references;
        for (std::size_t triangle = 0;
             triangle < ellipse_references.triangle_references.size(); ++triangle) {
            const auto& reference = ellipse_references.triangle_references[triangle];
            if (reference.owner_id != ellipse_target_owner ||
                !reference.semantic_key.starts_with("generated:")) continue;
            ellipse_side_key = reference.semantic_key;
            for (int corner = 0; corner < 3; ++corner) {
                ellipse_side_triangles.push_back(ellipse_references.vertices[
                    ellipse_references.triangles[triangle * 3 + corner]]);
            }
        }
        auto transverse_sketch = zima::sketcher::Sketch::create_default();
        transverse_sketch.plane = zima::sketcher::SketchPlane::YZ;
        transverse_sketch.plane_offset = -30.0;
        static_cast<void>(transverse_sketch.add_rectangle(1.0, 5.0, 5.0, 10.0));
        const auto transverse_sketch_id = transverse_sketch.id;
        ellipse_target_document.sketches.push_back(std::move(transverse_sketch));
        auto ellipse_up_to = zima::document::PartDocument::create_extrusion_container(
            transverse_sketch_id);
        ellipse_up_to.extrusion.extent =
            zima::document::ExtrusionExtent::UpToSurface;
        ellipse_up_to.extrusion.target_face = {
            ellipse_target_owner, ellipse_side_key, {}};
        ellipse_up_to.extrusion.target_surface_triangles = ellipse_side_triangles;
        ellipse_target_document.history.push_back(ellipse_up_to);
        const auto ellipse_up_to_results = kernel.evaluate_history(
            ellipse_target_document.kernel_operations());
        require(ellipse_up_to_results.size() == 2 &&
                    ellipse_up_to_results.back().volume > ellipse_boundary.volume,
                "Extrusion could not terminate on an elliptic Extrusion surface");

        auto spline_target_document = zima::document::PartDocument::create_default();
        auto spline_target_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(spline_target_sketch.add_bspline({
            {-20.0, 0.0}, {-15.0, 15.0}, {0.0, 22.0}, {15.0, 15.0},
            {20.0, 0.0}, {10.0, -18.0}, {-10.0, -18.0}}, 3, true));
        const auto spline_target_sketch_id = spline_target_sketch.id;
        spline_target_document.sketches.push_back(std::move(spline_target_sketch));
        auto spline_target_feature =
            zima::document::PartDocument::create_extrusion_container(
                spline_target_sketch_id);
        spline_target_feature.extrusion.height = 30.0;
        const auto spline_target_owner = spline_target_feature.id;
        spline_target_document.history.push_back(spline_target_feature);
        const auto spline_boundary = kernel.evaluate_history(
            spline_target_document.kernel_operations()).front();
        std::vector<zima::kernel::Vec3> spline_side_triangles;
        std::string spline_side_key;
        const auto& spline_references = spline_boundary.mesh.original_references;
        for (std::size_t triangle = 0;
             triangle < spline_references.triangle_references.size(); ++triangle) {
            const auto& reference = spline_references.triangle_references[triangle];
            if (reference.owner_id != spline_target_owner ||
                !reference.semantic_key.starts_with("generated:")) continue;
            spline_side_key = reference.semantic_key;
            for (int corner = 0; corner < 3; ++corner) {
                spline_side_triangles.push_back(spline_references.vertices[
                    spline_references.triangles[triangle * 3 + corner]]);
            }
        }
        auto spline_transverse_sketch = zima::sketcher::Sketch::create_default();
        spline_transverse_sketch.plane = zima::sketcher::SketchPlane::YZ;
        spline_transverse_sketch.plane_offset = -30.0;
        static_cast<void>(spline_transverse_sketch.add_rectangle(
            1.0, 5.0, 5.0, 10.0));
        const auto spline_transverse_id = spline_transverse_sketch.id;
        spline_target_document.sketches.push_back(
            std::move(spline_transverse_sketch));
        auto spline_up_to = zima::document::PartDocument::create_extrusion_container(
            spline_transverse_id);
        spline_up_to.extrusion.extent =
            zima::document::ExtrusionExtent::UpToSurface;
        spline_up_to.extrusion.target_face = {
            spline_target_owner, spline_side_key, {}};
        spline_up_to.extrusion.target_surface_triangles = spline_side_triangles;
        spline_target_document.history.push_back(spline_up_to);
        const auto spline_up_to_results = kernel.evaluate_history(
            spline_target_document.kernel_operations());
        require(spline_up_to_results.size() == 2 &&
                    spline_up_to_results.back().volume > spline_boundary.volume,
                "Extrusion could not terminate on a B-spline Extrusion surface");
        const auto spline_target_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-spline-up-to-contract.prtz";
        spline_target_document.save(spline_target_path, spline_up_to_results);
        const auto loaded_spline_target =
            zima::document::PartDocument::load(spline_target_path);
        std::filesystem::remove(spline_target_path);
        require(loaded_spline_target.history.back().extrusion.extent ==
                    zima::document::ExtrusionExtent::UpToSurface &&
                    loaded_spline_target.history.back().extrusion
                        .target_surface_triangles.size() ==
                        spline_side_triangles.size(),
                "B-spline Up-to target triangulation did not survive save/load");
        const auto up_to_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-up-to-contract.prtz";
        up_to_document.save(up_to_path, up_to_results);
        const auto loaded_up_to = zima::document::PartDocument::load(up_to_path);
        std::filesystem::remove(up_to_path);
        require(loaded_up_to.history.back().extrusion.extent ==
                    zima::document::ExtrusionExtent::UpToPlane &&
                    loaded_up_to.history.back().extrusion.target_face.owner_id ==
                        target_plane_id,
                "Up-to-plane target did not survive save/load");

        auto through_document = zima::document::PartDocument::create_default();
        auto through_base = zima::document::PartDocument::create_box_container();
        through_base.box = {20.0, 20.0, 10.0};
        through_document.history.push_back(through_base);
        auto through_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(through_sketch.add_rectangle(0.0, 0.0, 5.0, 5.0));
        const auto through_sketch_id = through_sketch.id;
        through_document.sketches.push_back(std::move(through_sketch));
        auto through = zima::document::PartDocument::create_extrusion_container(
            through_sketch_id);
        through.combine_mode = zima::document::CombineMode::Subtract;
        through.extrusion.extent = zima::document::ExtrusionExtent::ThroughAll;
        through_document.history.push_back(through);
        const auto through_results =
            kernel.evaluate_history(through_document.kernel_operations());
        require(through_results.size() == 2 &&
                    std::abs(through_results.back().volume - 3750.0) < 1e-6,
                "Through-all subtractive Extrusion did not cross the complete body");
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
                reference.semantic_key.starts_with("generated:")) {
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
        const auto multiple_circle_results = kernel.evaluate_history(
            multiple_circle_document.kernel_operations());
        require(std::abs(multiple_circle_results.back().volume -
                    (16000.0 - 290.0 * std::numbers::pi)) < 1.0e-6,
                "Disjoint circular profile regions did not share one feature");

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
                reference.semantic_key.starts_with("generated:")) {
                hole_side_found = true;
            }
        }
        require(hole_side_found,
                "Inner profile wall lost its stable Extrusion owner");

        auto text_profile_sketch = zima::sketcher::Sketch::create_default();
        auto profile_text = zima::sketcher::Sketch::create_text();
        profile_text.value = "OI";
        profile_text.contours = {
            {{1.0, 1.0}, {11.0, 1.0}, {11.0, 11.0}, {1.0, 11.0}},
            {{4.0, 4.0}, {8.0, 4.0}, {8.0, 8.0}, {4.0, 8.0}},
            {{15.0, 1.0}, {20.0, 1.0}, {20.0, 11.0}, {15.0, 11.0}}};
        const auto profile_text_id = profile_text.id;
        text_profile_sketch.add_text(std::move(profile_text));
        const auto text_profile_sketch_id = text_profile_sketch.id;
        auto text_profile_document = zima::document::PartDocument::create_default();
        text_profile_document.sketches.push_back(text_profile_sketch);
        auto text_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                text_profile_sketch_id);
        text_extrusion.extrusion.height = 5.0;
        text_profile_document.history.push_back(std::move(text_extrusion));
        const auto text_profile_operations = text_profile_document.kernel_operations();
        const auto& text_request = std::get<zima::kernel::ExtrusionRequest>(
            text_profile_operations.front().primitive);
        require(text_request.inner_profiles.size() == 1 &&
                    text_request.additional_profile_regions.size() == 1,
                "Text contours were not classified into a holed glyph and a second glyph");
        const auto text_profile_results =
            kernel.evaluate_history(text_profile_operations);
        require(std::abs(text_profile_results.front().volume - 670.0) < 1.0e-6,
                "Multi-glyph Text Extrusion has an incorrect exact volume");
        bool stable_text_boundary_found = false;
        for (const auto& reference : text_profile_results.front().mesh
                 .original_references.triangle_references) {
            if (reference.semantic_key == "generated:" + profile_text_id) {
                stable_text_boundary_found = true;
            }
        }
        require(stable_text_boundary_found,
                "Text profile side lost its stable source-boundary identity");
        const auto text_profile_path = std::filesystem::temp_directory_path() /
            "zima-cad-text-profile-contract.prtz";
        text_profile_document.save(text_profile_path);
        const auto restored_text_document =
            zima::document::PartDocument::load(text_profile_path);
        require(restored_text_document.sketches.front().texts.size() == 1 &&
                    restored_text_document.sketches.front().texts.front().id ==
                        profile_text_id &&
                    restored_text_document.sketches.front().texts.front().value == "OI",
                "Using Text as a profile destroyed its editable semantic entity");
        std::filesystem::remove(text_profile_path);
        auto changed_text_profile = text_profile_document;
        changed_text_profile.sketches.front().texts.front().contours.front()[1][0] =
            12.0;
        require(zima::kernel::history_fingerprint(
                    changed_text_profile.kernel_operations(), 1) !=
                    text_profile_results.front().source_fingerprint,
                "Text contours are missing from the history fingerprint");

        auto text_cut_document = zima::document::PartDocument::create_default();
        auto text_cut_box = zima::document::PartDocument::create_box_container();
        text_cut_box.box.length = 30.0;
        text_cut_box.box.width = 20.0;
        text_cut_box.box.height = 5.0;
        text_cut_document.history.push_back(std::move(text_cut_box));
        text_cut_document.sketches.push_back(text_profile_sketch);
        auto text_cut = zima::document::PartDocument::create_extrusion_container(
            text_profile_sketch_id);
        text_cut.combine_mode = zima::document::CombineMode::Subtract;
        text_cut.extrusion.height = 5.0;
        text_cut_document.history.push_back(std::move(text_cut));
        const auto text_cut_results = kernel.evaluate_history(
            text_cut_document.kernel_operations());
        require(std::abs(text_cut_results.back().volume - 2330.0) < 1.0e-6,
                "Subtractive multi-glyph Text Extrusion has an incorrect volume");

        auto text_revolution_sketch = zima::sketcher::Sketch::create_default();
        auto revolution_text = zima::sketcher::Sketch::create_text();
        revolution_text.value = "II";
        revolution_text.contours = {
            {{1.0, 5.0}, {6.0, 5.0}, {6.0, 10.0}, {1.0, 10.0}},
            {{9.0, 5.0}, {14.0, 5.0}, {14.0, 10.0}, {9.0, 10.0}}};
        text_revolution_sketch.add_text(std::move(revolution_text));
        auto text_revolution_document = zima::document::PartDocument::create_default();
        const auto text_revolution_sketch_id = text_revolution_sketch.id;
        text_revolution_document.sketches.push_back(std::move(text_revolution_sketch));
        text_revolution_document.history.push_back(
            zima::document::PartDocument::create_revolution_container(
                text_revolution_sketch_id));
        const auto text_revolution_results = kernel.evaluate_history(
            text_revolution_document.kernel_operations());
        require(std::abs(text_revolution_results.front().volume -
                    750.0 * std::numbers::pi) < 1.0e-5,
                "Multi-glyph Text Revolution has an incorrect exact volume");

        auto invalid_text_profile = text_profile_document;
        invalid_text_profile.sketches.front().texts.front().contours.push_back(
            {{10.0, 5.0}, {16.0, 5.0}, {16.0, 7.0}, {10.0, 7.0}});
        bool overlapping_text_rejected = false;
        try {
            static_cast<void>(invalid_text_profile.kernel_operations());
        } catch (const std::runtime_error&) {
            overlapping_text_rejected = true;
        }
        require(overlapping_text_rejected,
                "Overlapping Text contours reached the solid kernel");

        auto mixed_profile_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(mixed_profile_sketch.add_rectangle(
            0.0, 0.0, 10.0, 10.0));
        auto mixed_text = zima::sketcher::Sketch::create_text();
        mixed_text.value = "I";
        mixed_text.contours = {
            {{15.0, 0.0}, {20.0, 0.0}, {20.0, 10.0}, {15.0, 10.0}}};
        mixed_profile_sketch.add_text(std::move(mixed_text));
        auto mixed_profile_document = zima::document::PartDocument::create_default();
        const auto mixed_profile_sketch_id = mixed_profile_sketch.id;
        mixed_profile_document.sketches.push_back(std::move(mixed_profile_sketch));
        auto mixed_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                mixed_profile_sketch_id);
        mixed_extrusion.extrusion.height = 4.0;
        mixed_profile_document.history.push_back(std::move(mixed_extrusion));
        const auto mixed_profile_results = kernel.evaluate_history(
            mixed_profile_document.kernel_operations());
        require(std::abs(mixed_profile_results.front().volume - 600.0) < 1.0e-6,
                "Mixed Text and Segment profile did not create both exact regions");

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
                reference.semantic_key.starts_with("generated:")) {
                arc_profile_sides.insert(reference.semantic_key);
            }
        }
        require(arc_profile_sides.size() == 3,
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
        auto trimmed_ellipse_document =
            zima::document::PartDocument::create_default();
        auto trimmed_ellipse_sketch = zima::sketcher::Sketch::create_default();
        const auto trimmed_ellipse_id = trimmed_ellipse_sketch.add_ellipse(
            0.0, 0.0, 10.0, 0.0, 0.0, 4.0);
        static_cast<void>(trimmed_ellipse_sketch.add_segment(
            -10.0, 0.0, 10.0, 0.0));
        const auto trimmed_ellipse_topology =
            zima::sketcher::sketch_trim_topology(trimmed_ellipse_sketch, false);
        const auto lower_half = zima::sketcher::nearest_sketch_trim_piece(
            trimmed_ellipse_topology, {0.0, -4.0}, 0.25);
        require(lower_half && lower_half->geometry_id == trimmed_ellipse_id,
                "Ellipse half was not available to the exact Trim operation");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            trimmed_ellipse_sketch, {*lower_half}));
        require(trimmed_ellipse_sketch.ellipses.empty() &&
                    trimmed_ellipse_sketch.elliptical_arcs.size() == 1,
                "Trim did not convert the Ellipse survivor to an exact elliptical Arc");
        const auto trimmed_ellipse_sketch_id = trimmed_ellipse_sketch.id;
        trimmed_ellipse_document.sketches.push_back(
            std::move(trimmed_ellipse_sketch));
        auto trimmed_ellipse_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                trimmed_ellipse_sketch_id);
        trimmed_ellipse_extrusion.extrusion.direction =
            zima::document::ExtrusionDirection::Reverse;
        trimmed_ellipse_document.history.push_back(
            std::move(trimmed_ellipse_extrusion));
        const auto trimmed_ellipse_results = kernel.evaluate_history(
            trimmed_ellipse_document.kernel_operations());
        require(std::abs(trimmed_ellipse_results.front().volume -
                    200.0 * std::numbers::pi) < 1.0e-6,
                "Exact trimmed Ellipse profile has an incorrect solid volume");
        auto changed_elliptical_arc_operations =
            trimmed_ellipse_document.kernel_operations();
        auto& changed_elliptical_extrusion =
            std::get<zima::kernel::ExtrusionRequest>(
                changed_elliptical_arc_operations.front().primitive);
        auto& changed_curved_profile =
            std::get<zima::kernel::ExtrusionRequest::CurvedProfile>(
                changed_elliptical_extrusion.outer_profile);
        bool changed_elliptical_parameter = false;
        for (auto& curve : changed_curved_profile.curves) {
            if (auto* elliptical_arc = std::get_if<
                    zima::kernel::ExtrusionRequest::EllipticalArcCurve>(&curve)) {
                elliptical_arc->end_parameter -= 0.01;
                changed_elliptical_parameter = true;
            }
        }
        require(changed_elliptical_parameter &&
                    zima::kernel::history_fingerprint(
                        changed_elliptical_arc_operations, 1) !=
                        trimmed_ellipse_results.front().source_fingerprint,
                "Elliptical Arc parameters are missing from the history fingerprint");
        auto swapped_ellipse_document =
            zima::document::PartDocument::create_default();
        auto swapped_ellipse_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(swapped_ellipse_sketch.add_elliptical_arc(
            0.0, 0.0, 4.0, 0.0, 0.0, 10.0,
            4.0, 0.0, -4.0, 0.0));
        static_cast<void>(swapped_ellipse_sketch.add_segment(
            -4.0, 0.0, 4.0, 0.0));
        const auto swapped_ellipse_sketch_id = swapped_ellipse_sketch.id;
        swapped_ellipse_document.sketches.push_back(
            std::move(swapped_ellipse_sketch));
        auto swapped_ellipse_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                swapped_ellipse_sketch_id);
        swapped_ellipse_extrusion.extrusion.height = 5.0;
        swapped_ellipse_document.history.push_back(
            std::move(swapped_ellipse_extrusion));
        const auto swapped_ellipse_results = kernel.evaluate_history(
            swapped_ellipse_document.kernel_operations());
        require(std::abs(swapped_ellipse_results.front().volume -
                    100.0 * std::numbers::pi) < 1.0e-6,
                "Elliptical Arc with swapped semiaxis order changed its exact volume");
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
        const auto revolution_segment_ids = revolution_sketch.add_rectangle(
            10.0, 5.0, 20.0, 8.0);
        std::vector<std::string> revolution_point_ids;
        for (const auto& point : revolution_sketch.points) {
            revolution_point_ids.push_back(point.id);
        }
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
        std::set<std::string> full_revolution_faces;
        for (const auto& reference : revolution_results.front().mesh
                 .original_references.triangle_references) {
            full_revolution_faces.insert(reference.semantic_key);
        }
        std::set<std::string> full_revolution_edges;
        for (const auto& edge : revolution_results.front().mesh
                 .original_references.edges) {
            full_revolution_edges.insert(edge.reference.semantic_key);
        }
        require(std::ranges::all_of(revolution_segment_ids, [&](const auto& id) {
                    return full_revolution_faces.contains("generated:" + id);
                }) &&
                std::ranges::all_of(revolution_point_ids, [&](const auto& id) {
                    return full_revolution_edges.contains("generated:" + id);
                }),
                "Revolution faces/edges lost their Sketch curve/point parents");
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
        require(partial_revolution_faces.contains("start") &&
                    partial_revolution_faces.contains("end"),
                "Partial Revolution lost its start or end profile face");
        std::set<std::string> partial_revolution_edges;
        for (const auto& edge : half_revolution_results.front().mesh
                 .original_references.edges) {
            partial_revolution_edges.insert(edge.reference.semantic_key);
        }
        std::set<std::string> partial_revolution_vertices;
        for (const auto& vertex : half_revolution_results.front().mesh
                 .original_references.points) {
            partial_revolution_vertices.insert(vertex.reference.semantic_key);
        }
        require(std::ranges::all_of(revolution_segment_ids, [&](const auto& id) {
                    return partial_revolution_edges.contains("start:" + id) &&
                        partial_revolution_edges.contains("end:" + id);
                }) &&
                std::ranges::all_of(revolution_point_ids, [&](const auto& id) {
                    return partial_revolution_edges.contains("generated:" + id) &&
                        partial_revolution_vertices.contains("start:" + id) &&
                        partial_revolution_vertices.contains("end:" + id);
                }),
                "Partial Revolution topology lost curve/point ancestry");
        const auto revolution_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-revolution-contract.prtz";
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
        zima::document::DocumentSession revolution_session(
            half_revolution_document, half_revolution_results);
        auto edited_revolution = revolution_session.document();
        edited_revolution.history.front().revolution.angle_degrees = 225.0;
        const auto edited_revolution_results = kernel.evaluate_history(
            edited_revolution.kernel_operations());
        revolution_session.commit(
            std::move(edited_revolution), edited_revolution_results);
        require(revolution_session.document().history.front()
                        .revolution.angle_degrees == 225.0 &&
                    revolution_session.undo() &&
                    revolution_session.document().history.front()
                        .revolution.angle_degrees == 180.0 &&
                    revolution_session.calculated_boundaries().front()
                        .source_fingerprint ==
                        half_revolution_results.front().source_fingerprint &&
                    revolution_session.redo() &&
                    revolution_session.document().history.front()
                        .revolution.angle_degrees == 225.0,
                "Revolution edit did not behave as one Undo/Redo revision");
        auto source_edited_revolution = revolution_session.document();
        for (auto& point : source_edited_revolution.sketches.front().points) {
            if (point.x > 15.0) point.x += 2.0;
        }
        const auto source_edited_revolution_results = kernel.evaluate_history(
            source_edited_revolution.kernel_operations());
        require(source_edited_revolution_results.front().volume >
                    revolution_session.calculated_boundaries().front().volume &&
                std::ranges::all_of(revolution_point_ids, [&](const auto& id) {
                    return std::ranges::any_of(
                        source_edited_revolution_results.front().mesh
                            .original_references.edges,
                        [&](const auto& edge) {
                            return edge.reference.semantic_key ==
                                "generated:" + id;
                        });
                }),
                "Source Sketch edit broke Revolution point ancestry");
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

        auto long_history = zima::document::PartDocument::create_default();
        for (int index = 0; index < 25; ++index) {
            auto feature = zima::document::PartDocument::create_box_container();
            feature.box = {10.0, 10.0, 10.0};
            feature.placement.x = index * 8.0;
            long_history.history.push_back(std::move(feature));
        }
        const auto long_boundaries =
            kernel.evaluate_history(long_history.kernel_operations());
        require(long_boundaries.size() == long_history.history.size() &&
                    std::abs(long_boundaries.back().volume - 20'200.0) < 1.0e-6 &&
                    long_boundaries.back().source_fingerprint ==
                        zima::kernel::history_fingerprint(
                            long_history.kernel_operations(), 25),
                "Long Part history lost a boundary, volume, or fingerprint");
        zima::document::DocumentSession rollback_session(
            long_history, long_boundaries);
        const auto first_rollback = rollback_session.rollback_boundary(
            long_history.history.front().id);
        const auto middle_rollback = rollback_session.rollback_boundary(
            long_history.history[12].id);
        require(first_rollback && first_rollback->history_index == 0 &&
                    !first_rollback->input_body && middle_rollback &&
                    middle_rollback->history_index == 12 &&
                    middle_rollback->input_body &&
                    middle_rollback->input_body->source_fingerprint ==
                        long_boundaries[11].source_fingerprint,
                "History rollback did not return the exact persisted input boundary");
        require(!rollback_session.rollback_boundary("missing-container"),
                "History rollback accepted a missing container");
        zima::document::DocumentSession incomplete_rollback(long_history);
        require(!incomplete_rollback.rollback_boundary(
                    long_history.history[12].id),
                "History rollback reconstructed a missing calculated input implicitly");
        std::cout << "C++ document and OCCT contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

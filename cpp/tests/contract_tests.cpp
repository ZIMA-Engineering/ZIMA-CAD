#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <STEPControl_Writer.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string add_revolution_axis(
    zima::sketcher::Sketch& sketch, bool vertical = false) {
    const auto id = vertical
        ? sketch.add_segment(0.0, -100.0, 0.0, 100.0, true)
        : sketch.add_segment(-100.0, 0.0, 100.0, 0.0, true);
    sketch.set_segment_centerline(id, true);
    return id;
}

double viewer_triangle_area(const zima::kernel::ViewerMesh& mesh) {
    double area = 0.0;
    for (std::size_t index = 0; index + 2 < mesh.triangles.size(); index += 3) {
        const auto& first = mesh.vertices.at(mesh.triangles[index]);
        const auto& second = mesh.vertices.at(mesh.triangles[index + 1]);
        const auto& third = mesh.vertices.at(mesh.triangles[index + 2]);
        const std::array<double, 3> first_edge{
            second.x - first.x, second.y - first.y, second.z - first.z};
        const std::array<double, 3> second_edge{
            third.x - first.x, third.y - first.y, third.z - first.z};
        const std::array<double, 3> cross{
            first_edge[1] * second_edge[2] - first_edge[2] * second_edge[1],
            first_edge[2] * second_edge[0] - first_edge[0] * second_edge[2],
            first_edge[0] * second_edge[1] - first_edge[1] * second_edge[0]};
        area += 0.5 * std::sqrt(
            cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
    }
    return area;
}

}  // namespace

int main() {
    try {
        std::set<std::string> generated_stable_ids;
        std::string previous_stable_id;
        for (int index = 0; index < 4096; ++index) {
            const auto id = zima::kernel::make_stable_id();
            require(id.size() == 32 && id[12] == '7' &&
                        std::string("89ab").find(id[16]) != std::string::npos &&
                        std::ranges::all_of(id, [](char value) {
                            return (value >= '0' && value <= '9') ||
                                (value >= 'a' && value <= 'f');
                        }),
                    "Stable ID is not a compact RFC UUIDv7");
            require(previous_stable_id.empty() || previous_stable_id < id,
                    "Stable UUID generator lost monotonic creation order");
            previous_stable_id = id;
            generated_stable_ids.insert(id);
        }
        require(generated_stable_ids.size() == 4096,
                "Stable UUID generator produced a collision");
        const auto start_part_template =
            zima::document::PartDocument::load(
                std::filesystem::current_path() /
                "config/templates/start_part.prtz");
        require(start_part_template.document_id == "template-start-part" &&
                    start_part_template.history.empty() &&
                    start_part_template.sketches.empty() &&
                    start_part_template.constructions.empty() &&
                    start_part_template.physical_parameters.at("MATERIAL_NAME") ==
                        "S235JR" &&
                    start_part_template.physical_parameters.contains("YOUNG_MODULUS") &&
                    start_part_template.physical_parameters.contains(
                        "STRESS_LIMIT_FOR_TENSION"),
                "Start Part template is stale or has no complete S235JR assignment");
        const auto fixture_dir = std::filesystem::current_path() /
            "tests/fixtures/cross_language";
        std::vector<zima::kernel::BodyResult> fixture_boundaries;
        const auto fixture_part = zima::document::PartDocument::load(
            fixture_dir / "part.prtz", &fixture_boundaries);
        require(fixture_part.document_id == "part-fixture-001" &&
                    fixture_part.name == "Fixture Part" &&
                    fixture_boundaries.empty(),
                "Python Part fixture identity or cache boundary is invalid");
        zima::kernel::OcctKernel kernel;
        const auto step_path = std::filesystem::temp_directory_path() /
            "zima-cad-imported-step-contract.step";
        STEPControl_Writer step_writer;
        require(step_writer.Transfer(BRepPrimAPI_MakeBox(12.0, 8.0, 5.0).Shape(),
                    STEPControl_AsIs) == IFSelect_RetDone &&
                    step_writer.Write(step_path.string().c_str()) == IFSelect_RetDone,
                "STEP contract fixture could not be written");
        auto step_document = zima::document::PartDocument::create_default();
        auto step_container =
            zima::document::PartDocument::create_imported_step_container(step_path);
        const auto explicitly_imported_step = kernel.import_step_components({{
            step_path.generic_string(), {}, {}, {}, step_container.id, {}}});
        require(explicitly_imported_step.size() == 1 &&
                    std::abs(explicitly_imported_step.back().volume - 480.0) < 1.0e-6 &&
                    !explicitly_imported_step.back().kernel_shape.empty(),
                "Explicit STEP import did not calculate the expected frozen box");
        require(!explicitly_imported_step.back().mesh.edges.empty() &&
                    std::ranges::all_of(
                        explicitly_imported_step.back().mesh.edges,
                        [&](const auto& edge) {
                            return !edge.reference.valid() &&
                                edge.display_owner_id == step_container.id;
                        }),
                "Imported STEP display wire cannot be recoloured without "
                "becoming persistent topology");
        const auto& imported_step_topology =
            explicitly_imported_step.back().imported_step_topology;
        std::set<std::string> imported_step_face_ids;
        std::set<std::string> imported_step_edge_ids;
        std::set<std::string> imported_step_vertex_ids;
        for (const auto& identity : imported_step_topology) {
            require(!identity.semantic_key.empty() &&
                        !identity.shape_locator.empty(),
                    "Imported STEP topology identity is incomplete");
            auto* identities = &imported_step_face_ids;
            auto expected_prefix = std::string{"step:face:#"};
            if (identity.kind == zima::kernel::StepRequest::TopologyIdentity::Kind::Edge) {
                identities = &imported_step_edge_ids;
                expected_prefix = "step:edge:#";
            } else if (identity.kind ==
                    zima::kernel::StepRequest::TopologyIdentity::Kind::Vertex) {
                identities = &imported_step_vertex_ids;
                expected_prefix = "step:vertex:#";
            }
            require(identity.semantic_key.starts_with(expected_prefix),
                    "Imported STEP identity was not defined by a source STEP entity");
            require(identities->insert(identity.semantic_key).second,
                    "Imported STEP contains a duplicate source topology identity");
        }
        if (imported_step_face_ids.size() != 6 ||
            imported_step_edge_ids.size() != 12 ||
            imported_step_vertex_ids.size() != 8) {
            std::cerr << "STEP topology identities: faces="
                      << imported_step_face_ids.size() << " edges="
                      << imported_step_edge_ids.size() << " vertices="
                      << imported_step_vertex_ids.size() << '\n';
        }
        require(imported_step_face_ids.size() == 6 &&
                    imported_step_edge_ids.size() == 12 &&
                    imported_step_vertex_ids.size() == 8,
                "Imported STEP box did not preserve its source face/edge/vertex identities");

        const auto& imported_step_references =
            explicitly_imported_step.back().mesh.original_references;
        require(!imported_step_references.triangle_references.empty() &&
                    !imported_step_references.edges.empty() &&
                    !imported_step_references.points.empty(),
                "Imported STEP source topology is missing viewer reference geometry");
        for (const auto& reference :
             imported_step_references.triangle_references) {
            require(reference.owner_id == step_container.id &&
                        imported_step_face_ids.contains(reference.semantic_key) &&
                        !reference.semantic_key.starts_with("face:"),
                    "Imported STEP face reference used a synthetic traversal identity");
        }
        for (const auto& edge : imported_step_references.edges) {
            require(edge.reference.owner_id == step_container.id &&
                        imported_step_edge_ids.contains(
                            edge.reference.semantic_key) &&
                        !edge.reference.semantic_key.starts_with("edge:"),
                    "Imported STEP edge reference used a synthetic traversal identity");
        }
        for (const auto& point : imported_step_references.points) {
            require(point.reference.owner_id == step_container.id &&
                        imported_step_vertex_ids.contains(
                            point.reference.semantic_key) &&
                        !point.reference.semantic_key.starts_with("vertex:"),
                    "Imported STEP vertex reference used a synthetic traversal identity");
        }
        step_container.imported_step.frozen_brep =
            std::make_shared<const std::string>(
                explicitly_imported_step.back().kernel_shape);
        step_container.imported_step.topology = imported_step_topology;
        step_document.history.push_back(std::move(step_container));
        std::filesystem::remove(step_path);
        const auto step_boundaries = kernel.evaluate_history(
            step_document.kernel_operations());
        require(step_boundaries.size() == 1 &&
                    std::abs(step_boundaries.back().volume - 480.0) < 1.0e-6 &&
                    !step_boundaries.back().mesh.original_references
                        .triangle_references.empty(),
                "Frozen imported STEP did not restore source topology without its file");
        const auto step_document_path = std::filesystem::temp_directory_path() /
            "zima-cad-imported-step-contract.prtz";
        step_document.save(step_document_path, step_boundaries);
        std::vector<zima::kernel::BodyResult> loaded_step_boundaries;
        const auto loaded_step_document = zima::document::PartDocument::load(
            step_document_path, &loaded_step_boundaries);
        std::filesystem::remove(step_document_path);
        require(loaded_step_boundaries.size() == 1 &&
                    !loaded_step_boundaries.back().mesh.edges.empty() &&
                    std::ranges::all_of(
                        loaded_step_boundaries.back().mesh.edges,
                        [&](const auto& edge) {
                            return edge.reference.valid() &&
                                edge.reference.owner_id ==
                                    loaded_step_document.history.front().id &&
                                edge.display_owner_id ==
                                    loaded_step_document.history.front().id;
                        }),
                "Saved STEP display wire lost its stable source owner");
        const auto recalculated_frozen_step = kernel.evaluate_history(
            loaded_step_document.kernel_operations());
        require(loaded_step_document.history.size() == 1 &&
                    loaded_step_document.history.front().feature_kind ==
                        zima::document::FeatureKind::ImportedStep &&
                    loaded_step_document.history.front().imported_step.topology ==
                        imported_step_topology &&
                    loaded_step_boundaries.size() == 1 &&
                    recalculated_frozen_step.size() == 1 &&
                    std::abs(recalculated_frozen_step.back().volume - 480.0) < 1.0e-6,
                "Frozen imported STEP did not survive source-free Part recalculation");
        const auto& recalculated_step_references =
            recalculated_frozen_step.back().mesh.original_references;
        require(!recalculated_step_references.triangle_references.empty() &&
                    !recalculated_step_references.edges.empty() &&
                    !recalculated_step_references.points.empty(),
                "Frozen imported STEP did not restore persisted source references");
        for (const auto& reference :
             recalculated_step_references.triangle_references) {
            require(reference.owner_id == step_document.history.front().id &&
                        imported_step_face_ids.contains(reference.semantic_key),
                    "Frozen imported STEP restored a different face identity");
        }
        for (const auto& edge : recalculated_step_references.edges) {
            require(edge.reference.owner_id == step_document.history.front().id &&
                        imported_step_edge_ids.contains(
                            edge.reference.semantic_key),
                    "Frozen imported STEP restored a different edge identity");
        }
        for (const auto& point : recalculated_step_references.points) {
            require(point.reference.owner_id == step_document.history.front().id &&
                        imported_step_vertex_ids.contains(
                            point.reference.semantic_key),
                    "Frozen imported STEP restored a different vertex identity");
        }

        // An imported STEP need not be a closed solid.  Keep the original
        // open sheet as one immutable Part container and prove that a later
        // explicit history Cut operates on that sheet itself.  The expected
        // area also guards against accidentally displaying/returning both the
        // uncut source sheet and the cut result as ordinary result geometry.
        const auto sheet_step_path = std::filesystem::temp_directory_path() /
            "zima-cad-imported-step-sheet-cut-contract.step";
        BRepBuilderAPI_MakePolygon sheet_boundary;
        sheet_boundary.Add(gp_Pnt(0.0, 0.0, 0.0));
        sheet_boundary.Add(gp_Pnt(20.0, 0.0, 0.0));
        sheet_boundary.Add(gp_Pnt(20.0, 10.0, 0.0));
        sheet_boundary.Add(gp_Pnt(0.0, 10.0, 0.0));
        sheet_boundary.Close();
        BRepBuilderAPI_MakeFace sheet_face(sheet_boundary.Wire());
        require(sheet_boundary.IsDone() && sheet_face.IsDone(),
                "Open STEP sheet fixture could not be constructed");
        STEPControl_Writer sheet_step_writer;
        require(sheet_step_writer.Transfer(sheet_face.Face(), STEPControl_AsIs) ==
                        IFSelect_RetDone &&
                    sheet_step_writer.Write(sheet_step_path.string().c_str()) ==
                        IFSelect_RetDone,
                "Open STEP sheet fixture could not be written");

        auto sheet_document = zima::document::PartDocument::create_default();
        auto sheet_container =
            zima::document::PartDocument::create_imported_step_container(
                sheet_step_path);
        const auto imported_sheet = kernel.import_step_components({{
            sheet_step_path.generic_string(), {}, {}, {}, sheet_container.id, {}}});
        require(imported_sheet.size() == 1 &&
                    std::abs(imported_sheet.front().volume) < 1.0e-9 &&
                    std::abs(imported_sheet.front().surface_area - 200.0) < 1.0e-6 &&
                    !imported_sheet.front().kernel_shape.empty(),
                "Explicit STEP import did not preserve the open sheet");
        sheet_container.imported_step.frozen_brep =
            std::make_shared<const std::string>(imported_sheet.front().kernel_shape);
        sheet_container.imported_step.topology =
            imported_sheet.front().imported_step_topology;
        sheet_document.history.push_back(std::move(sheet_container));

        auto sheet_cutter =
            zima::document::PartDocument::create_box_container();
        sheet_cutter.name = "Sheet Cut";
        sheet_cutter.combine_mode = zima::document::CombineMode::Subtract;
        sheet_cutter.box = {4.0, 4.0, 2.0};
        sheet_cutter.placement.x = 5.0;
        sheet_cutter.placement.y = 5.0;
        sheet_document.history.push_back(std::move(sheet_cutter));
        const auto cut_sheet_boundaries = kernel.evaluate_history(
            sheet_document.kernel_operations());
        std::filesystem::remove(sheet_step_path);
        require(cut_sheet_boundaries.size() == 2 &&
                    std::abs(cut_sheet_boundaries.back().volume) < 1.0e-9 &&
                    std::abs(cut_sheet_boundaries.back().surface_area - 184.0) <
                        1.0e-6 &&
                    std::abs(viewer_triangle_area(
                                 cut_sheet_boundaries.back().mesh) - 184.0) <
                        1.0e-6,
                "Cutting an imported open STEP sheet duplicated or failed to cut the result");

        // Edit/regenerate/reopen: the Python-produced fixture starts with an
        // empty history. Append a native feature, calculate it, save and
        // reload the edited document to prove the open/load fixture also
        // survives an explicit edit-regenerate-reopen cycle, not merely an
        // initial load.
        auto edited_fixture_part = fixture_part;
        auto fixture_box = zima::document::PartDocument::create_box_container();
        fixture_box.id = "fixture-edit-box-001";
        fixture_box.feature_id = "fixture-edit-box-001:feature";
        fixture_box.feature_parent_id = fixture_box.id;
        fixture_box.container_origin =
            zima::document::create_container_origin(fixture_box.id);
        fixture_box.name = "Fixture Edit Box";
        fixture_box.box = {30.0, 15.0, 8.0};
        edited_fixture_part.history.push_back(fixture_box);
        edited_fixture_part.insert_history_entry(
            zima::document::PartHistoryKind::Feature, fixture_box.id);
        const auto fixture_edit_boundaries = kernel.evaluate_history(
            edited_fixture_part.kernel_operations());
        require(fixture_edit_boundaries.size() == 1 &&
                    std::abs(fixture_edit_boundaries.back().volume - 3600.0) < 1.0e-6,
                "Editing the Python fixture history did not calculate the expected box");
        const auto fixture_edit_path = std::filesystem::temp_directory_path() /
            "zima-cad-fixture-edit-regenerate-reopen-contract.prtz";
        edited_fixture_part.save(fixture_edit_path, fixture_edit_boundaries);
        std::vector<zima::kernel::BodyResult> reopened_fixture_boundaries;
        const auto reopened_fixture_part = zima::document::PartDocument::load(
            fixture_edit_path, &reopened_fixture_boundaries);
        std::filesystem::remove(fixture_edit_path);
        require(reopened_fixture_part.document_id == "part-fixture-001" &&
                    reopened_fixture_part.history.size() == 1 &&
                    reopened_fixture_part.history.front().id == "fixture-edit-box-001" &&
                    reopened_fixture_part.history.front().box.length == 30.0 &&
                    reopened_fixture_part.history.front().box.width == 15.0 &&
                    reopened_fixture_part.history.front().box.height == 8.0 &&
                    reopened_fixture_boundaries.size() == 1 &&
                    std::abs(reopened_fixture_boundaries.back().volume - 3600.0) < 1.0e-6,
                "Edited Python fixture did not survive regenerate/save/reopen");

        const auto body = kernel.make_box({100.0, 80.0, 50.0});
        require(std::abs(body.volume - 400000.0) < 1e-6, "Incorrect box volume");
        require(std::abs(body.surface_area - 34000.0) < 1e-6,
                "Incorrect box surface area");
        require(!body.mesh.vertices.empty(), "Viewer mesh is empty");
        require(!body.kernel_shape.empty(),
                "Calculated body did not persist its kernel snapshot");
        auto centered_box_document = zima::document::PartDocument::create_default();
        centered_box_document.history.push_back(
            zima::document::PartDocument::create_box_container());
        const auto centered_box_result = kernel.evaluate_history(
            centered_box_document.kernel_operations()).front();
        const auto bounds_on = [&](auto coordinate) {
            const auto bounds = std::minmax_element(
                centered_box_result.mesh.vertices.begin(),
                centered_box_result.mesh.vertices.end(),
                [&](const auto& left, const auto& right) {
                    return coordinate(left) < coordinate(right);
                });
            return std::pair{coordinate(*bounds.first), coordinate(*bounds.second)};
        };
        const auto [box_x_min, box_x_max] = bounds_on(
            [](const auto& point) { return point.x; });
        const auto [box_y_min, box_y_max] = bounds_on(
            [](const auto& point) { return point.y; });
        const auto [box_z_min, box_z_max] = bounds_on(
            [](const auto& point) { return point.z; });
        require(std::abs(box_x_min + 50.0) < 1.0e-7 &&
                    std::abs(box_x_max - 50.0) < 1.0e-7 &&
                    std::abs(box_y_min + 40.0) < 1.0e-7 &&
                    std::abs(box_y_max - 40.0) < 1.0e-7 &&
                    std::abs(box_z_min + 25.0) < 1.0e-7 &&
                    std::abs(box_z_max - 25.0) < 1.0e-7,
                "Part Box is not symmetric about its local origin");
        centered_box_document.history.front().placement.x = 7.0;
        centered_box_document.history.front().placement.y = -4.0;
        centered_box_document.history.front().placement.z = 13.0;
        centered_box_document.history.front().placement.rotation_x = 21.0;
        centered_box_document.history.front().placement.rotation_y = 32.0;
        centered_box_document.history.front().placement.rotation_z = 43.0;
        const auto rotated_centered_box = kernel.evaluate_history(
            centered_box_document.kernel_operations()).front();
        zima::kernel::Vec3 vertex_center;
        for (const auto& point :
             rotated_centered_box.mesh.original_references.points) {
            vertex_center.x += point.position.x;
            vertex_center.y += point.position.y;
            vertex_center.z += point.position.z;
        }
        const double vertex_count = static_cast<double>(
            rotated_centered_box.mesh.original_references.points.size());
        require(vertex_count == 8.0 &&
                    std::abs(vertex_center.x / vertex_count - 7.0) < 1.0e-7 &&
                    std::abs(vertex_center.y / vertex_count + 4.0) < 1.0e-7 &&
                    std::abs(vertex_center.z / vertex_count - 13.0) < 1.0e-7,
                "Rotated Part Box local origin is not its geometric center");
        zima::kernel::BoxRequest cutter_request{20.0, 20.0, 20.0};
        cutter_request.translation = {50.0, 0.0, 0.0};
        const auto cutter = kernel.make_box(cutter_request);
        const auto assembly_cut = kernel.subtract_bodies(
            body, cutter, {50.0, 0.0, 0.0}, {});
        require(std::abs(assembly_cut.volume - 392000.0) < 1.0e-6 &&
                    !assembly_cut.kernel_shape.empty() &&
                    assembly_cut.mesh.original_references.triangle_references ==
                        body.mesh.original_references.triangle_references &&
                    assembly_cut.mesh.original_references.edges.size() ==
                        body.mesh.original_references.edges.size(),
                "Assembly boolean did not cut the placed body or preserve original references");
        const auto exported_step = std::filesystem::temp_directory_path() /
            "zima-cad-kernel-export-contract.step";
        const auto exported_stl = std::filesystem::temp_directory_path() /
            "zima-cad-kernel-export-contract.stl";
        kernel.export_step({{assembly_cut, {25.0, 0.0, 0.0}, {}}},
            exported_step.string());
        kernel.export_stl({{assembly_cut, {}, {}}}, exported_stl.string());
        require(std::filesystem::file_size(exported_step) > 100 &&
                    std::filesystem::file_size(exported_stl) > 100,
                "STEP/STL export did not write calculated solid data");
        std::filesystem::remove(exported_step);
        std::filesystem::remove(exported_stl);
        require(body.mesh.triangles.size() % 3 == 0, "Invalid triangle indices");
        require(body.mesh.original_references.triangle_references.size() ==
                    body.mesh.original_references.triangles.size() / 3,
                "Viewer triangles and face references are not aligned");
        require(std::all_of(body.mesh.triangle_references.begin(),
                    body.mesh.triangle_references.end(),
                    [](const auto& reference) {
                        return reference.valid() && reference.owner_id == "box";
                    }),
                "Calculated body fragments lost stable source-face references");
        require(std::all_of(body.mesh.edges.begin(), body.mesh.edges.end(),
                    [](const auto& edge) {
                        return edge.reference.valid() &&
                            edge.reference.owner_id == "box";
                    }),
                "Calculated body edges lost stable input-body references");
        require(!body.mesh.points.empty() &&
                    std::all_of(body.mesh.points.begin(), body.mesh.points.end(),
                        [](const auto& point) {
                            return point.reference.valid() &&
                                !point.always_visible;
                        }),
                "Calculated body vertices became permanent visible markers");
        std::set<std::string> box_face_keys;
        for (const auto& reference : body.mesh.original_references.triangle_references) {
            require(reference.owner_id == "box" && reference.valid(),
                    "Primitive triangle lost its persisted owner");
            box_face_keys.insert(reference.semantic_key);
        }
        require(box_face_keys == std::set<std::string>{
                    "x_min", "x_max", "y_min", "y_max", "z_min", "z_max"},
                "Primitive semantic face keys are incomplete");
        std::set<std::string> display_box_face_keys;
        for (const auto& reference : body.mesh.triangle_references) {
            display_box_face_keys.insert(reference.semantic_key);
        }
        require(display_box_face_keys == box_face_keys,
                "Visible Box fragments changed stable semantic face identities");

        const zima::kernel::FaceReference shell_opening{
            "box", "z_max", {}};
        const auto shell_boundaries = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"shell", zima::kernel::ShellRequest{{shell_opening}, 2.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(shell_boundaries.size() == 2 &&
                    shell_boundaries.back().volume > 0.0 &&
                    shell_boundaries.back().volume < body.volume,
                "Inward Shell did not remove material from the input solid");
        const auto shell_bounds = [&](auto coordinate) {
            return std::minmax_element(
                shell_boundaries.back().mesh.vertices.begin(),
                shell_boundaries.back().mesh.vertices.end(),
                [&](const auto& left, const auto& right) {
                    return coordinate(left) < coordinate(right);
                });
        };
        const auto shell_x = shell_bounds(
            [](const auto& point) { return point.x; });
        const auto shell_y = shell_bounds(
            [](const auto& point) { return point.y; });
        const auto shell_z = shell_bounds(
            [](const auto& point) { return point.z; });
        require(std::abs(shell_x.first->x) < 1.0e-7 &&
                    std::abs(shell_x.second->x - 100.0) < 1.0e-7 &&
                    std::abs(shell_y.first->y) < 1.0e-7 &&
                    std::abs(shell_y.second->y - 80.0) < 1.0e-7 &&
                    std::abs(shell_z.first->z) < 1.0e-7 &&
                    std::abs(shell_z.second->z - 50.0) < 1.0e-7,
                "Inward Shell changed the exterior dimensions");
        std::set<std::pair<std::string, std::string>> shell_face_ids;
        std::set<std::string> shell_face_owners;
        for (const auto& reference :
             shell_boundaries.back().mesh.triangle_references) {
            require(reference.valid(),
                "Shell left an anonymous result face");
            shell_face_ids.emplace(reference.owner_id, reference.semantic_key);
            shell_face_owners.insert(reference.owner_id);
        }
        require(shell_face_owners.contains("box") &&
                    shell_face_owners.contains("shell") &&
                    shell_face_ids.size() > box_face_keys.size(),
                "Shell did not preserve exterior face identity and create "
                "stable owned inner faces");
        std::set<std::pair<std::string, std::string>>
            shell_owned_display_face_ids;
        for (const auto& identity : shell_face_ids) {
            if (identity.first == "shell") {
                shell_owned_display_face_ids.insert(identity);
            }
        }
        std::set<std::pair<std::string, std::string>>
            shell_owned_reference_face_ids;
        for (const auto& reference : shell_boundaries.back()
                 .mesh.original_references.triangle_references) {
            if (reference.owner_id == "shell") {
                shell_owned_reference_face_ids.emplace(
                    reference.owner_id, reference.semantic_key);
            }
        }
        require(!shell_owned_display_face_ids.empty() &&
                    shell_owned_reference_face_ids ==
                        shell_owned_display_face_ids,
                "Shell-owned display faces were not persisted as stable "
                "placement reference geometry");
        const auto& shell_reference_identity =
            *shell_owned_reference_face_ids.begin();
        zima::document::ConstructionReference shell_face_reference{
            {}, shell_reference_identity.first,
            shell_reference_identity.second, 0.0, true};
        require(zima::document::point_constraint_remaining_dof(
                    {shell_face_reference}, shell_boundaries.back()
                        .mesh.original_references) < 3,
                "A persisted inner Shell face did not constrain placement");
        std::set<std::pair<std::string, std::string>> shell_edge_ids;
        for (const auto& edge : shell_boundaries.back().mesh.edges) {
            require(edge.reference.valid(),
                "Shell left an anonymous result edge");
            require(shell_edge_ids.emplace(edge.reference.owner_id,
                        edge.reference.semantic_key).second,
                "Shell reused one stable edge identity");
        }
        std::set<std::pair<std::string, std::string>> shell_vertex_ids;
        for (const auto& point : shell_boundaries.back().mesh.points) {
            require(point.reference.valid(),
                "Shell left an anonymous result vertex");
            require(shell_vertex_ids.emplace(point.reference.owner_id,
                        point.reference.semantic_key).second,
                "Shell reused one stable vertex identity");
        }
        const auto closed_shell_boundaries = kernel.evaluate_history({
            {"closed-shell-box", zima::kernel::BoxRequest{10.0, 10.0, 10.0},
             zima::kernel::BooleanOperation::Add},
            {"closed-shell", zima::kernel::ShellRequest{{}, 1.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(closed_shell_boundaries.size() == 2 &&
                    std::abs(closed_shell_boundaries.back().volume - 488.0) <
                        1.0e-6,
                "Closed Shell without opening faces did not create a hollow body");
        const auto adjacent_opening_shell = kernel.evaluate_history({
            {"adjacent-opening-box",
             zima::kernel::BoxRequest{10.0, 10.0, 10.0},
             zima::kernel::BooleanOperation::Add},
            {"adjacent-opening-shell",
             zima::kernel::ShellRequest{{
                 {"adjacent-opening-box", "z_max", {}},
                 {"adjacent-opening-box", "x_max", {}}}, 1.0},
             zima::kernel::BooleanOperation::Add},
        });
        const auto adjacent_opening_error =
            "Adjacent Shell openings retained a wall at their common edge; "
            "volume=" + std::to_string(
                adjacent_opening_shell.back().volume);
        require(adjacent_opening_shell.size() == 2 &&
                    std::abs(adjacent_opening_shell.back().volume - 352.0) <
                        1.0e-6,
                adjacent_opening_error.c_str());
        std::size_t adjacent_shell_max_key{};
        for (const auto& reference :
             adjacent_opening_shell.back().mesh.triangle_references) {
            adjacent_shell_max_key = std::max(
                adjacent_shell_max_key, reference.semantic_key.size());
        }
        for (const auto& edge : adjacent_opening_shell.back().mesh.edges) {
            adjacent_shell_max_key = std::max(
                adjacent_shell_max_key, edge.reference.semantic_key.size());
        }
        for (const auto& point : adjacent_opening_shell.back().mesh.points) {
            adjacent_shell_max_key = std::max(
                adjacent_shell_max_key, point.reference.semantic_key.size());
        }
        require(adjacent_opening_shell.back().mesh.triangles.size() < 100000 &&
                    adjacent_shell_max_key < 4096,
                "Adjacent Shell openings produced a pathological viewer packet");
        const std::string shell_fillet_box_id = "shell-two-fillet-box";
        std::vector<zima::kernel::HistoryOperation> shell_after_fillets{
            {shell_fillet_box_id,
             zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"shell-first-fillet",
             zima::kernel::FilletRequest{{
                 {shell_fillet_box_id,
                  "edge:x_max:y_min:z_max--x_max:y_min:z_min", {}},
                 {shell_fillet_box_id,
                  "edge:x_min:y_min:z_max--x_min:y_min:z_min", {}}}, 5.0},
             zima::kernel::BooleanOperation::Add},
            {"shell-second-fillet",
             zima::kernel::FilletRequest{{
                 {shell_fillet_box_id,
                  "edge:x_max:y_max:z_min--x_max:y_min:z_min", {}}}, 5.0},
             zima::kernel::BooleanOperation::Add},
        };
        const auto two_fillet_body =
            kernel.evaluate_history(shell_after_fillets);
        require(two_fillet_body.size() == 3 &&
                    two_fillet_body.back().volume > 0.0 &&
                    two_fillet_body.back().volume < 400000.0,
                "Sequential Fillet setup for Shell regression failed");
        std::size_t treated_input_max_key{};
        for (const auto& reference :
             two_fillet_body.back().mesh.triangle_references) {
            treated_input_max_key = std::max(
                treated_input_max_key, reference.semantic_key.size());
        }
        for (const auto& edge : two_fillet_body.back().mesh.edges) {
            treated_input_max_key = std::max(
                treated_input_max_key, edge.reference.semantic_key.size());
        }
        shell_after_fillets.push_back({"closed-shell-after-two-fillets",
            zima::kernel::ShellRequest{{}, 1.0},
            zima::kernel::BooleanOperation::Add});
        const auto closed_after_two_fillets =
            kernel.evaluate_history(shell_after_fillets);
        require(closed_after_two_fillets.size() == 4 &&
                    closed_after_two_fillets.back().volume > 0.0 &&
                    closed_after_two_fillets.back().volume <
                        closed_after_two_fillets[2].volume,
                "Closed Shell failed after two Fillet features");
        std::size_t treated_closed_shell_max_key{};
        for (const auto& reference :
             closed_after_two_fillets.back().mesh.triangle_references) {
            treated_closed_shell_max_key = std::max(
                treated_closed_shell_max_key,
                reference.semantic_key.size());
        }
        for (const auto& edge :
             closed_after_two_fillets.back().mesh.edges) {
            treated_closed_shell_max_key = std::max(
                treated_closed_shell_max_key,
                edge.reference.semantic_key.size());
        }
        shell_after_fillets.back().primitive = zima::kernel::ShellRequest{{
            {shell_fillet_box_id, "z_max", {}}}, 1.0};
        const auto single_open_after_two_fillets =
            kernel.evaluate_history(shell_after_fillets);
        require(single_open_after_two_fillets.size() == 4 &&
                    single_open_after_two_fillets.back().volume > 0.0 &&
                    single_open_after_two_fillets.back().volume <
                        single_open_after_two_fillets[2].volume,
                "Single-face Shell failed after two Fillet features");
        shell_after_fillets.back().primitive = zima::kernel::ShellRequest{{
            {shell_fillet_box_id, "z_max", {}},
            {shell_fillet_box_id, "x_max", {}}}, 1.0};
        const auto open_after_two_fillets =
            kernel.evaluate_history(shell_after_fillets);
        require(open_after_two_fillets.size() == 4 &&
                    open_after_two_fillets.back().volume > 0.0 &&
                    open_after_two_fillets.back().volume <
                        open_after_two_fillets[2].volume,
                "Open Shell failed after two Fillet features");
        std::size_t treated_shell_max_face_key{};
        for (const auto& reference :
             open_after_two_fillets.back().mesh.triangle_references) {
            treated_shell_max_face_key = std::max(
                treated_shell_max_face_key, reference.semantic_key.size());
        }
        std::size_t treated_shell_max_edge_key{};
        for (const auto& edge : open_after_two_fillets.back().mesh.edges) {
            treated_shell_max_edge_key = std::max(
                treated_shell_max_edge_key,
                edge.reference.semantic_key.size());
        }
        const auto treated_shell_max_key = std::max(
            treated_shell_max_face_key, treated_shell_max_edge_key);
        const auto treated_shell_packet_error =
            "Shell after two Fillets produced pathological persisted or "
            "viewer data; kernel_shape=" +
            std::to_string(
                open_after_two_fillets.back().kernel_shape.size()) +
            ", triangle_indices=" +
            std::to_string(
                open_after_two_fillets.back().mesh.triangles.size()) +
            ", input_max_key=" +
            std::to_string(treated_input_max_key) +
            ", closed_shell_max_key=" +
            std::to_string(treated_closed_shell_max_key) +
            ", max_face_key=" +
            std::to_string(treated_shell_max_face_key) +
            ", max_edge_key=" +
            std::to_string(treated_shell_max_edge_key);
        require(open_after_two_fillets.back().kernel_shape.size() <
                    1U * 1024U * 1024U &&
                    open_after_two_fillets.back().mesh.triangles.size() <
                        100000 &&
                    treated_shell_max_key < 4096,
                treated_shell_packet_error.c_str());
        zima::kernel::BoxRequest separated_shell_box{10.0, 10.0, 10.0};
        separated_shell_box.translation = {20.0, 0.0, 0.0};
        bool rejected_multi_solid_shell = false;
        try {
            static_cast<void>(kernel.evaluate_history({
                {"shell-a", zima::kernel::BoxRequest{10.0, 10.0, 10.0},
                 zima::kernel::BooleanOperation::Add},
                {"shell-b", separated_shell_box,
                 zima::kernel::BooleanOperation::Add},
                {"shell", zima::kernel::ShellRequest{{
                    {"shell-a", "z_max", {}}}, 1.0},
                 zima::kernel::BooleanOperation::Add},
            }));
        } catch (const std::invalid_argument&) {
            rejected_multi_solid_shell = true;
        }
        require(rejected_multi_solid_shell,
            "Shell accepted multiple disjoint input solids");

        auto shell_document = zima::document::PartDocument::create_default();
        shell_document.history.clear();
        shell_document.history_order.clear();
        auto persisted_shell_box =
            zima::document::PartDocument::create_box_container();
        persisted_shell_box.box = {40.0, 30.0, 20.0};
        auto persisted_shell =
            zima::document::PartDocument::create_shell_container({{
                persisted_shell_box.id, "z_max", {}}});
        persisted_shell.shell.thickness = 1.25;
        shell_document.insert_history_entry(
            zima::document::PartHistoryKind::Feature,
            persisted_shell_box.id);
        shell_document.history.push_back(persisted_shell_box);
        shell_document.insert_history_entry(
            zima::document::PartHistoryKind::Feature,
            persisted_shell.id);
        shell_document.history.push_back(persisted_shell);
        const auto persisted_shell_boundaries = kernel.evaluate_history(
            shell_document.kernel_operations());
        const auto shell_path = std::filesystem::temp_directory_path() /
            "zima-cad-shell-roundtrip-contract.prtz";
        shell_document.save(shell_path, persisted_shell_boundaries);
        std::vector<zima::kernel::BodyResult> loaded_shell_boundaries;
        const auto loaded_shell_document = zima::document::PartDocument::load(
            shell_path, &loaded_shell_boundaries);
        std::filesystem::remove(shell_path);
        require(loaded_shell_document.history.size() == 2 &&
                    loaded_shell_document.history.back().feature_kind ==
                        zima::document::FeatureKind::Shell &&
                    loaded_shell_document.history.back().shell.thickness ==
                        1.25 &&
                    loaded_shell_document.history.back().shell.removed_faces ==
                        persisted_shell.shell.removed_faces &&
                    loaded_shell_boundaries.size() == 2 &&
                    loaded_shell_boundaries.back().source_fingerprint ==
                        persisted_shell_boundaries.back().source_fingerprint,
                "Shell parameters or calculated stable topology did not "
                "survive save/reopen");

        auto measured_shell_document =
            zima::document::PartDocument::create_default();
        measured_shell_document.history.clear();
        measured_shell_document.history_order.clear();
        auto measured_shell_box =
            zima::document::PartDocument::create_box_container();
        measured_shell_box.box = {100.0, 80.0, 50.0};
        const auto measured_shell_box_id = measured_shell_box.id;
        auto measured_shell_first_fillet =
            zima::document::PartDocument::create_fillet_container({
                {measured_shell_box_id,
                 "edge:x_max:y_min:z_max--x_max:y_min:z_min", {}},
                {measured_shell_box_id,
                 "edge:x_min:y_min:z_max--x_min:y_min:z_min", {}}});
        measured_shell_first_fillet.edge_treatment.primary_size = 5.0;
        auto measured_shell_second_fillet =
            zima::document::PartDocument::create_fillet_container({{
                measured_shell_box_id,
                "edge:x_max:y_max:z_min--x_max:y_min:z_min", {}}});
        measured_shell_second_fillet.edge_treatment.primary_size = 5.0;
        auto measured_shell =
            zima::document::PartDocument::create_shell_container({
                {measured_shell_box_id, "z_max", {}},
                {measured_shell_box_id, "x_max", {}}});
        measured_shell.shell.thickness = 1.0;
        const auto measured_shell_id = measured_shell.id;
        auto append_measured_shell_container =
            [&measured_shell_document](auto container) {
                measured_shell_document.insert_history_entry(
                    zima::document::PartHistoryKind::Feature, container.id);
                measured_shell_document.history.push_back(
                    std::move(container));
            };
        append_measured_shell_container(std::move(measured_shell_box));
        append_measured_shell_container(std::move(measured_shell_first_fillet));
        append_measured_shell_container(std::move(measured_shell_second_fillet));
        append_measured_shell_container(std::move(measured_shell));
        const auto measured_shell_results = kernel.evaluate_history(
            measured_shell_document.kernel_operations());
        const auto measured_shell_path =
            std::filesystem::temp_directory_path() /
            "zima-cad-shell-current-format-measure.prtz";
        measured_shell_document.save(
            measured_shell_path, measured_shell_results);
        const auto measured_shell_bytes =
            std::filesystem::file_size(measured_shell_path);
        std::vector<zima::kernel::BodyResult> measured_loaded_results;
        const auto measured_loaded_document =
            zima::document::PartDocument::load(
                measured_shell_path, &measured_loaded_results);
        std::filesystem::remove(measured_shell_path);
        const auto measured_shell_size_error =
            "Treated Shell document exceeded its persisted-size budget; bytes=" +
            std::to_string(measured_shell_bytes) + ", kernel_shape=" +
            std::to_string(
                measured_shell_results.back().kernel_shape.size());
        const bool loaded_shell_has_owned_reference_face =
            !measured_loaded_results.empty() && std::any_of(
                measured_loaded_results.back().mesh.original_references
                    .triangle_references.begin(),
                measured_loaded_results.back().mesh.original_references
                    .triangle_references.end(),
                [&](const auto& reference) {
                    return reference.owner_id == measured_shell_id;
                });
        require(measured_shell_bytes < 4U * 1024U * 1024U &&
                    measured_loaded_document.history.size() == 4 &&
                    measured_loaded_document.history.back().feature_kind ==
                        zima::document::FeatureKind::Shell &&
                    measured_loaded_results.size() == 4 &&
                    loaded_shell_has_owned_reference_face,
                measured_shell_size_error.c_str());

        zima::kernel::BoxRequest adjacent_second{10.0, 10.0, 10.0};
        adjacent_second.translation = {10.0, 0.0, 0.0};
        const auto adjacent_boxes = kernel.evaluate_history({
            {"adjacent-a", zima::kernel::BoxRequest{10.0, 10.0, 10.0},
             zima::kernel::BooleanOperation::Add},
            {"adjacent-b", adjacent_second,
             zima::kernel::BooleanOperation::Add},
        }).back();
        std::set<std::string> adjacent_top_owners;
        for (std::size_t triangle = 0;
             triangle < adjacent_boxes.mesh.triangle_references.size(); ++triangle) {
            if (triangle * 3 + 2 >= adjacent_boxes.mesh.triangles.size()) continue;
            bool top = true;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto vertex = adjacent_boxes.mesh.triangles[triangle * 3 + corner];
                top = top && vertex < adjacent_boxes.mesh.vertices.size() &&
                    std::abs(adjacent_boxes.mesh.vertices[vertex].z - 10.0) < 1.0e-7;
            }
            if (top) {
                adjacent_top_owners.insert(
                    adjacent_boxes.mesh.triangle_references[triangle].owner_id);
            }
        }
        const bool draws_provenance_seam = std::any_of(
            adjacent_boxes.mesh.edges.begin(), adjacent_boxes.mesh.edges.end(),
            [](const auto& edge) {
                return edge.points.size() >= 2 &&
                    std::all_of(edge.points.begin(), edge.points.end(),
                        [](const auto& point) {
                            return std::abs(point.x - 10.0) < 1.0e-7 &&
                                std::abs(point.z - 10.0) < 1.0e-7;
                        });
            });
        require(adjacent_top_owners ==
                    std::set<std::string>{"adjacent-a", "adjacent-b"} &&
                    !draws_provenance_seam,
                "Same-domain fuse merged source-face identities or drew their logical seam");

        std::vector<zima::kernel::EdgeReference> adjacent_top_front_route;
        std::set<std::string> adjacent_route_owners;
        for (const auto& edge : adjacent_boxes.mesh.edges) {
            if (!edge.reference.valid() || edge.points.size() < 2 ||
                !std::all_of(edge.points.begin(), edge.points.end(),
                    [](const auto& point) {
                        return std::abs(point.y) < 1.0e-7 &&
                            std::abs(point.z - 10.0) < 1.0e-7;
                    })) continue;
            adjacent_top_front_route.push_back(edge.reference);
            adjacent_route_owners.insert(edge.reference.owner_id);
        }
        require(adjacent_top_front_route.size() == 2 &&
                    adjacent_route_owners ==
                        std::set<std::string>{"adjacent-a", "adjacent-b"},
                "Fused Body did not retain both stable members of its tangent route");
        const auto tangent_route_fillet = kernel.evaluate_history({
            {"adjacent-a", zima::kernel::BoxRequest{10.0, 10.0, 10.0},
             zima::kernel::BooleanOperation::Add},
            {"adjacent-b", adjacent_second,
             zima::kernel::BooleanOperation::Add},
            {"route-fillet", zima::kernel::FilletRequest{
                adjacent_top_front_route,
                1.0},
             zima::kernel::BooleanOperation::Add},
        }).back();
        std::set<std::string> tangent_fillet_owners;
        for (const auto& reference :
                tangent_route_fillet.mesh.triangle_references) {
            tangent_fillet_owners.insert(reference.owner_id);
        }
        require(std::all_of(
                    tangent_route_fillet.mesh.triangle_references.begin(),
                    tangent_route_fillet.mesh.triangle_references.end(),
                    [](const auto& reference) { return reference.valid(); }) &&
                    tangent_fillet_owners == std::set<std::string>{
                        "adjacent-a", "adjacent-b", "route-fillet"},
                "Cross-container tangent Fillet lost source face ownership or "
                "left anonymous result faces");
        const auto tangent_route_chamfer = kernel.evaluate_history({
            {"adjacent-a", zima::kernel::BoxRequest{10.0, 10.0, 10.0},
             zima::kernel::BooleanOperation::Add},
            {"adjacent-b", adjacent_second,
             zima::kernel::BooleanOperation::Add},
            {"route-chamfer", zima::kernel::ChamferRequest{
                adjacent_top_front_route,
                1.0},
             zima::kernel::BooleanOperation::Add},
        }).back();
        std::set<std::string> tangent_chamfer_owners;
        for (const auto& reference :
                tangent_route_chamfer.mesh.triangle_references) {
            tangent_chamfer_owners.insert(reference.owner_id);
        }
        require(std::all_of(
                    tangent_route_chamfer.mesh.triangle_references.begin(),
                    tangent_route_chamfer.mesh.triangle_references.end(),
                    [](const auto& reference) { return reference.valid(); }) &&
                    tangent_chamfer_owners == std::set<std::string>{
                        "adjacent-a", "adjacent-b", "route-chamfer"},
                "Cross-container tangent Chamfer lost source face ownership or "
                "left anonymous result faces");

        zima::kernel::BoxRequest through_cutter{10.0, 10.0, 12.0};
        through_cutter.translation = {5.0, 5.0, -1.0};
        const auto clipped_box = kernel.evaluate_history({
            {"clipped-base", zima::kernel::BoxRequest{20.0, 20.0, 10.0},
             zima::kernel::BooleanOperation::Add},
            {"clipping-tool", through_cutter,
             zima::kernel::BooleanOperation::Subtract},
        }).back();
        std::set<std::string> clipped_face_owners;
        for (const auto& reference : clipped_box.mesh.triangle_references) {
            require(reference.valid(),
                "Subtract produced an anonymous visible Body fragment");
            clipped_face_owners.insert(reference.owner_id);
        }
        require(clipped_face_owners ==
                    std::set<std::string>{"clipped-base", "clipping-tool"},
                "Subtract did not map surviving and cut-wall fragments to their sources");
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
        const auto selected_box_display_edge = std::find_if(
            body.mesh.edges.begin(), body.mesh.edges.end(),
            [&](const auto& edge) {
                return edge.reference == selected_box_edge;
            });
        require(selected_box_display_edge != body.mesh.edges.end() &&
                    selected_box_display_edge->
                        edge_treatment_side_directions.size() == 2 &&
                    selected_box_display_edge->
                        edge_treatment_side_references.size() == 2 &&
                    selected_box_display_edge->
                        edge_treatment_endpoint_references.size() == 2 &&
                    std::tie(selected_box_display_edge->
                            edge_treatment_side_references[0].owner_id,
                        selected_box_display_edge->
                            edge_treatment_side_references[0].semantic_key) <
                        std::tie(selected_box_display_edge->
                            edge_treatment_side_references[1].owner_id,
                        selected_box_display_edge->
                            edge_treatment_side_references[1].semantic_key) &&
                    std::ranges::all_of(
                        selected_box_display_edge->
                            edge_treatment_side_directions,
                        [&](const auto& side) {
                            return side.size() ==
                                selected_box_display_edge->points.size();
                        }),
                "Calculated solid edge did not persist two aligned adjacent-face "
                "directions for the Fillet/Chamfer preview");
        const auto require_stable_body_edges = [](const auto& result,
                                                   std::string_view operation) {
            for (const auto& edge : result.mesh.edges) {
                const auto anonymous_message = std::string(operation) +
                    " left an anonymous operational-body edge";
                require(edge.reference.valid(),
                    anonymous_message.c_str());
            }
        };
        const auto fillet_boundaries = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"fillet", zima::kernel::FilletRequest{
                {selected_box_edge},
                3.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(fillet_boundaries.size() == 2 &&
                    fillet_boundaries.back().volume < body.volume &&
                    fillet_boundaries.back().volume > body.volume - 1000.0 &&
                    std::any_of(
                        fillet_boundaries.back().mesh.triangle_references.begin(),
                        fillet_boundaries.back().mesh.triangle_references.end(),
                        [](const auto& reference) {
                            return reference.owner_id == "fillet" &&
                                reference.semantic_key.starts_with(
                                    "fillet:face:from:");
                        }),
                "Original-edge Fillet did not produce a valid bounded solid");
        require_stable_body_edges(fillet_boundaries.back(), "Fillet");
        const auto fillet_boundary_edge_count = std::ranges::count_if(
            fillet_boundaries.back().mesh.edges, [](const auto& edge) {
                return std::ranges::find(
                    edge.edge_treatment_owner_ids, "fillet") !=
                    edge.edge_treatment_owner_ids.end();
            });
        const auto curved_fillet_boundary_edge_count = std::ranges::count_if(
            fillet_boundaries.back().mesh.edges, [](const auto& edge) {
                return edge.points.size() > 2 &&
                    std::ranges::find(edge.edge_treatment_owner_ids, "fillet") !=
                        edge.edge_treatment_owner_ids.end();
            });
        const auto fillet_owned_edge_count = std::ranges::count_if(
            fillet_boundaries.back().mesh.edges, [](const auto& edge) {
                return edge.reference.owner_id == "fillet";
            });
        require(fillet_boundary_edge_count == 4 &&
                    curved_fillet_boundary_edge_count == 2 &&
                    fillet_owned_edge_count == fillet_boundary_edge_count,
                "Single-edge Fillet did not persist exactly its four-edge "
                "surface boundary, or stole identity from surviving Box edges");
        const std::vector<zima::kernel::HistoryOperation> edited_fillet_history{
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"fillet", zima::kernel::FilletRequest{
                {selected_box_edge},
                4.0},
             zima::kernel::BooleanOperation::Add},
        };
        const auto incremental_fillet = kernel.evaluate_history_incremental(
            edited_fillet_history, fillet_boundaries);
        const auto full_edited_fillet =
            kernel.evaluate_history(edited_fillet_history);
        require(incremental_fillet.size() == full_edited_fillet.size() &&
                    incremental_fillet.back().mesh.vertices ==
                        full_edited_fillet.back().mesh.vertices &&
                    incremental_fillet.back().mesh.triangles ==
                        full_edited_fillet.back().mesh.triangles &&
                    std::abs(incremental_fillet.back().volume -
                             full_edited_fillet.back().volume) < 1.0e-7,
                "Live topology cache changed an incremental Fillet result");
        const auto chamfer_boundaries = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"chamfer", zima::kernel::ChamferRequest{
                {selected_box_edge},
                3.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(chamfer_boundaries.size() == 2 &&
                    chamfer_boundaries.back().volume < body.volume &&
                    chamfer_boundaries.back().volume > body.volume - 1000.0 &&
                    std::any_of(
                        chamfer_boundaries.back().mesh.triangle_references.begin(),
                        chamfer_boundaries.back().mesh.triangle_references.end(),
                        [](const auto& reference) {
                            return reference.owner_id == "chamfer" &&
                                reference.semantic_key.starts_with(
                                    "chamfer:face:from:");
                        }),
                "Original-edge Chamfer did not produce a valid bounded solid");
        require_stable_body_edges(chamfer_boundaries.back(), "Chamfer");
        const auto chamfer_boundary_edge_count =
            std::ranges::count_if(chamfer_boundaries.back().mesh.edges,
                    [](const auto& edge) {
                        return std::ranges::find(
                            edge.edge_treatment_owner_ids, "chamfer") !=
                            edge.edge_treatment_owner_ids.end();
                    });
        const auto chamfer_owned_edge_count =
            std::ranges::count_if(chamfer_boundaries.back().mesh.edges,
                [](const auto& edge) {
                    return edge.reference.owner_id == "chamfer";
                });
        require(chamfer_boundary_edge_count == 4 &&
                    chamfer_owned_edge_count == chamfer_boundary_edge_count,
                "Single-edge Chamfer did not persist exactly its four-edge "
                "surface boundary, or stole identity from surviving Box edges");

        const auto two_distance_chamfer = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"chamfer-a-b", zima::kernel::ChamferRequest{
                {selected_box_edge},
                zima::kernel::ChamferRequest::Mode::TwoDistances,
                2.0, 5.0, std::numbers::pi / 4.0, false},
             zima::kernel::BooleanOperation::Add},
        });
        const auto flipped_two_distance_chamfer = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"chamfer-a-b", zima::kernel::ChamferRequest{
                {selected_box_edge},
                zima::kernel::ChamferRequest::Mode::TwoDistances,
                2.0, 5.0, std::numbers::pi / 4.0, true},
             zima::kernel::BooleanOperation::Add},
        });
        require(two_distance_chamfer.size() == 2 &&
                    flipped_two_distance_chamfer.size() == 2 &&
                    two_distance_chamfer.back().volume < body.volume &&
                    flipped_two_distance_chamfer.back().volume < body.volume &&
                    two_distance_chamfer.back().mesh.vertices !=
                        flipped_two_distance_chamfer.back().mesh.vertices,
                "A x B Chamfer or FLIP did not select a stable opposite support face");
        const auto distance_angle_chamfer = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"chamfer-a-angle", zima::kernel::ChamferRequest{
                {selected_box_edge},
                zima::kernel::ChamferRequest::Mode::DistanceAngle,
                3.0, 1.0, std::numbers::pi / 6.0, false},
             zima::kernel::BooleanOperation::Add},
        });
        require(distance_angle_chamfer.size() == 2 &&
                    distance_angle_chamfer.back().volume < body.volume,
                "A + angle Chamfer did not produce a valid bounded solid");
        const auto linear_fillet = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"linear-fillet", zima::kernel::FilletRequest{
                {selected_box_edge},
                zima::kernel::FilletRequest::Mode::Linear,
                2.0, 5.0, false,
                {selected_box_display_edge->
                    edge_treatment_endpoint_references.front()}},
             zima::kernel::BooleanOperation::Add},
        });
        const auto reversed_linear_fillet = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"linear-fillet", zima::kernel::FilletRequest{
                {selected_box_edge},
                zima::kernel::FilletRequest::Mode::Linear,
                2.0, 5.0, true,
                {selected_box_display_edge->
                    edge_treatment_endpoint_references.front()}},
             zima::kernel::BooleanOperation::Add},
        });
        require(linear_fillet.size() == 2 &&
                    reversed_linear_fillet.size() == 2 &&
                    linear_fillet.back().volume < body.volume &&
                    reversed_linear_fillet.back().volume < body.volume &&
                    linear_fillet.back().mesh.vertices !=
                        reversed_linear_fillet.back().mesh.vertices,
                "Linear R1 -> R2 Fillet or direction reversal did not affect geometry");
        const auto fillet_radius_at_endpoint = [](const auto& result,
                                                   std::string_view owner_id,
                                                   const auto& endpoint,
                                                   const auto& opposite) {
            const auto axis_x = opposite.x - endpoint.x;
            const auto axis_y = opposite.y - endpoint.y;
            const auto axis_z = opposite.z - endpoint.z;
            const auto axis_length =
                std::hypot(std::hypot(axis_x, axis_y), axis_z);
            double radius = -1.0;
            for (const auto& edge : result.mesh.edges) {
                if (edge.reference.owner_id != owner_id || edge.points.empty())
                    continue;
                const bool lies_in_endpoint_plane =
                    std::ranges::all_of(edge.points, [&](const auto& point) {
                        const auto projection =
                            ((point.x - endpoint.x) * axis_x +
                             (point.y - endpoint.y) * axis_y +
                             (point.z - endpoint.z) * axis_z) /
                            axis_length;
                        return std::abs(projection) < 1.0e-6;
                    });
                if (!lies_in_endpoint_plane) continue;
                radius = std::max(radius, 0.0);
                for (const auto& point : edge.points) {
                    radius = std::max(radius,
                        std::hypot(
                            std::hypot(point.x - endpoint.x,
                                       point.y - endpoint.y),
                            point.z - endpoint.z));
                }
            }
            return radius;
        };
        const auto& visual_start = selected_box_display_edge->points.front();
        const auto& visual_end = selected_box_display_edge->points.back();
        const auto start_radius = fillet_radius_at_endpoint(
            linear_fillet.back(), "linear-fillet", visual_start, visual_end);
        const auto end_radius = fillet_radius_at_endpoint(
            linear_fillet.back(), "linear-fillet", visual_end, visual_start);
        const auto reversed_start_radius = fillet_radius_at_endpoint(
            reversed_linear_fillet.back(), "linear-fillet", visual_start,
            visual_end);
        const auto reversed_end_radius = fillet_radius_at_endpoint(
            reversed_linear_fillet.back(), "linear-fillet", visual_end,
            visual_start);
        require(std::abs(start_radius - 2.0) < 0.1 &&
                    std::abs(end_radius - 5.0) < 0.1 &&
                    std::abs(reversed_start_radius - 5.0) < 0.1 &&
                    std::abs(reversed_end_radius - 2.0) < 0.1,
                "Variable Fillet R1/R2 did not follow the displayed route "
                "direction and Reverse contract");

        const zima::kernel::EdgeReference first_fillet_edge{
            "box",
            "edge:x_max:y_max:z_max--x_min:y_max:z_max",
            {}};
        const auto identity_fillet_boundaries = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"identity-fillet", zima::kernel::FilletRequest{
                {first_fillet_edge}, 7.0},
             zima::kernel::BooleanOperation::Add},
        });
        const auto end_fillet_edge = [&](double expected_x) {
            return std::find_if(
                identity_fillet_boundaries.back().mesh.edges.begin(),
                identity_fillet_boundaries.back().mesh.edges.end(),
                [&](const auto& edge) {
                    return edge.reference.owner_id == "identity-fillet" &&
                        !edge.points.empty() &&
                        std::ranges::all_of(edge.points,
                            [&](const auto& point) {
                                return std::abs(point.x - expected_x) < 1.0e-7;
                            });
                });
        };
        const auto positive_end_fillet = end_fillet_edge(100.0);
        const auto negative_end_fillet = end_fillet_edge(0.0);
        require(positive_end_fillet !=
                    identity_fillet_boundaries.back().mesh.edges.end() &&
                    negative_end_fillet !=
                        identity_fillet_boundaries.back().mesh.edges.end(),
                "Fillet did not expose both generated end edges for the identity test");
        const std::vector<zima::kernel::EdgeReference> identity_chamfer_route{
            {"box",
             "edge:x_max:y_max:z_max--x_max:y_min:z_max", {}},
            positive_end_fillet->reference,
            {"box",
             "edge:x_max:y_max:z_max--x_max:y_max:z_min", {}}};
        const std::vector<zima::kernel::EdgeReference> unaffected_opposite_route{
            {"box",
             "edge:x_min:y_max:z_max--x_min:y_min:z_max", {}},
            negative_end_fillet->reference,
            {"box",
             "edge:x_min:y_max:z_max--x_min:y_max:z_min", {}}};
        const auto separated_treatments = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"identity-fillet", zima::kernel::FilletRequest{
                {first_fillet_edge}, 7.0},
             zima::kernel::BooleanOperation::Add},
            {"separate-chamfer", zima::kernel::ChamferRequest{
                identity_chamfer_route, 5.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(std::ranges::all_of(unaffected_opposite_route,
                    [&](const auto& expected) {
                        return std::ranges::any_of(
                            separated_treatments.back().mesh.edges,
                            [&](const auto& edge) {
                                return edge.reference == expected;
                            });
                    }),
                "Geometrically separate Chamfer stole unchanged opposite-edge identities");
        const auto after_deleting_separate_chamfer = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"identity-fillet", zima::kernel::FilletRequest{
                {first_fillet_edge}, 7.0},
             zima::kernel::BooleanOperation::Add},
            {"opposite-fillet", zima::kernel::FilletRequest{
                unaffected_opposite_route, 4.0},
             zima::kernel::BooleanOperation::Add},
        });
        require(after_deleting_separate_chamfer.size() == 3,
                "Deleting an unrelated Chamfer broke the opposite Fillet route");
        const auto generated_fillet_edge = std::find_if(
            fillet_boundaries.back().mesh.edges.begin(),
            fillet_boundaries.back().mesh.edges.end(), [](const auto& edge) {
                return edge.reference.owner_id == "fillet";
            });
        require(generated_fillet_edge != fillet_boundaries.back().mesh.edges.end(),
                "Fillet did not expose a generated operational-body edge");
        std::optional<zima::kernel::EdgeReference> chained_chamfer_edge;
        std::optional<std::vector<zima::kernel::BodyResult>>
            chained_chamfer_boundaries;
        for (const auto& candidate : fillet_boundaries.back().mesh.edges) {
            try {
                auto calculated = kernel.evaluate_history({
                    {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
                     zima::kernel::BooleanOperation::Add},
                    {"fillet", zima::kernel::FilletRequest{
                        {selected_box_edge},
                        3.0},
                     zima::kernel::BooleanOperation::Add},
                    {"chained-chamfer", zima::kernel::ChamferRequest{
                        {candidate.reference},
                        1.0},
                     zima::kernel::BooleanOperation::Add},
                });
                if (calculated.size() == 3) {
                    chained_chamfer_edge = candidate.reference;
                    chained_chamfer_boundaries = std::move(calculated);
                    break;
                }
            } catch (const std::exception&) {
                // Some rounded-body edges cannot accept this distance.
            }
        }
        require(chained_chamfer_edge && chained_chamfer_boundaries,
                "Fillet body exposed no stable edge suitable for a second treatment");
        require_stable_body_edges(
            chained_chamfer_boundaries->back(), "Chained Chamfer");
        const auto generated_chamfer_edge = std::find_if(
            chained_chamfer_boundaries->back().mesh.edges.begin(),
            chained_chamfer_boundaries->back().mesh.edges.end(), [](const auto& edge) {
                return edge.reference.owner_id == "chained-chamfer";
            });
        require(generated_chamfer_edge !=
                    chained_chamfer_boundaries->back().mesh.edges.end(),
                "Chained Chamfer did not expose its generated body edge");
        std::optional<std::vector<zima::kernel::BodyResult>> third_treatment;
        for (const auto& candidate :
                chained_chamfer_boundaries->back().mesh.edges) {
            try {
                auto calculated = kernel.evaluate_history({
                    {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
                     zima::kernel::BooleanOperation::Add},
                    {"fillet", zima::kernel::FilletRequest{
                        {selected_box_edge},
                        3.0},
                     zima::kernel::BooleanOperation::Add},
                    {"chained-chamfer", zima::kernel::ChamferRequest{
                        {*chained_chamfer_edge},
                        1.0},
                     zima::kernel::BooleanOperation::Add},
                    {"third-fillet", zima::kernel::FilletRequest{
                        {candidate.reference},
                        0.4},
                     zima::kernel::BooleanOperation::Add},
                });
                if (calculated.size() == 4) {
                    third_treatment = std::move(calculated);
                    break;
                }
            } catch (const std::exception&) {
                // Not every geometrically valid edge admits the requested
                // radius. The contract is that the post-Chamfer body still
                // exposes at least one stable edge for a third operation.
            }
        }
        require(third_treatment && third_treatment->size() == 4,
                "Third edge treatment could not consume a generated Chamfer edge");
        require_stable_body_edges(third_treatment->back(), "Third Fillet");
        const auto multi_fillet_boundaries = kernel.evaluate_history({
            {"box", zima::kernel::BoxRequest{100.0, 80.0, 50.0},
             zima::kernel::BooleanOperation::Add},
            {"multi-fillet", zima::kernel::FilletRequest{{
                body.mesh.original_references.edges[0].reference,
                body.mesh.original_references.edges[1].reference},
                2.0},
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
        auto incremental_document = zima::document::PartDocument::create_default();
        incremental_document.history.clear();
        for (int index = 0; index < 10; ++index) {
            auto feature = zima::document::PartDocument::create_box_container();
            feature.placement.x = index * 12.0;
            feature.box = {20.0, 20.0, 20.0};
            incremental_document.history.push_back(std::move(feature));
        }
        const auto incremental_original_operations =
            incremental_document.kernel_operations();
        const auto incremental_original =
            kernel.evaluate_history(incremental_original_operations);
        require(std::count_if(incremental_original.begin(),
                    incremental_original.end(), [](const auto& boundary) {
                        return !boundary.kernel_shape.empty();
                    }) == 1 &&
                    std::all_of(incremental_original.begin(),
                        incremental_original.end() - 1,
                        [](const auto& boundary) {
                            return boundary.kernel_shape.empty();
                        }) &&
                    !incremental_original.back().kernel_shape.empty(),
                "Part history persisted an intermediate BRep checkpoint");
        incremental_document.history.back().box.height = 30.0;
        const auto incremental_changed_operations =
            incremental_document.kernel_operations();
        const auto incremental_result = kernel.evaluate_history_incremental(
            incremental_changed_operations, incremental_original);
        const auto incremental_full =
            kernel.evaluate_history(incremental_changed_operations);
        zima::kernel::OcctKernel cold_incremental_kernel;
        const auto cold_incremental_result =
            cold_incremental_kernel.evaluate_history_incremental(
                incremental_changed_operations, incremental_original);
        require(incremental_result.size() == incremental_full.size() &&
                    incremental_result.back().source_fingerprint ==
                        incremental_full.back().source_fingerprint &&
                    std::abs(incremental_result.back().volume -
                             incremental_full.back().volume) < 1.0e-7 &&
                    incremental_result.back().mesh.triangles ==
                        incremental_full.back().mesh.triangles &&
                    incremental_result.back().mesh.vertices ==
                        incremental_full.back().mesh.vertices,
                "Incremental Part regeneration differs from a full calculation");
        require(cold_incremental_result.size() == incremental_full.size() &&
                    cold_incremental_result.back().source_fingerprint ==
                        incremental_full.back().source_fingerprint &&
                    cold_incremental_result.back().mesh.vertices ==
                        incremental_full.back().mesh.vertices &&
                    cold_incremental_result.back().mesh.triangles ==
                        incremental_full.back().mesh.triangles &&
                    cold_incremental_result.back().mesh.triangle_references ==
                        incremental_full.back().mesh.triangle_references,
                "Cold regeneration did not rebuild stable source ancestry");
        auto appended_document = incremental_document;
        appended_document.history.back().box.height = 20.0;
        auto appended_feature =
            zima::document::PartDocument::create_box_container();
        appended_feature.placement.x = 120.0;
        appended_feature.box = {20.0, 20.0, 20.0};
        appended_document.history.push_back(std::move(appended_feature));
        const auto appended_operations = appended_document.kernel_operations();
        zima::kernel::OcctKernel cold_append_kernel;
        const auto appended_incremental =
            cold_append_kernel.evaluate_history_incremental(
                appended_operations, incremental_original);
        const auto appended_full = kernel.evaluate_history(appended_operations);
        require(appended_incremental.size() == appended_full.size() &&
                    appended_incremental.back().source_fingerprint ==
                        appended_full.back().source_fingerprint &&
                    appended_incremental.back().mesh.vertices ==
                        appended_full.back().mesh.vertices &&
                    appended_incremental.back().mesh.triangles ==
                        appended_full.back().mesh.triangles &&
                    appended_incremental.back().mesh.triangle_references ==
                        appended_full.back().mesh.triangle_references,
                "Cold appended operation did not rebuild stable source ancestry");
        require(std::equal(incremental_result.begin(),
                    incremental_result.end() - 1, incremental_original.begin(),
                    [](const auto& reused, const auto& original) {
                        return reused.source_fingerprint == original.source_fingerprint &&
                            reused.kernel_shape == original.kernel_shape;
                    }),
                "Incremental Part regeneration did not preserve its unchanged prefix");
        require(std::all_of(incremental_result.begin(),
                    incremental_result.end() - 1, [](const auto& boundary) {
                        const auto& references = boundary.mesh.original_references;
                        return references.vertices.empty() &&
                            references.edges.empty() && references.points.empty() &&
                            references.axes.empty();
                    }) &&
                    !incremental_result.back().mesh.original_references.edges.empty(),
                "Part history duplicated cumulative reference geometry in every boundary");
        zima::document::DocumentSession incremental_session(
            incremental_document, incremental_result);
        const auto cursor_body = incremental_session.calculated_boundary(4);
        require(cursor_body &&
                    !cursor_body->mesh.original_references.edges.empty() &&
                    std::all_of(
                        cursor_body->mesh.original_references.edges.begin(),
                        cursor_body->mesh.original_references.edges.end(),
                        [&](const auto& edge) {
                            return std::any_of(
                                incremental_document.history.begin(),
                                incremental_document.history.begin() + 4,
                                [&](const auto& feature) {
                                    return feature.id == edge.reference.owner_id;
                                });
                        }),
                "Compact Part cache did not reconstruct history-cursor references");
        const auto rollback = incremental_session.rollback_boundary(
            incremental_document.history.back().id);
        require(rollback && rollback->input_body &&
                    !rollback->input_body->mesh.original_references.edges.empty() &&
                    std::none_of(
                        rollback->input_body->mesh.original_references.edges.begin(),
                        rollback->input_body->mesh.original_references.edges.end(),
                        [&](const auto& edge) {
                            return edge.reference.owner_id ==
                                incremental_document.history.back().id;
                        }),
                "Compact Part cache did not reconstruct rollback references");
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
        zima::kernel::BoxRequest touching_second{10.0, 10.0, 10.0};
        touching_second.translation = {10.0, 0.0, 0.0};
        const auto touching = kernel.evaluate_history({
            {"touching-first", zima::kernel::BoxRequest{10.0, 10.0, 10.0},
             zima::kernel::BooleanOperation::Add, false, 0.001},
            {"touching-second", touching_second,
             zima::kernel::BooleanOperation::Add, false, 0.001},
        }).back();
        require(std::abs(touching.volume - 2000.0) < 1.0e-6 &&
                    std::abs(touching.surface_area - 1000.0) < 1.0e-6,
                "Touching additive solids changed their unified body measure");
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
                    cone_boundaries.front().mesh.original_references.axes.size() == 1 &&
                    cone_boundaries.front().mesh.axes.size() == 1 &&
                    cone_boundaries.front().mesh.axes.front().reference.semantic_key ==
                        "axis:primary",
                "Cone geometry or stable axis is incorrect");
        zima::kernel::PyramidRequest pyramid;
        pyramid.length = 30.0; pyramid.width = 20.0; pyramid.height = 40.0;
        const auto pyramid_boundaries = kernel.evaluate_history({
            {"pyramid", pyramid, zima::kernel::BooleanOperation::Add}});
        require(pyramid_boundaries.size() == 1 &&
                    std::abs(pyramid_boundaries.front().volume - 8000.0) < 1e-5 &&
                    pyramid_boundaries.front().mesh.original_references.axes.size() == 3 &&
                    !pyramid_boundaries.front().mesh.original_references
                        .triangle_references.empty() &&
                    !pyramid_boundaries.front().mesh.original_references.edges.empty() &&
                    !pyramid_boundaries.front().mesh.original_references.points.empty(),
                "Pyramid geometry or references are incorrect");
        zima::kernel::WedgeRequest wedge;
        wedge.length = 60.0; wedge.width = 40.0;
        wedge.height = 40.0; wedge.top_offset = 30.0;
        const auto wedge_boundaries = kernel.evaluate_history({
            {"wedge", wedge, zima::kernel::BooleanOperation::Add}});
        require(wedge_boundaries.size() == 1 && wedge_boundaries.front().volume > 0.0 &&
                    wedge_boundaries.front().mesh.original_references.axes.size() == 3 &&
                    !wedge_boundaries.front().mesh.original_references
                        .triangle_references.empty() &&
                    !wedge_boundaries.front().mesh.original_references.edges.empty() &&
                    !wedge_boundaries.front().mesh.original_references.points.empty(),
                "Wedge geometry or references are incorrect");
        std::set<std::string> cylinder_edges;
        bool sampled_circle = false;
        bool persisted_parameter_seam = false;
        for (const auto& edge : cylinder_boundaries.front().mesh.original_references.edges) {
            cylinder_edges.insert(edge.reference.semantic_key);
            if (edge.reference.semantic_key == "seam") {
                persisted_parameter_seam = edge.parameter_seam;
            }
            if (edge.reference.semantic_key.starts_with("circle:")) {
                sampled_circle = sampled_circle || edge.points.size() > 16;
            }
        }
        require(cylinder_edges == std::set<std::string>{
                    "circle:z_max", "circle:z_min", "seam"} && sampled_circle &&
                    persisted_parameter_seam,
                "Cylinder edges are not stable selectable viewer polylines");
        require(std::ranges::none_of(cylinder_boundaries.front().mesh.edges,
                    [](const auto& edge) {
                        return edge.reference.semantic_key == "seam" ||
                            edge.reference.semantic_key.starts_with("seam:");
                    }),
                "Cylinder parameterization seam leaked into visible/hidden edges");
        require(cylinder_boundaries.front().mesh.original_references.axes.size() == 1 &&
                    cylinder_boundaries.front().mesh.axes.size() == 1 &&
                    cylinder_boundaries.front().mesh.original_references.axes.front()
                        .reference.semantic_key == "axis:primary" &&
                    cylinder_boundaries.front().mesh.axes.front().display_length > 20.0,
                "Cylinder does not expose its fitted visible primary axis");

        auto document = zima::document::PartDocument::create_default();
        require(document.history.empty(), "New Part must have empty history");
        document.document_units["Length"] = "cm";
        document.user_parameters = {{"name", "Bracket"}};
        document.user_parameter_order = {"name"};
        document.user_parameter_labels["name"] = {{"cs", "Název"}, {"en", "Name"}};
        document.user_parameter_values["name"] = {{"", "Bracket"}};
        document.document_precision["decimal_places"] = "5";
        document.physical_parameters["MASS_DENSITY"] = "7.85e-6";
        document.physical_parameter_units["MASS_DENSITY"] = "kg/mm^3";
        document.material_parameter_descriptions["MASS_DENSITY"]["cs"] = "Hustota";
        document.family_table = R"({"columns":["length"],"instances":[]})";
        document.body_color = "#803F6F9F";
        document.face_colors = {{"feature-a::side:curve-1", "#FFFF0000"},
                                {"feature-a::cap:end", "#FF00AA44"}};
        const auto empty_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-empty-contract.prtz";
        document.save(empty_path);
        std::ifstream empty_serialized(empty_path);
        const std::string empty_text(
            (std::istreambuf_iterator<char>(empty_serialized)),
            std::istreambuf_iterator<char>());
        require(empty_text.find("[Document]\n") != std::string::npos &&
                    empty_text.find("format_version=12\n") != std::string::npos &&
                    empty_text.find("[DocumentUnits]\n") != std::string::npos &&
                    empty_text.find("[UserParameterValues]\n") != std::string::npos,
                "Part persistence did not write the Python-compatible INI sections");
        const auto empty_loaded = zima::document::PartDocument::load(empty_path);
        empty_serialized.close();
        std::filesystem::remove(empty_path);
        require(empty_loaded.history.empty(), "Empty history was not preserved");
        require(empty_loaded.document_units == document.document_units &&
                    empty_loaded.user_parameters == document.user_parameters &&
                    empty_loaded.user_parameter_order == document.user_parameter_order &&
                    empty_loaded.user_parameter_labels == document.user_parameter_labels &&
                    empty_loaded.user_parameter_values == document.user_parameter_values &&
                    empty_loaded.document_precision == document.document_precision &&
                    empty_loaded.physical_parameters == document.physical_parameters &&
                    empty_loaded.physical_parameter_units == document.physical_parameter_units &&
                    empty_loaded.material_parameter_descriptions ==
                        document.material_parameter_descriptions &&
                    empty_loaded.family_table == document.family_table &&
                    empty_loaded.body_color == document.body_color &&
                    empty_loaded.face_colors == document.face_colors,
                "Part document tools data did not round-trip");
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
        require(!loaded_boundaries.back().mesh.points.empty() &&
                    std::ranges::all_of(
                        loaded_boundaries.back().mesh.points,
                        [](const auto& point) {
                            return !point.always_visible;
                        }),
                "Part save/load turned solid reference vertices into visible points");
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
        suppression_document.relations = {
            {"diameter", "radius * 2"}, {"scaled", "max(diameter, 12) + model.volume"}};
        const auto evaluated_parameters = zima::document::evaluate_relations(
            {{"radius", "5"}}, suppression_document.relations,
            {{"model.volume", 3.0}}, 2);
        require(evaluated_parameters.at("diameter") == "10.00" &&
                    evaluated_parameters.at("scaled") == "15.00",
                "Ordered document relations were not evaluated deterministically");
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
        require(loaded_suppression.relations == suppression_document.relations,
                "Part relations did not survive save/load");
        auto constructions = zima::document::PartDocument::create_default();
        auto origin_point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        require(origin_point.container_origin.parent_id == origin_point.id &&
                    origin_point.container_origin.id == origin_point.id + ":origin" &&
                    origin_point.container_origin.children.size() == 7 &&
                    origin_point.container_origin.children.front().id ==
                        origin_point.container_origin.id + ":point" &&
                    origin_point.container_origin.children.back().id ==
                        origin_point.container_origin.id + ":plane:xz",
                "Construction container did not own the complete persisted Origin");
        origin_point.definition =
            zima::document::ConstructionDefinition::PointReference;
        origin_point.references = {{{}, constructions.document_id + ":origin",
                                     "origin:point"}};
        constructions.constructions.push_back(origin_point);
        constructions.resolve_constructions();
        require(constructions.constructions.front().reference_valid &&
                    constructions.constructions.front().origin ==
                        zima::kernel::Vec3{},
                "Construction Point did not resolve the persisted document Origin");
        const auto document_origin_mesh = constructions.origin_viewer_mesh();
        const auto document_origin_xy = std::find_if(
            document_origin_mesh.edges.begin(), document_origin_mesh.edges.end(),
            [](const auto& edge) {
                return edge.reference.semantic_key == "origin:plane:xy";
            });
        const auto polyline_segment_length = [](const auto& edge) {
            const auto& first = edge.points[0];
            const auto& second = edge.points[1];
            return std::hypot(std::hypot(second.x - first.x,
                                         second.y - first.y),
                              second.z - first.z);
        };
        require(document_origin_xy != document_origin_mesh.edges.end() &&
                    document_origin_mesh.axes.size() == 3 &&
                    std::all_of(document_origin_mesh.axes.begin(),
                        document_origin_mesh.axes.end(), [&](const auto& axis) {
                            return std::abs(axis.display_length * 2.0 -
                                polyline_segment_length(*document_origin_xy)) <
                                1.0e-12;
                        }),
                "Document Origin axis arrows do not terminate on the plane edge");
        auto plane_constrained_point =
            zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Point);
        plane_constrained_point.origin = {7.0, 8.0, 9.0};
        plane_constrained_point.definition =
            zima::document::ConstructionDefinition::PointReference;
        plane_constrained_point.references = {
            {{}, constructions.document_id + ":origin", "origin:plane:yz"},
            {{}, constructions.document_id + ":origin", "origin:plane:xz"},
            {{}, constructions.document_id + ":origin", "origin:plane:xy"}};
        constructions.constructions = {plane_constrained_point};
        const auto document_origin_geometry =
            constructions.origin_viewer_mesh().original_references;
        const auto xy_state = zima::document::point_constraint_state({
                    {{}, constructions.document_id + ":origin", "origin:plane:xy"}},
                    document_origin_geometry);
        require(xy_state.remaining_dof == 2 &&
                    xy_state.constrained_axes == std::array<bool, 3>{false, false, true} &&
                zima::document::point_constraint_remaining_dof({
                    {{}, constructions.document_id + ":origin", "origin:plane:xy"},
                    {{}, constructions.document_id + ":origin", "origin:plane:xy"}},
                    document_origin_geometry) == 2 &&
                zima::document::point_constraint_remaining_dof({
                    {{}, constructions.document_id + ":origin", "origin:plane:xy"},
                    {{}, constructions.document_id + ":origin", "origin:plane:yz"}},
                    document_origin_geometry) == 1 &&
                zima::document::point_constraint_remaining_dof({
                    {{}, constructions.document_id + ":origin", "origin:point"}},
                    document_origin_geometry) == 0,
                "Point placement DOF did not use geometric matrix rank");
        auto dimensioned_point = plane_constrained_point;
        const std::array reference_offsets{4.0, 5.0, 6.0};
        for (std::size_t index = 0;
             index < dimensioned_point.references.size(); ++index) {
            dimensioned_point.references[index].supports_offset = true;
            dimensioned_point.references[index].offset =
                reference_offsets[index];
        }
        require(zima::document::resolve_construction(
                    dimensioned_point, document_origin_geometry),
                "Dimensioned Point fixture did not resolve its Origin planes");
        const auto referenced_point_dimensions =
            zima::document::construction_point_dimensions(
                dimensioned_point, document_origin_geometry);
        require(referenced_point_dimensions.size() == 3 &&
                    referenced_point_dimensions[0].reference.semantic_key ==
                        "parameter:reference_offset:0" &&
                    referenced_point_dimensions[1].reference.semantic_key ==
                        "parameter:reference_offset:1" &&
                    referenced_point_dimensions[2].reference.semantic_key ==
                        "parameter:reference_offset:2" &&
                    referenced_point_dimensions[0].label_prefix.empty() &&
                    referenced_point_dimensions[1].label_prefix.empty() &&
                    referenced_point_dimensions[2].label_prefix.empty() &&
                    std::abs(referenced_point_dimensions[2].plane_normal.x) <
                        1.0e-9 &&
                    std::abs(referenced_point_dimensions[2].plane_normal.y) <
                        1.0e-9 &&
                    std::abs(std::abs(
                        referenced_point_dimensions[2].plane_normal.z) - 1.0) <
                        1.0e-9 &&
                    std::abs(referenced_point_dimensions[0].witness_second.y -
                        referenced_point_dimensions[0].witness_first.y) < 1.0e-9 &&
                    std::abs(referenced_point_dimensions[0].witness_second.z -
                        referenced_point_dimensions[0].witness_first.z) < 1.0e-9 &&
                    std::abs(referenced_point_dimensions[1].witness_second.x -
                        referenced_point_dimensions[1].witness_first.x) < 1.0e-9 &&
                    std::abs(referenced_point_dimensions[1].witness_second.z -
                        referenced_point_dimensions[1].witness_first.z) < 1.0e-9 &&
                    std::abs(referenced_point_dimensions[2].witness_second.x -
                        referenced_point_dimensions[2].witness_first.x) < 1.0e-9 &&
                    std::abs(referenced_point_dimensions[2].witness_second.y -
                        referenced_point_dimensions[2].witness_first.y) < 1.0e-9 &&
                    std::abs(referenced_point_dimensions[2].line_first.y -
                        referenced_point_dimensions[2].witness_first.y) < 1.0e-9 &&
                    std::abs(referenced_point_dimensions[2].line_second.y -
                        referenced_point_dimensions[2].witness_second.y) < 1.0e-9 &&
                    std::abs(referenced_point_dimensions[2].line_first.x -
                        referenced_point_dimensions[2].witness_first.x) > 1.0e-9,
                "Referenced Point did not expose three axis-aligned offset "
                "dimensions with Z in a stable XZ plane");

        auto zero_offset_point = dimensioned_point;
        for (auto& reference : zero_offset_point.references) {
            reference.offset = 0.0;
        }
        require(zima::document::construction_point_dimensions(
                    zero_offset_point, document_origin_geometry).empty(),
                "Zero reference offsets still produced viewer dimensions");

        auto partially_referenced_point = plane_constrained_point;
        partially_referenced_point.references = {
            {{}, constructions.document_id + ":origin", "origin:plane:xy",
                2.0, true}};
        require(zima::document::resolve_construction(
                    partially_referenced_point, document_origin_geometry),
                "Partially referenced Point fixture did not resolve");
        const auto partial_dimensions =
            zima::document::construction_point_dimensions(
                partially_referenced_point, document_origin_geometry);
        require(partial_dimensions.size() == 3 &&
                    partial_dimensions[0].reference.semantic_key == "parameter:x" &&
                    partial_dimensions[1].reference.semantic_key == "parameter:y" &&
                    partial_dimensions[2].reference.semantic_key ==
                        "parameter:reference_offset:0",
                "Point dimensions did not automatically exchange constrained "
                "absolute Z for the editable reference offset");

        auto absolute_dimensioned_point =
            zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Point);
        absolute_dimensioned_point.origin = {1.0, 2.0, 3.0};
        const auto absolute_dimensions =
            zima::document::construction_point_dimensions(
                absolute_dimensioned_point, document_origin_geometry);
        require(absolute_dimensions.size() == 3 &&
                    absolute_dimensions[0].reference.semantic_key == "parameter:x" &&
                    absolute_dimensions[1].reference.semantic_key == "parameter:y" &&
                    absolute_dimensions[2].reference.semantic_key == "parameter:z" &&
                    absolute_dimensions[0].label_prefix.empty() &&
                    absolute_dimensions[1].label_prefix.empty() &&
                    absolute_dimensions[2].label_prefix.empty() &&
                    absolute_dimensions[0].plane_normal ==
                        zima::kernel::Vec3{1.0, 0.0, 0.0} &&
                    absolute_dimensions[1].plane_normal ==
                        zima::kernel::Vec3{0.0, 1.0, 0.0} &&
                    absolute_dimensions[2].plane_normal ==
                        zima::kernel::Vec3{0.0, 0.0, 1.0} &&
                    std::abs(absolute_dimensions[2].line_first.y -
                        absolute_dimensions[2].witness_first.y) < 1.0e-9 &&
                    std::abs(absolute_dimensions[2].line_second.y -
                        absolute_dimensions[2].witness_second.y) < 1.0e-9,
                "Absolute Point did not restore all three coordinate dimensions "
                "with Z in a stable XZ plane");
        zima::document::Placement container_placement;
        container_placement.x = 1.0;
        container_placement.y = 2.0;
        container_placement.z = 3.0;
        const auto container_dimensions =
            zima::document::container_placement_dimensions(
                "container", container_placement, document_origin_geometry);
        require(container_dimensions.size() == 3 &&
                    container_dimensions[0].reference.semantic_key ==
                        "parameter:placement:x" &&
                    container_dimensions[1].reference.semantic_key ==
                        "parameter:placement:y" &&
                    container_dimensions[2].reference.semantic_key ==
                        "parameter:placement:z" &&
                    container_dimensions[0].label_prefix.empty() &&
                    container_dimensions[1].label_prefix.empty() &&
                    container_dimensions[2].label_prefix.empty(),
                "Universal container placement did not expose numeric-only "
                "editable coordinate dimensions");
        auto rotated_container_placement = container_placement;
        rotated_container_placement.absolute_rotation_y = 35.0;
        rotated_container_placement.rotation_y = 35.0;
        const auto rotated_container_dimensions =
            zima::document::container_placement_dimensions(
                "container", rotated_container_placement,
                document_origin_geometry);
        const auto& rotation_dimension =
            rotated_container_dimensions.back();
        const double rotation_radius = std::hypot(std::hypot(
            rotation_dimension.line_first.x -
                rotation_dimension.witness_first.x,
            rotation_dimension.line_first.y -
                rotation_dimension.witness_first.y),
            rotation_dimension.line_first.z -
                rotation_dimension.witness_first.z);
        require(rotated_container_dimensions.size() == 4 &&
                    rotation_dimension.reference.semantic_key ==
                        "parameter:placement:rotation_y" &&
                    rotation_dimension.kind ==
                        zima::kernel::ViewerDimensionKind::Angular &&
                    rotation_dimension.label_prefix == "RY = " &&
                    rotation_dimension.unit_suffix == " °" &&
                    std::abs(rotation_dimension.value - 35.0) < 1.0e-9 &&
                    std::abs(rotation_dimension.sweep_degrees - 35.0) <
                        1.0e-9 &&
                    std::abs(rotation_radius - 22.0) < 1.0e-9,
                "Universal container placement did not expose its nonzero "
                "angle in the separated angular dimension band");
        auto oriented_correction = container_placement;
        oriented_correction.references = {
            {{}, constructions.document_id + ":origin", "origin:plane:xz",
                0.0, true, "front", true, true},
            {{}, constructions.document_id + ":origin", "origin:plane:xy",
                0.0, true, "top", true, true}};
        oriented_correction.orientation_back = true;
        oriented_correction.orientation_quarter_turns = 1;
        oriented_correction.rotation_offset_y = 15.0;
        oriented_correction.rotation_offset_z = -10.0;
        require(zima::document::resolve_placement(
                    oriented_correction, document_origin_geometry),
                "Oriented correction dimension fixture did not resolve");
        const auto oriented_dimensions =
            zima::document::container_placement_dimensions(
                "container", oriented_correction, document_origin_geometry);
        const auto correction_y = std::ranges::find_if(
            oriented_dimensions, [](const auto& dimension) {
                return dimension.reference.semantic_key ==
                    "parameter:placement:rotation_y";
            });
        const auto correction_z = std::ranges::find_if(
            oriented_dimensions, [](const auto& dimension) {
                return dimension.reference.semantic_key ==
                    "parameter:placement:rotation_z";
            });
        require(correction_y != oriented_dimensions.end() &&
                    correction_z != oriented_dimensions.end() &&
                    std::abs(correction_y->plane_normal.x + 1.0) < 1.0e-9 &&
                    std::abs(correction_y->plane_normal.y) < 1.0e-9 &&
                    std::abs(correction_y->plane_normal.z) < 1.0e-9 &&
                    std::abs(correction_z->plane_normal.x) < 1.0e-9 &&
                    std::abs(correction_z->plane_normal.y) < 1.0e-9 &&
                    std::abs(correction_z->plane_normal.z + 1.0) < 1.0e-9,
                "FRONT/BACK and quarter-turn controls did not rotate the "
                "correction-dimension planes with the local container frame");
        container_placement.references = dimensioned_point.references;
        container_placement.x = dimensioned_point.origin.x;
        container_placement.y = dimensioned_point.origin.y;
        container_placement.z = dimensioned_point.origin.z;
        const auto referenced_container_dimensions =
            zima::document::container_placement_dimensions(
                "container", container_placement, document_origin_geometry);
        require(referenced_container_dimensions.size() == 3 &&
                    referenced_container_dimensions[0].reference.semantic_key ==
                        "parameter:placement:reference_offset:0" &&
                    referenced_container_dimensions[1].reference.semantic_key ==
                        "parameter:placement:reference_offset:1" &&
                    referenced_container_dimensions[2].reference.semantic_key ==
                        "parameter:placement:reference_offset:2",
                "Universal container placement did not exchange constrained "
                "coordinates for its three editable reference-offset dimensions");
        auto rank_geometry = document_origin_geometry;
        rank_geometry.axes.push_back({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 10.0,
            {"parallel-a", "axis", {}}});
        rank_geometry.axes.push_back({{0.0, 5.0, 0.0}, {1.0, 0.0, 0.0}, 10.0,
            {"parallel-b", "axis", {}}});
        rank_geometry.edges.push_back({
            {{0.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {2.0, 0.0, 0.0}},
            {"curved-edge", "edge", {}}, false, false});
        require(zima::document::point_constraint_remaining_dof({
                    {{}, "parallel-a", "axis"}}, rank_geometry) == 1 &&
                zima::document::point_constraint_remaining_dof({
                    {{}, "parallel-a", "axis"},
                    {{}, "parallel-b", "axis"}}, rank_geometry) == 1 &&
                zima::document::point_constraint_remaining_dof({
                    {{}, "curved-edge", "edge"}}, rank_geometry) == 3,
                "Point placement counted redundant or curved references as new DOF");
        constructions.resolve_constructions();
        require(constructions.constructions.front().reference_valid &&
                    std::abs(constructions.constructions.front().origin.x) < 1.0e-5 &&
                    std::abs(constructions.constructions.front().origin.y) < 1.0e-5 &&
                    std::abs(constructions.constructions.front().origin.z) < 1.0e-5,
                "Construction Point did not solve three selected planar references");
        constructions.constructions.front().references.front().offset = 12.0;
        constructions.resolve_constructions();
        require(constructions.constructions.front().reference_valid &&
                    std::abs(constructions.constructions.front().origin.x - 12.0) <
                        1.0e-5,
                "Construction Point ignored its planar reference offset");
        constructions.constructions.clear();
        auto point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        point.origin = {1.0, 2.0, 3.0};
        auto axis = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        // Z direction (not X): the Plane container created below has its
        // zero-rotation normal along X, so the shared external-reference
        // sketch further down uses the YZ plane to view it face-on. An
        // axis running along X would then be exactly edge-on to that view
        // (a degenerate, zero-length 2D projection), so this axis must lie
        // in the YZ plane instead.
        axis.direction = {0.0, 0.0, 1.0};
        auto plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        plane.display_size = 75.0;
        constructions.constructions = {point, axis, plane};
        const auto construction_mesh = constructions.construction_viewer_mesh();
        // Point, Axis and Plane each expose their own defining point through
        // a distinct persisted Container-Origin owner.
        require(construction_mesh.points.size() == 3 &&
                    construction_mesh.axes.size() == 1 &&
                    construction_mesh.edges.size() == 1 &&
                    construction_mesh.original_references.points.size() == 3 &&
                    construction_mesh.original_references.points.front().reference.owner_id ==
                        point.id + ":origin" &&
                    construction_mesh.original_references.points.front()
                            .reference.semantic_key == "point" &&
                    construction_mesh.original_references.points[1].reference.owner_id ==
                        axis.id + ":origin" &&
                    construction_mesh.original_references.points[1]
                            .reference.semantic_key == "point" &&
                    construction_mesh.original_references.points.back().reference.owner_id ==
                        plane.id + ":origin" &&
                    construction_mesh.original_references.points.back()
                            .reference.semantic_key == "point" &&
                    construction_mesh.original_references.axes.size() == 1 &&
                    construction_mesh.original_references.axes.front().reference.owner_id ==
                        axis.entity_id &&
                    construction_mesh.original_references.triangle_references.size() == 2,
                "Construction objects did not produce persisted ZIMA references");
        auto curve_document = zima::document::PartDocument::create_default();
        auto curve = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Curve3D);
        curve.origin = {5.0, 6.0, 7.0};
        curve.curve_type =
            zima::document::Curve3DType::InterpolatingSpline;
        auto curve_first = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        curve_first.parent_construction_id = curve.id;
        curve_first.origin = {0.0, 0.0, 0.0};
        curve_first.curve_tangent =
            zima::document::Curve3DTangentMode::PositiveY;
        curve_first.curve_tangent_enabled = true;
        auto curve_second = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        curve_second.parent_construction_id = curve.id;
        curve_second.origin = {10.0, 0.0, 0.0};
        curve_second.curve_tangent =
            zima::document::Curve3DTangentMode::NegativeY;
        curve_second.curve_tangent_enabled = true;
        const auto curve_id = curve.id;
        const auto curve_first_id = curve_first.id;
        const auto curve_second_id = curve_second.id;
        curve.curve_points = {curve_first, curve_second};
        curve_document.constructions.push_back(curve);
        curve_document.insert_history_entry(
            zima::document::PartHistoryKind::Construction, curve.id);
        curve_document.resolve_constructions();
        const auto curve_mesh = curve_document.construction_viewer_mesh();
        const auto edited_curve_mesh =
            curve_document.construction_viewer_mesh(curve_id);
        const auto edited_curve_point_mesh =
            curve_document.construction_viewer_mesh(curve_first_id);
        const auto has_editing_axis = [](const auto& mesh,
                                         const std::string& owner_id) {
            return std::ranges::any_of(mesh.axes, [&](const auto& axis) {
                return axis.reference.owner_id == owner_id &&
                    axis.reference.semantic_key == "origin:axis:x";
            }) && std::ranges::any_of(
                mesh.original_references.axes, [&](const auto& axis) {
                    return axis.reference.owner_id == owner_id &&
                        axis.reference.semantic_key == "origin:axis:x";
                });
        };
        require(curve_mesh.points.empty() && curve_mesh.edges.size() == 1 &&
                    curve_mesh.edges.front().points.size() == 25 &&
                    std::abs(curve_mesh.edges.front().points.front().x - 5.0) < 1.0e-9 &&
                    std::abs(curve_mesh.edges.front().points.front().y - 6.0) < 1.0e-9 &&
                    std::abs(curve_mesh.edges.front().points.back().x - 15.0) < 1.0e-9 &&
                    curve_mesh.edges.front().points[1].y > 6.0 &&
                    !curve_mesh.edges.front().construction &&
                    curve_mesh.edges.front().overlay &&
                    curve_mesh.edges.front().display_owner_id == curve_id &&
                    curve_mesh.edges.front().reference.semantic_key ==
                        "curve:segment:" + curve_first_id + ":" + curve_second_id &&
                    edited_curve_mesh.points.size() == 3 &&
                    has_editing_axis(
                        edited_curve_point_mesh, curve.container_origin.id) &&
                    has_editing_axis(edited_curve_point_mesh,
                        curve_first.container_origin.id),
                "3D Curve did not keep normal View leaf-only while exposing editing Points");
        auto automatic_curve_document = curve_document;
        auto* automatic_curve = automatic_curve_document.find_construction(curve_id);
        require(automatic_curve != nullptr,
            "3D Curve disappeared before automatic-tangent verification");
        for (auto& point : automatic_curve->curve_points)
            point.curve_tangent_enabled = false;
        automatic_curve_document.resolve_constructions();
        const auto automatic_curve_mesh =
            automatic_curve_document.construction_viewer_mesh();
        require(automatic_curve_mesh.edges.size() == 1 &&
                    std::ranges::all_of(
                        automatic_curve_mesh.edges.front().points,
                        [](const auto& point) {
                            return std::abs(point.y - 6.0) < 1.0e-9;
                        }),
                "Disabled 3D Curve directions still constrained interpolation tangents");
        auto natural_curve_document =
            zima::document::PartDocument::create_default();
        auto natural_curve =
            zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Curve3D);
        natural_curve.curve_type =
            zima::document::Curve3DType::InterpolatingSpline;
        for (const auto position : std::array{
                 zima::kernel::Vec3{0.0, 100.0, 26.353700913074114},
                 zima::kernel::Vec3{-100.0, 0.0, 60.0},
                 zima::kernel::Vec3{0.0, 0.0, 60.0}}) {
            auto child = zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Point);
            child.parent_construction_id = natural_curve.id;
            child.origin = position;
            child.curve_tangent_enabled = false;
            natural_curve.curve_points.push_back(std::move(child));
        }
        natural_curve_document.constructions.push_back(natural_curve);
        natural_curve_document.resolve_constructions();
        const auto natural_curve_mesh =
            natural_curve_document.construction_viewer_mesh();
        const auto& natural_last_span = natural_curve_mesh.edges.back().points;
        const auto& natural_last_point =
            natural_curve.curve_points.back().origin;
        require(natural_curve_mesh.edges.size() == 2 &&
                    natural_last_span.size() == 25 &&
                    std::abs(natural_last_span.back().x -
                        natural_last_point.x) < 1.0e-9 &&
                    std::abs(natural_last_span.back().y -
                        natural_last_point.y) < 1.0e-9 &&
                    std::abs(natural_last_span.back().z -
                        natural_last_point.z) < 1.0e-9 &&
                    std::abs(natural_last_span.back().y -
                        natural_last_span[natural_last_span.size()-2].y) >
                        1.0e-4,
                "Automatic 3D spline did not terminate exactly at the last "
                "Point with its global natural end condition");
        curve_document.find_construction(curve_id)
            ->curve_points[1].curve_tangent_enabled = false;
        const auto curve_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-curve3d-contract.prtz";
        curve_document.save(curve_path);
        const auto loaded_curve_document =
            zima::document::PartDocument::load(curve_path);
        std::filesystem::remove(curve_path);
        const auto* loaded_curve =
            loaded_curve_document.find_construction(curve_id);
        require(loaded_curve != nullptr &&
                    loaded_curve->kind ==
                        zima::document::ConstructionKind::Curve3D &&
                    loaded_curve->curve_type ==
                        zima::document::Curve3DType::InterpolatingSpline &&
                    loaded_curve->curve_points.size() == 2 &&
                    loaded_curve->curve_points[0].id == curve_first_id &&
                    loaded_curve->curve_points[1].id == curve_second_id &&
                    loaded_curve->curve_points[0].parent_construction_id == curve_id &&
                    loaded_curve->curve_points[0].curve_tangent ==
                        zima::document::Curve3DTangentMode::PositiveY &&
                    loaded_curve->curve_points[0].curve_tangent_enabled &&
                    loaded_curve->curve_points[1].curve_tangent ==
                        zima::document::Curve3DTangentMode::NegativeY &&
                    !loaded_curve->curve_points[1].curve_tangent_enabled,
                "3D Curve did not preserve point order, ownership, tangent axes and switches");

        auto experimental_document =
            zima::document::PartDocument::create_default();
        auto experimental_curve =
            zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Curve3DExperimental);
        experimental_curve.curve_type =
            zima::document::Curve3DType::InterpolatingSpline;
        for (const auto& position : std::array{
                 zima::kernel::Vec3{0.0, 0.0, 0.0},
                 zima::kernel::Vec3{10.0, 10.0, 0.0},
                 zima::kernel::Vec3{20.0, 0.0, 5.0}}) {
            auto child = zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Point);
            child.parent_construction_id = experimental_curve.id;
            child.origin = position;
            experimental_curve.curve_points.push_back(std::move(child));
        }
        const auto experimental_generator = zima::kernel::make_stable_id();
        for (std::size_t index = 0; index + 1 <
                experimental_curve.curve_points.size(); ++index) {
            zima::document::Curve3DConnection connection;
            connection.id = zima::kernel::make_stable_id();
            connection.generator_id = experimental_generator;
            connection.parent_construction_id = experimental_curve.id;
            connection.start_point_id =
                experimental_curve.curve_points[index].id;
            connection.end_point_id =
                experimental_curve.curve_points[index + 1].id;
            connection.type = zima::document::Curve3DConnectionType::
                InterpolatingSpline;
            experimental_curve.curve_connections.push_back(
                std::move(connection));
        }
        const auto experimental_solution =
            zima::document::solve_experimental_curve3d(experimental_curve);
        require(experimental_solution.valid &&
                    experimental_solution.primitives.size() == 2 &&
                    experimental_solution.primitives[0].generator_id ==
                        experimental_generator &&
                    experimental_solution.primitives[1].generator_id ==
                        experimental_generator &&
                    experimental_solution.primitives[0].points.size() == 25,
                "Experimental global spline did not solve as one persisted generator");
        auto influenced_curve = experimental_curve;
        influenced_curve.curve_points.back().origin.y = 20.0;
        const auto influenced_solution =
            zima::document::solve_experimental_curve3d(influenced_curve);
        require(influenced_solution.valid &&
                    std::abs(influenced_solution.primitives[0].points[12].y -
                        experimental_solution.primitives[0].points[12].y) >
                        1.0e-6,
                "Experimental spline was calculated per interval instead of globally");

        auto sketch_curve = experimental_curve;
        sketch_curve.curve_points.resize(2);
        sketch_curve.curve_connections.resize(1);
        auto& sketch_connection = sketch_curve.curve_connections.front();
        sketch_connection.type =
            zima::document::Curve3DConnectionType::Sketch;
        sketch_connection.generator_id = sketch_connection.id;
        auto trajectory_sketch = zima::sketcher::Sketch::create_default();
        trajectory_sketch.owner_container_id = sketch_curve.id;
        trajectory_sketch.plane_reference_owner_id =
            "trajectory-frame:" + sketch_connection.id;
        const double diagonal = std::sqrt(200.0);
        trajectory_sketch.resolved_origin = {0.0, 0.0, 0.0};
        trajectory_sketch.resolved_x_axis = {
            1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0), 0.0};
        trajectory_sketch.resolved_y_axis = {0.0, 0.0, 1.0};
        trajectory_sketch.resolved_normal = {
            1.0 / std::sqrt(2.0), -1.0 / std::sqrt(2.0), 0.0};
        const auto trajectory_start = trajectory_sketch.add_point(0.0, 0.0);
        const auto trajectory_end = trajectory_sketch.add_point(diagonal, 0.0);
        trajectory_sketch.set_point_fixed(trajectory_start, true);
        trajectory_sketch.set_point_fixed(trajectory_end, true);
        static_cast<void>(trajectory_sketch.add_segment(
            0.0, 0.0, diagonal, 0.0));
        sketch_connection.sketch_id = trajectory_sketch.id;
        sketch_connection.sketch_start_point_id = trajectory_start;
        sketch_connection.sketch_end_point_id = trajectory_end;
        sketch_connection.sketch_plane_valid = true;
        sketch_connection.sketch_serialized = trajectory_sketch.serialized();
        const auto trajectory_sketch_solution =
            zima::document::solve_experimental_curve3d(sketch_curve);
        require(trajectory_sketch_solution.valid &&
                    trajectory_sketch_solution.primitives.size() == 1 &&
                    trajectory_sketch_solution.primitives.front().semantic_key ==
                        "trajectory:sketch:" + sketch_connection.generator_id,
                "A connected START-END trajectory Sketch was not solved");

        auto branched_sketch = zima::sketcher::Sketch::create_default();
        branched_sketch.owner_container_id = sketch_curve.id;
        branched_sketch.plane_reference_owner_id =
            trajectory_sketch.plane_reference_owner_id;
        branched_sketch.resolved_origin = trajectory_sketch.resolved_origin;
        branched_sketch.resolved_x_axis = trajectory_sketch.resolved_x_axis;
        branched_sketch.resolved_y_axis = trajectory_sketch.resolved_y_axis;
        branched_sketch.resolved_normal = trajectory_sketch.resolved_normal;
        const auto branch_start = branched_sketch.add_point(0.0, 0.0);
        static_cast<void>(branched_sketch.add_point(diagonal * 0.5, 0.0));
        const auto branch_end = branched_sketch.add_point(diagonal, 0.0);
        static_cast<void>(branched_sketch.add_point(diagonal * 0.5, 5.0));
        branched_sketch.set_point_fixed(branch_start, true);
        branched_sketch.set_point_fixed(branch_end, true);
        static_cast<void>(branched_sketch.add_segment(
            0.0, 0.0, diagonal * 0.5, 0.0));
        static_cast<void>(branched_sketch.add_segment(
            diagonal * 0.5, 0.0, diagonal, 0.0));
        static_cast<void>(branched_sketch.add_segment(
            diagonal * 0.5, 0.0, diagonal * 0.5, 5.0));
        sketch_connection.sketch_id = branched_sketch.id;
        sketch_connection.sketch_start_point_id = branch_start;
        sketch_connection.sketch_end_point_id = branch_end;
        sketch_connection.sketch_serialized = branched_sketch.serialized();
        require(!zima::document::solve_experimental_curve3d(sketch_curve).valid,
                "A branched trajectory Sketch was accepted as one path");
        const auto experimental_id = experimental_curve.id;
        const auto experimental_point_origins =
            std::array{experimental_curve.curve_points[0].container_origin.id,
                experimental_curve.curve_points[1].container_origin.id,
                experimental_curve.curve_points[2].container_origin.id};
        experimental_document.constructions.push_back(experimental_curve);
        experimental_document.insert_history_entry(
            zima::document::PartHistoryKind::Construction, experimental_id);
        experimental_document.resolve_constructions();
        const auto experimental_edit_mesh =
            experimental_document.construction_viewer_mesh(experimental_id);
        require(std::ranges::all_of(experimental_point_origins,
                    [&](const auto& origin_id) {
                        return std::ranges::any_of(
                            experimental_edit_mesh.axes, [&](const auto& axis) {
                                return axis.reference.owner_id == origin_id &&
                                    axis.reference.semantic_key ==
                                        "origin:axis:x";
                            });
                    }),
                "3D trajectory Properties did not expose every child Point Origin");
        const auto experimental_point_references =
            experimental_document.construction_reference_geometry_for(
                experimental_curve.curve_points[1].id, {});
        require(std::ranges::all_of(experimental_point_origins,
                    [&](const auto& origin_id) {
                        const bool has_axis = std::ranges::any_of(
                            experimental_point_references.axes,
                            [&](const auto& axis) {
                                return axis.reference.owner_id == origin_id;
                            });
                        const bool has_plane = std::ranges::any_of(
                            experimental_point_references.triangle_references,
                            [&](const auto& face) {
                                return face.owner_id == origin_id &&
                                    face.semantic_key.starts_with(
                                        "origin:plane:");
                            });
                        const bool has_point = std::ranges::any_of(
                            experimental_point_references.points,
                            [&](const auto& point) {
                                return point.reference.owner_id == origin_id &&
                                    point.reference.semantic_key == "point";
                            });
                        return has_point && has_axis && has_plane;
                    }),
                "Sibling Point Origins were visible but their Point was not "
                "available to the reference picker");
        require(zima::document::point_constraint_remaining_dof({
                    {{}, experimental_point_origins[0], "point"}},
                    experimental_point_references) == 0,
                "A picked experimental-trajectory Point did not fully define "
                "the nested Point Properties position");
        const auto experimental_path =
            std::filesystem::temp_directory_path() /
            "zima-cad-cpp-experimental-curve3d-contract.prtz";
        experimental_document.save(experimental_path);
        const auto loaded_experimental_document =
            zima::document::PartDocument::load(experimental_path);
        std::filesystem::remove(experimental_path);
        const auto* loaded_experimental =
            loaded_experimental_document.find_construction(experimental_id);
        require(loaded_experimental != nullptr &&
                    loaded_experimental->kind ==
                        zima::document::ConstructionKind::Curve3DExperimental &&
                    loaded_experimental->curve_type ==
                        zima::document::Curve3DType::InterpolatingSpline &&
                    loaded_experimental->curve_connections.size() == 2 &&
                    loaded_experimental->curve_connections[0].generator_id ==
                        experimental_generator &&
                    loaded_experimental->curve_connections[1].generator_id ==
                        experimental_generator,
                "Experimental 3D trajectory lost its stable interval model on save/load");
        auto solid_origin_document =
            zima::document::PartDocument::create_default();
        auto solid_origin_box =
            zima::document::PartDocument::create_box_container();
        solid_origin_box.placement = {11.0, 12.0, 13.0};
        const auto solid_origin_owner = solid_origin_box.id;
        solid_origin_document.history.push_back(std::move(solid_origin_box));
        const auto solid_origin_mesh =
            solid_origin_document.construction_viewer_mesh();
        require(solid_origin_mesh.points.size() == 1 &&
                    solid_origin_mesh.points.front().reference.owner_id ==
                        solid_origin_owner &&
                    solid_origin_mesh.points.front().reference.semantic_key ==
                        "container:origin-marker" &&
                    solid_origin_mesh.points.front().position.x == 11.0 &&
                    solid_origin_mesh.points.front().position.y == 12.0 &&
                    solid_origin_mesh.points.front().position.z == 13.0,
                "Basic solid did not publish its persisted placement-origin marker");
        auto linked_origin_document =
            zima::document::PartDocument::create_default();
        auto linked_origin_parent =
            zima::document::PartDocument::create_box_container();
        linked_origin_parent.placement = {11.0, 12.0, 13.0};
        const auto linked_parent_id = linked_origin_parent.id;
        auto linked_origin_child =
            zima::document::PartDocument::create_cylinder_container();
        linked_origin_child.placement.references = {
            {{}, linked_origin_parent.container_origin.id, "origin:plane:xz",
                0.0, true, "front", true},
            {{}, linked_origin_parent.container_origin.id, "origin:plane:xy",
                0.0, true, "top", true},
            {{}, linked_origin_parent.container_origin.id, "origin:plane:yz",
                0.0, true}};
        const auto linked_child_id = linked_origin_child.id;
        linked_origin_document.history.push_back(linked_origin_parent);
        linked_origin_document.history.push_back(linked_origin_child);
        linked_origin_document.resolve_constructions();
        const auto* resolved_linked_child =
            linked_origin_document.find_container(linked_child_id);
        require(resolved_linked_child != nullptr &&
                    resolved_linked_child->placement.reference_valid &&
                    std::abs(resolved_linked_child->placement.x - 11.0) < 1.0e-6 &&
                    std::abs(resolved_linked_child->placement.y - 12.0) < 1.0e-6 &&
                    std::abs(resolved_linked_child->placement.z - 13.0) < 1.0e-6,
                "History container did not follow an earlier container Origin");
        const auto linked_dialog_geometry = linked_origin_document
            .history_origin_reference_geometry_before(linked_child_id);
        const auto linked_translation_state =
            zima::document::point_constraint_state(
                resolved_linked_child->placement.references,
                linked_dialog_geometry);
        const auto linked_rotation_state =
            zima::document::orientation_constraint_state(
                resolved_linked_child->placement.references,
                linked_dialog_geometry, true,
                {resolved_linked_child->placement.x,
                 resolved_linked_child->placement.y,
                 resolved_linked_child->placement.z});
        require(linked_translation_state.remaining_dof == 0 &&
                    linked_rotation_state.remaining_dof == 0 &&
                    std::ranges::all_of(
                        linked_translation_state.constrained_axes,
                        [](bool constrained) { return constrained; }) &&
                    std::ranges::all_of(
                        linked_rotation_state.constrained_axes,
                        [](bool constrained) { return constrained; }),
                "Live dialog geometry did not fully constrain a linked local Origin");
        require(zima::document::container_placement_dimensions(
                    linked_child_id, resolved_linked_child->placement,
                    linked_dialog_geometry).empty(),
                "Reference-driven absolute placement dimensions remained visible");
        auto* moved_linked_parent =
            linked_origin_document.find_container(linked_parent_id);
        require(moved_linked_parent != nullptr,
                "Linked parent container disappeared");
        moved_linked_parent->placement.x = 21.0;
        moved_linked_parent->placement.y = -8.0;
        moved_linked_parent->placement.z = 4.0;
        linked_origin_document.resolve_constructions();
        resolved_linked_child =
            linked_origin_document.find_container(linked_child_id);
        require(resolved_linked_child != nullptr &&
                    std::abs(resolved_linked_child->placement.x - 21.0) < 1.0e-6 &&
                    std::abs(resolved_linked_child->placement.y + 8.0) < 1.0e-6 &&
                    std::abs(resolved_linked_child->placement.z - 4.0) < 1.0e-6,
                "Child container did not follow its moved parent Origin");
        const auto linked_origin_path = std::filesystem::temp_directory_path() /
            "zima-cad-linked-container-origin-contract.prtz";
        linked_origin_document.save(linked_origin_path);
        auto loaded_linked_origin_document =
            zima::document::PartDocument::load(linked_origin_path);
        std::filesystem::remove(linked_origin_path);
        loaded_linked_origin_document.resolve_constructions();
        const auto* loaded_linked_child =
            loaded_linked_origin_document.find_container(linked_child_id);
        require(loaded_linked_child != nullptr &&
                    loaded_linked_child->placement.references ==
                        resolved_linked_child->placement.references &&
                    std::abs(loaded_linked_child->placement.x - 21.0) < 1.0e-6 &&
                    std::abs(loaded_linked_child->placement.y + 8.0) < 1.0e-6 &&
                    std::abs(loaded_linked_child->placement.z - 4.0) < 1.0e-6,
                "Container-Origin dependency did not survive save/load");

        auto forward_origin_document =
            zima::document::PartDocument::create_default();
        auto forward_origin_first =
            zima::document::PartDocument::create_cylinder_container();
        auto forward_origin_later =
            zima::document::PartDocument::create_box_container();
        forward_origin_first.placement.references = {
            {{}, forward_origin_later.container_origin.id, "origin:plane:xz",
                0.0, true}};
        const auto forward_first_id = forward_origin_first.id;
        forward_origin_document.history.push_back(forward_origin_first);
        forward_origin_document.history.push_back(forward_origin_later);
        forward_origin_document.resolve_constructions();
        require(!forward_origin_document.find_container(forward_first_id)
                    ->placement.reference_valid,
                "A forward history-container dependency was accepted");

        auto cyclic_origin_document =
            zima::document::PartDocument::create_default();
        auto cyclic_origin_first =
            zima::document::PartDocument::create_box_container();
        auto cyclic_origin_second =
            zima::document::PartDocument::create_cylinder_container();
        cyclic_origin_first.placement.references = {
            {{}, cyclic_origin_second.container_origin.id, "origin:plane:xz",
                0.0, true}};
        cyclic_origin_second.placement.references = {
            {{}, cyclic_origin_first.container_origin.id, "origin:plane:xz",
                0.0, true}};
        const auto cyclic_first_id = cyclic_origin_first.id;
        const auto cyclic_second_id = cyclic_origin_second.id;
        cyclic_origin_document.history.push_back(cyclic_origin_first);
        cyclic_origin_document.history.push_back(cyclic_origin_second);
        cyclic_origin_document.resolve_constructions();
        require(!cyclic_origin_document.find_container(cyclic_first_id)
                     ->placement.reference_valid &&
                    !cyclic_origin_document.find_container(cyclic_second_id)
                     ->placement.reference_valid,
                "A cyclic history-container dependency was accepted");
        const auto edited_point_mesh =
            constructions.construction_viewer_mesh(point.id);
        const auto edited_point_origin_xy = std::find_if(
            edited_point_mesh.edges.begin(), edited_point_mesh.edges.end(),
            [&](const auto& edge) {
                return edge.reference.owner_id == point.id + ":origin" &&
                    edge.reference.semantic_key == "origin:plane:xy";
            });
        // Editing the Point adds its origin frame, but not a duplicate of
        // the Point entity itself; Axis and Plane keep their own markers.
        require(edited_point_mesh.points.size() == 3 &&
                    edited_point_mesh.points.front().reference.semantic_key ==
                        "point" &&
                    edited_point_mesh.axes.size() == 4 &&
                    edited_point_mesh.edges.size() == 4 &&
                    std::all_of(edited_point_mesh.original_references.axes.begin(),
                        edited_point_mesh.original_references.axes.begin() + 3,
                        [&](const auto& value) {
                            return value.reference.owner_id == point.id + ":origin";
                        }) &&
                    std::all_of(edited_point_mesh.axes.begin(),
                        edited_point_mesh.axes.begin() + 3,
                        [&](const auto& value) {
                            return edited_point_origin_xy !=
                                    edited_point_mesh.edges.end() &&
                                std::abs(value.display_length * 2.0 -
                                    polyline_segment_length(
                                        *edited_point_origin_xy)) < 1.0e-12;
                        }),
                "Edited Point did not expose its distinct Container Origin");
        auto second_point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        second_point.origin = {11.0, 2.0, 3.0};
        auto referenced_axis = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        referenced_axis.definition =
            zima::document::ConstructionDefinition::TwoPointAxis;
        referenced_axis.references = {
            {{}, point.id + ":origin", "point"},
            {{}, second_point.id + ":origin", "point"}};
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
        zima::kernel::ViewerReferenceGeometry bounded_axis_geometry;
        bounded_axis_geometry.edges.push_back({{{5.0, 0.0, 0.0},
            {0.0, 5.0, 0.0}, {-5.0, 0.0, 0.0}, {0.0, -5.0, 0.0},
            {5.0, 0.0, 0.0}}, {"radius-owner", "circle", {}}});
        const auto append_limit_plane = [&](const std::string& owner, double z) {
            const auto offset = static_cast<std::uint32_t>(
                bounded_axis_geometry.vertices.size());
            bounded_axis_geometry.vertices.insert(
                bounded_axis_geometry.vertices.end(),
                {{-10.0, -10.0, z}, {10.0, -10.0, z},
                 {10.0, 10.0, z}, {-10.0, 10.0, z}});
            bounded_axis_geometry.triangles.insert(
                bounded_axis_geometry.triangles.end(),
                {offset, offset+1, offset+2, offset, offset+2, offset+3});
            bounded_axis_geometry.triangle_references.insert(
                bounded_axis_geometry.triangle_references.end(), 2,
                {owner, "surface", {}});
        };
        append_limit_plane("limit-start", -5.0);
        append_limit_plane("limit-end", 15.0);
        auto bounded_datum_axis =
            zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Axis);
        bounded_datum_axis.references = {
            {{}, "radius-owner", "circle"},
            {{}, "limit-start", "surface"},
            {{}, "limit-end", "surface"}};
        require(zima::document::resolve_construction(
                    bounded_datum_axis, bounded_axis_geometry) &&
                    std::abs(bounded_datum_axis.origin.z - 5.0) < 1.0e-8 &&
                    std::abs(bounded_datum_axis.direction.z - 1.0) < 1.0e-8 &&
                    std::abs(bounded_datum_axis.display_size - 20.0) < 1.0e-8,
                "Circular edge plus from/to faces did not define a bounded Axis");
        auto referenced_plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        referenced_plane.definition =
            zima::document::ConstructionDefinition::PlaneReference;
        referenced_plane.references = {
            {{}, constructions.document_id + ":origin", "origin:plane:xy", 8.0, true},
            {{}, constructions.document_id + ":origin", "origin:axis:x", 0.0, false,
                "front", true, true}};
        constructions.constructions.push_back(referenced_plane);
        constructions.resolve_constructions();
        require(constructions.constructions.back().reference_valid &&
                    std::abs(constructions.constructions.back().origin.z - 8.0) <
                        1.0e-6 &&
                    constructions.constructions.back().references.size() == 2,
                "Plane reference resolution ignored its placement/orientation split");
        auto inherited_plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        inherited_plane.references = {
            {{}, plane.entity_id, "plane"}};
        inherited_plane.offset = 12.0;
        constructions.constructions.push_back(inherited_plane);
        constructions.resolve_constructions();
        const auto& resolved_inherited_plane = constructions.constructions.back();
        require(resolved_inherited_plane.reference_valid &&
                    // The CONTAINER's own origin stays exactly at the
                    // reference position (0,0,0) -- the work-plane offset
                    // must move only the rendered plane entity, not the
                    // container/its Container Origin preview.
                    std::abs(resolved_inherited_plane.origin.x) < 1.0e-6 &&
                    std::abs(resolved_inherited_plane.origin.y) < 1.0e-6 &&
                    std::abs(resolved_inherited_plane.origin.z) < 1.0e-6 &&
                    std::abs(resolved_inherited_plane.entity_origin.x - 12.0) <
                        1.0e-6 &&
                    std::abs(resolved_inherited_plane.entity_origin.y) < 1.0e-6 &&
                    std::abs(resolved_inherited_plane.entity_origin.z) < 1.0e-6 &&
                    std::abs(resolved_inherited_plane.direction.x - 1.0) < 1.0e-6 &&
                    std::abs(resolved_inherited_plane.direction.y) < 1.0e-6 &&
                    std::abs(resolved_inherited_plane.direction.z) < 1.0e-6,
                "Plane work-plane offset did not move the Plane entity (not "
                "the container) along the inherited normal");
        auto inherited_origin_plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        inherited_origin_plane.references = {
            {{}, constructions.document_id + ":origin", "origin:plane:xz"}};
        inherited_origin_plane.offset = 6.0;
        constructions.constructions.push_back(inherited_origin_plane);
        constructions.resolve_constructions();
        const auto& resolved_inherited_origin_plane = constructions.constructions.back();
        require(resolved_inherited_origin_plane.reference_valid &&
                    // Container origin stays at the reference position (the
                    // XZ origin plane passes through the world origin);
                    // the 6.0 offset must only move `entity_origin`.
                    std::abs(resolved_inherited_origin_plane.origin.x) < 1.0e-6 &&
                    std::abs(resolved_inherited_origin_plane.origin.y) < 1.0e-6 &&
                    std::abs(resolved_inherited_origin_plane.origin.z) < 1.0e-6 &&
                    std::abs(resolved_inherited_origin_plane.entity_origin.x) <
                        1.0e-6 &&
                    std::abs(resolved_inherited_origin_plane.entity_origin.y - 6.0) <
                        1.0e-6 &&
                    std::abs(resolved_inherited_origin_plane.entity_origin.z) <
                        1.0e-6 &&
                    std::abs(resolved_inherited_origin_plane.direction.x) < 1.0e-6 &&
                    std::abs(resolved_inherited_origin_plane.direction.y - 1.0) < 1.0e-6 &&
                    std::abs(resolved_inherited_origin_plane.direction.z) < 1.0e-6,
                "Plane did not inherit frame from the built-in Origin plane");
        auto origin_bulk_fill_plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        origin_bulk_fill_plane.references = {
            {{}, constructions.document_id + ":origin", "origin:plane:xz", 0.0,
                false, "front", true},
            {{}, constructions.document_id + ":origin", "origin:plane:xy", 0.0,
                false, "top", true},
            {{}, constructions.document_id + ":origin", "origin:plane:yz"}};
        constructions.constructions.push_back(origin_bulk_fill_plane);
        constructions.resolve_constructions();
        const auto& resolved_origin_bulk_fill_plane = constructions.constructions.back();
        // The whole-Origin bulk-fill IS a special "always identity" case:
        // whichever origin datum plane happens to land in row 0 first as an
        // automatically assigned FRONT role is an accidental artifact of
        // click order (the tree's "Počátek" node bulk-fills all three at
        // once), not a deliberate "parallel to this one plane" pick. So a
        // Plane resolved from the completed origin triad ignores that
        // auto-assigned FRONT role and lands on the identity frame, exactly
        // like every other kind (Point/Axis) already does for the same
        // bulk-filled triad below.
        require(resolved_origin_bulk_fill_plane.reference_valid &&
                    std::abs(resolved_origin_bulk_fill_plane.origin.x) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_plane.origin.y) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_plane.origin.z) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_plane.direction.x - 1.0) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_plane.direction.y) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_plane.direction.z) < 1.0e-6,
                "Origin bulk-fill Plane did not resolve to the identity frame");
        auto origin_bulk_fill_point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        // Simulates the transient preview state during incremental
        // "Počátek" bulk-fill entry: the first picked origin plane may have
        // already produced a temporary non-identity rotation before the full
        // xy/yz/xz triad is complete. The completed bulk-fill must
        // overwrite that stale intermediate value back to the identity.
        origin_bulk_fill_point.rotation = {37.0, -18.0, 92.0};
        origin_bulk_fill_point.references = {
            {{}, constructions.document_id + ":origin", "origin:plane:xz", 0.0,
                false, "front", true},
            {{}, constructions.document_id + ":origin", "origin:plane:xy", 0.0,
                false, "top", true},
            {{}, constructions.document_id + ":origin", "origin:plane:yz"}};
        constructions.constructions.push_back(origin_bulk_fill_point);
        constructions.resolve_constructions();
        const auto& resolved_origin_bulk_fill_point = constructions.constructions.back();
        require(resolved_origin_bulk_fill_point.reference_valid &&
                    std::abs(resolved_origin_bulk_fill_point.origin.x) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_point.origin.y) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_point.origin.z) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_point.rotation.x) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_point.rotation.y) < 1.0e-6 &&
                    std::abs(resolved_origin_bulk_fill_point.rotation.z) < 1.0e-6,
                "Origin bulk-fill Point did not clear its stale intermediate rotation");
        const auto point_origin_mesh = constructions.construction_viewer_mesh(
            resolved_origin_bulk_fill_point.id);
        const auto require_axis_direction = [&](std::string_view semantic,
                                               zima::kernel::Vec3 expected) {
            const auto axis = std::find_if(point_origin_mesh.axes.begin(),
                point_origin_mesh.axes.end(), [&](const auto& candidate) {
                    return candidate.reference.owner_id ==
                            resolved_origin_bulk_fill_point.container_origin.id &&
                        candidate.reference.semantic_key == semantic;
                });
            require(axis != point_origin_mesh.axes.end() &&
                        std::abs(axis->direction.x - expected.x) < 1.0e-6 &&
                        std::abs(axis->direction.y - expected.y) < 1.0e-6 &&
                        std::abs(axis->direction.z - expected.z) < 1.0e-6,
                    "Origin bulk-fill Point editing triad axes were permuted");
        };
        require_axis_direction("origin:axis:x", {1.0, 0.0, 0.0});
        require_axis_direction("origin:axis:y", {0.0, 1.0, 0.0});
        require_axis_direction("origin:axis:z", {0.0, 0.0, 1.0});
        // Universal container placement: any HistoryContainer (not only a
        // standalone Point/Axis/Plane) can be positioned by a reference and
        // oriented by a FRONT/TOP reference pair, mirroring the Python
        // reference implementation's `_plane_reference_rotation` /
        // `_rotation_with_local_offset` frame math bit-for-bit.
        {
            zima::document::Placement identity_placement;
            identity_placement.references = {
                {{}, constructions.document_id + ":origin", "origin:point"},
                {{}, constructions.document_id + ":origin", "origin:axis:y", 0.0,
                    false, "front", true},
                {{}, constructions.document_id + ":origin", "origin:axis:z", 0.0,
                    false, "top", true}};
            require(zima::document::resolve_placement(
                        identity_placement, document_origin_geometry) &&
                        std::abs(identity_placement.x) < 1.0e-9 &&
                        std::abs(identity_placement.y) < 1.0e-9 &&
                        std::abs(identity_placement.z) < 1.0e-9 &&
                        std::abs(identity_placement.rotation_x) < 1.0e-6 &&
                        std::abs(identity_placement.rotation_y) < 1.0e-6 &&
                        std::abs(identity_placement.rotation_z) < 1.0e-6,
                    "Container placement did not resolve the identity FRONT=Y/TOP=Z frame");

            auto cylinder_end_geometry =
                cylinder_boundaries.front().mesh.original_references;
            const auto append_reference_geometry = [](auto& target,
                                                       const auto& source) {
                const auto vertex_offset =
                    static_cast<std::uint32_t>(target.vertices.size());
                target.vertices.insert(target.vertices.end(),
                    source.vertices.begin(), source.vertices.end());
                for (const auto index : source.triangles) {
                    target.triangles.push_back(vertex_offset + index);
                }
                target.triangle_references.insert(
                    target.triangle_references.end(),
                    source.triangle_references.begin(),
                    source.triangle_references.end());
                target.edges.insert(target.edges.end(),
                    source.edges.begin(), source.edges.end());
                target.points.insert(target.points.end(),
                    source.points.begin(), source.points.end());
                target.axes.insert(target.axes.end(),
                    source.axes.begin(), source.axes.end());
            };
            append_reference_geometry(
                cylinder_end_geometry, document_origin_geometry);
            zima::document::Placement cylinder_end_placement;
            cylinder_end_placement.references = {
                {{}, "cylinder", "z_max", 0.0, true, "front", true},
                {{}, constructions.document_id + ":origin", "origin:plane:xz",
                    0.0, true, "top", true},
                {{}, constructions.document_id + ":origin", "origin:plane:yz",
                    0.0, true, "none", false}};
            require(zima::document::resolve_placement(
                        cylinder_end_placement, cylinder_end_geometry) &&
                        std::abs(cylinder_end_placement.x) < 1.0e-7 &&
                        std::abs(cylinder_end_placement.y) < 1.0e-7 &&
                        std::abs(cylinder_end_placement.z - cylinder.height) < 1.0e-7,
                "Cylinder end plus Origin planes did not locate a primitive container");
            const auto rotate = [](zima::kernel::Vec3 value,
                                   const zima::document::Placement& placement) {
                constexpr double radians = std::numbers::pi / 180.0;
                const double cx = std::cos(placement.rotation_x * radians);
                const double sx = std::sin(placement.rotation_x * radians);
                const double cy = std::cos(placement.rotation_y * radians);
                const double sy = std::sin(placement.rotation_y * radians);
                const double cz = std::cos(placement.rotation_z * radians);
                const double sz = std::sin(placement.rotation_z * radians);
                value = {value.x, cx * value.y - sx * value.z,
                    sx * value.y + cx * value.z};
                value = {cy * value.x + sy * value.z, value.y,
                    -sy * value.x + cy * value.z};
                return zima::kernel::Vec3{cz * value.x - sz * value.y,
                    sz * value.x + cz * value.y, value.z};
            };
            const auto resolved_front = rotate(
                {0.0, 1.0, 0.0}, cylinder_end_placement);
            const auto resolved_top = rotate(
                {0.0, 0.0, 1.0}, cylinder_end_placement);
            require(resolved_front.z > 0.999 && resolved_top.y > 0.999,
                "Cylinder FRONT/TOP references did not align the local container Origin");

            zima::document::Placement origin_bulk_fill_placement;
            origin_bulk_fill_placement.references = {
                // This is the canonical order used when the whole Origin
                // node is clicked: XZ owns FRONT, XY owns TOP and YZ only
                // completes translation.
                {{}, constructions.document_id + ":origin", "origin:plane:xz", 0.0,
                    false, "front", true},
                {{}, constructions.document_id + ":origin", "origin:plane:xy", 0.0,
                    false, "top", true},
                {{}, constructions.document_id + ":origin", "origin:plane:yz"}};
            require(zima::document::resolve_placement(
                        origin_bulk_fill_placement, document_origin_geometry) &&
                        std::abs(origin_bulk_fill_placement.x) < 1.0e-9 &&
                        std::abs(origin_bulk_fill_placement.y) < 1.0e-9 &&
                        std::abs(origin_bulk_fill_placement.z) < 1.0e-9 &&
                        std::abs(origin_bulk_fill_placement.rotation_x) < 1.0e-6 &&
                        std::abs(origin_bulk_fill_placement.rotation_y) < 1.0e-6 &&
                        std::abs(origin_bulk_fill_placement.rotation_z) < 1.0e-6,
                    "Origin bulk-fill placement did not stay in the identity frame");

            auto manually_ordered_origin_placement = origin_bulk_fill_placement;
            manually_ordered_origin_placement.references = {
                {{}, constructions.document_id + ":origin", "origin:plane:yz", 0.0,
                    false, "front", true},
                {{}, constructions.document_id + ":origin", "origin:plane:xz", 0.0,
                    false, "top", true},
                {{}, constructions.document_id + ":origin", "origin:plane:xy"}};
            require(zima::document::resolve_placement(
                        manually_ordered_origin_placement,
                        document_origin_geometry),
                    "Manually ordered Origin planes did not resolve placement");
            const auto manual_front = rotate(
                {0.0, 1.0, 0.0}, manually_ordered_origin_placement);
            require(manual_front.x > 0.999,
                    "Complete Origin triad replaced its manually selected first FRONT plane");

            zima::document::PartDocument origin_sketch_document;
            origin_sketch_document.document_id = constructions.document_id;
            auto sketch_container =
                zima::document::PartDocument::create_sketch_container();
            sketch_container.placement.references = {
                {{}, constructions.document_id + ":origin", "origin:plane:xz",
                    0.0, true, "front", true},
                {{}, constructions.document_id + ":origin", "origin:plane:xy",
                    0.0, true},
                {{}, constructions.document_id + ":origin", "origin:plane:yz",
                    0.0, true, "front", true},
                // Reproduce the stale automatic orientation twin that used
                // to make the last entered plane replace row-0 FRONT.
                {{}, constructions.document_id + ":origin", "origin:plane:yz",
                    0.0, true, "front", true, true}};
            auto origin_sketch = zima::sketcher::Sketch::create_default();
            origin_sketch.owner_container_id = sketch_container.id;
            origin_sketch_document.history.push_back(sketch_container);
            origin_sketch_document.sketches.push_back(origin_sketch);
            origin_sketch_document.resolve_constructions(document_origin_geometry);
            require(std::abs(origin_sketch_document.sketches.front()
                        .resolved_normal.x) < 1.0e-6 &&
                    std::abs(origin_sketch_document.sketches.front()
                        .resolved_normal.y - 1.0) < 1.0e-6 &&
                    std::abs(origin_sketch_document.sketches.front()
                        .resolved_normal.z) < 1.0e-6,
                "Third Origin plane replaced the Sketch row-0 FRONT plane");
            const auto origin_sketch_normal =
                origin_sketch_document.sketches.front().resolved_normal;
            const auto origin_sketch_x_axis =
                origin_sketch_document.sketches.front().resolved_x_axis;
            origin_sketch_document.history.front().placement
                .orientation_quarter_turns = 1;
            origin_sketch_document.resolve_constructions(document_origin_geometry);
            const auto& quarter_turned_origin_sketch =
                origin_sketch_document.sketches.front();
            require(std::abs(quarter_turned_origin_sketch.resolved_normal.x -
                            origin_sketch_normal.x) < 1.0e-6 &&
                        std::abs(quarter_turned_origin_sketch.resolved_normal.y -
                            origin_sketch_normal.y) < 1.0e-6 &&
                        std::abs(quarter_turned_origin_sketch.resolved_normal.z -
                            origin_sketch_normal.z) < 1.0e-6,
                "ROTATE tilted a referenced Sketch away from its work plane");
            require(std::abs(
                        quarter_turned_origin_sketch.resolved_x_axis.x *
                            origin_sketch_x_axis.x +
                        quarter_turned_origin_sketch.resolved_x_axis.y *
                            origin_sketch_x_axis.y +
                        quarter_turned_origin_sketch.resolved_x_axis.z *
                            origin_sketch_x_axis.z) < 1.0e-6,
                "ROTATE did not turn the referenced Sketch axes in its plane");

            // A single placement Plane fixes only the owned Sketch's local
            // +Y/normal.  Its free absolute RY must remain a live roll around
            // the unchanged Container Origin, including before the user adds
            // another reference and completes the frame.
            zima::kernel::ViewerReferenceGeometry skew_profile_geometry;
            // Same +Y plane as XZ, but deliberately start its triangle with
            // a diagonal edge.  Profile orientation must depend on the plane
            // normal and explicit RY only, never this tessellation order.
            skew_profile_geometry.vertices = {
                {0.0, 40.0, 0.0}, {10.0, 40.0, 10.0},
                {10.0, 40.0, 0.0}};
            skew_profile_geometry.triangles = {0, 1, 2};
            skew_profile_geometry.triangle_references = {
                {"skew-profile-face", "surface", {}}};
            zima::document::PartDocument partial_sketch_document;
            partial_sketch_document.document_id = constructions.document_id;
            auto partial_sketch_container =
                zima::document::PartDocument::create_sketch_container();
            partial_sketch_container.placement.references = {
                {{}, "skew-profile-face", "surface",
                    0.0, true, "front", true}};
            auto partial_sketch = zima::sketcher::Sketch::create_default();
            partial_sketch.owner_container_id = partial_sketch_container.id;
            partial_sketch_document.history.push_back(partial_sketch_container);
            partial_sketch_document.sketches.push_back(partial_sketch);
            partial_sketch_document.resolve_constructions(skew_profile_geometry);
            const auto zero_roll_origin =
                partial_sketch_document.sketches.front().resolved_origin;
            const auto zero_roll_normal =
                partial_sketch_document.sketches.front().resolved_normal;
            const auto zero_roll_x =
                partial_sketch_document.sketches.front().resolved_x_axis;
            partial_sketch_document.history.front().placement
                .absolute_rotation_y = 30.0;
            partial_sketch_document.resolve_constructions(skew_profile_geometry);
            const auto& rolled_partial_sketch =
                partial_sketch_document.sketches.front();
            const double rolled_x_dot =
                rolled_partial_sketch.resolved_x_axis.x * zero_roll_x.x +
                rolled_partial_sketch.resolved_x_axis.y * zero_roll_x.y +
                rolled_partial_sketch.resolved_x_axis.z * zero_roll_x.z;
            require(std::abs(rolled_partial_sketch.resolved_origin.x -
                            zero_roll_origin.x) < 1.0e-7 &&
                        std::abs(rolled_partial_sketch.resolved_origin.y -
                            zero_roll_origin.y) < 1.0e-7 &&
                        std::abs(rolled_partial_sketch.resolved_origin.z -
                            zero_roll_origin.z) < 1.0e-7 &&
                        std::abs(rolled_partial_sketch.resolved_normal.x -
                            zero_roll_normal.x) < 1.0e-7 &&
                        std::abs(rolled_partial_sketch.resolved_normal.y -
                            zero_roll_normal.y) < 1.0e-7 &&
                        std::abs(rolled_partial_sketch.resolved_normal.z -
                            zero_roll_normal.z) < 1.0e-7 &&
                        std::abs(rolled_x_dot - std::cos(
                            30.0 * std::numbers::pi / 180.0)) < 1.0e-7,
                "Free absolute RY did not roll a partially constrained owned "
                "Sketch/local Origin around its fixed reference normal");

            zima::document::Placement rotated_placement;
            rotated_placement.references = {
                {{}, constructions.document_id + ":origin", "origin:axis:x", 0.0,
                    false, "front", true},
                {{}, constructions.document_id + ":origin", "origin:axis:z", 0.0,
                    false, "top", true}};
            require(zima::document::resolve_placement(
                        rotated_placement, document_origin_geometry) &&
                        std::abs(rotated_placement.rotation_x) < 1.0e-6 &&
                        std::abs(rotated_placement.rotation_y) < 1.0e-6 &&
                        std::abs(rotated_placement.rotation_z - (-90.0)) < 1.0e-4,
                    "Container placement did not derive RZ from a FRONT=X reference");

            zima::document::Placement offset_placement;
            offset_placement.rotation_x = 22.0;
            offset_placement.rotation_y = -8.0;
            offset_placement.rotation_z = 41.0;
            offset_placement.absolute_rotation_x = 22.0;
            offset_placement.absolute_rotation_y = -8.0;
            offset_placement.absolute_rotation_z = 41.0;
            offset_placement.rotation_offset_x = 12.5;
            offset_placement.rotation_offset_y = -4.0;
            offset_placement.rotation_offset_z = 30.0;
            require(zima::document::resolve_placement(
                        offset_placement, document_origin_geometry) &&
                        std::abs(offset_placement.rotation_x - 22.0) < 1.0e-9 &&
                        std::abs(offset_placement.rotation_y - (-8.0)) < 1.0e-9 &&
                        std::abs(offset_placement.rotation_z - 41.0) < 1.0e-9,
                    "Container placement without a FRONT/TOP reference did not "
                    "preserve absolute RX/RY/RZ or incorrectly applied correction");

            // A point is a complete positional constraint, not an orientation
            // constraint.  One/two following planes therefore describe only
            // the local frame and must not demand that the selected point lies
            // on either plane.
            auto point_plane_geometry = document_origin_geometry;
            point_plane_geometry.points.push_back(
                {{10.0, 20.0, 30.0}, {"fixture-point", "point"}});
            zima::document::Placement point_only_placement;
            point_only_placement.absolute_rotation_x = 11.0;
            point_only_placement.absolute_rotation_y = 22.0;
            point_only_placement.absolute_rotation_z = 33.0;
            point_only_placement.references = {{{}, "fixture-point", "point"}};
            bool point_only_oriented = true;
            require(zima::document::resolve_placement(point_only_placement,
                        point_plane_geometry, nullptr, &point_only_oriented) &&
                        !point_only_oriented &&
                        std::abs(point_only_placement.x - 10.0) < 1.0e-7 &&
                        std::abs(point_only_placement.y - 20.0) < 1.0e-7 &&
                        std::abs(point_only_placement.z - 30.0) < 1.0e-7 &&
                        std::abs(point_only_placement.rotation_x - 11.0) < 1.0e-9 &&
                        std::abs(point_only_placement.rotation_y - 22.0) < 1.0e-9 &&
                        std::abs(point_only_placement.rotation_z - 33.0) < 1.0e-9,
                    "Point placement consumed or replaced editable absolute rotation");

            auto point_one_plane = point_only_placement;
            point_one_plane.references.push_back({{},
                constructions.document_id + ":origin", "origin:plane:xz",
                0.0, false, "direction", true, true});
            require(zima::document::orientation_constraint_remaining_dof(
                        point_one_plane.references, point_plane_geometry, true,
                        {10.0, 20.0, 30.0}) == 1 &&
                        zima::document::resolve_placement(
                            point_one_plane, point_plane_geometry) &&
                        std::abs(point_one_plane.x - 10.0) < 1.0e-7 &&
                        std::abs(point_one_plane.y - 20.0) < 1.0e-7 &&
                        std::abs(point_one_plane.z - 30.0) < 1.0e-7,
                    "Point plus one orientation-only plane relocated the container");

            auto point_two_planes = point_one_plane;
            point_two_planes.references.push_back({{},
                constructions.document_id + ":origin", "origin:plane:xy",
                0.0, false, "direction", true, true});
            require(zima::document::orientation_constraint_remaining_dof(
                        point_two_planes.references, point_plane_geometry, true,
                        {10.0, 20.0, 30.0}) == 0 &&
                        zima::document::resolve_placement(
                            point_two_planes, point_plane_geometry) &&
                        std::abs(point_two_planes.x - 10.0) < 1.0e-7 &&
                        std::abs(point_two_planes.y - 20.0) < 1.0e-7 &&
                        std::abs(point_two_planes.z - 30.0) < 1.0e-7,
                "Point plus two orientation-only planes did not fully orient "
                    "the same fixed container origin");

            // One plane determines only FRONT/local-Y. RX/RZ come from that
            // reference, while the user's absolute RY remains the real
            // unsolved roll around the selected normal.
            zima::document::Placement minimum_twist_placement;
            minimum_twist_placement.absolute_rotation_x = 37.0;
            minimum_twist_placement.absolute_rotation_y = -23.0;
            minimum_twist_placement.absolute_rotation_z = 81.0;
            minimum_twist_placement.references = {
                {{}, "fixture-point", "point"},
                {{}, constructions.document_id + ":origin", "origin:plane:xy",
                    0.0, false, "direction", true, true}};
            require(zima::document::resolve_placement(
                        minimum_twist_placement, point_plane_geometry),
                "Single-plane minimum-twist placement did not resolve");
            const auto minimum_twist_x = rotate(
                {1.0, 0.0, 0.0}, minimum_twist_placement);
            const auto minimum_twist_front = rotate(
                {0.0, 1.0, 0.0}, minimum_twist_placement);
            const auto minimum_twist_top = rotate(
                {0.0, 0.0, 1.0}, minimum_twist_placement);
            require(minimum_twist_front.z > 0.999999 &&
                        minimum_twist_x.y < -0.39 &&
                        minimum_twist_top.x < -0.39,
                "Single-plane placement consumed absolute RY instead of "
                "using it as roll around the FRONT normal");
            require(std::abs(minimum_twist_placement.absolute_rotation_x - 90.0) <
                            1.0e-9 &&
                        std::abs(minimum_twist_placement.absolute_rotation_y + 23.0) <
                            1.0e-9 &&
                        std::abs(minimum_twist_placement.absolute_rotation_z) <
                            1.0e-9,
                "Single FRONT reference did not overwrite constrained absolute "
                "RX/RZ while preserving free absolute RY");

            // A point used as a direction must be measured from the newly
            // resolved placement point, never from the previous preview
            // position. This covers the Point + directional Point contract
            // shared by ordinary containers and construction containers.
            point_plane_geometry.points.push_back(
                {{10.0, 30.0, 30.0}, {"direction-point", "point"}});
            point_plane_geometry.points.push_back(
                {{10.0, 20.0, 40.0}, {"top-direction-point", "point"}});
            const zima::document::ConstructionReference direction_point{
                {}, "direction-point", "point", 0.0, false,
                "direction", true, true};
            zima::document::Placement point_direction_placement;
            point_direction_placement.x = -100.0;
            point_direction_placement.y = -100.0;
            point_direction_placement.z = -100.0;
            point_direction_placement.references = {
                {{}, "fixture-point", "point"}, direction_point};
            zima::kernel::Vec3 point_direction_base;
            require(zima::document::resolve_placement(
                        point_direction_placement, point_plane_geometry,
                        &point_direction_base) &&
                        std::abs(point_direction_placement.x - 10.0) < 1.0e-7 &&
                        std::abs(point_direction_placement.y - 20.0) < 1.0e-7 &&
                        std::abs(point_direction_placement.z - 30.0) < 1.0e-7 &&
                        std::abs(point_direction_base.x) < 1.0e-7 &&
                        std::abs(point_direction_base.y) < 1.0e-7 &&
                        std::abs(point_direction_base.z) < 1.0e-7,
                    "Directional Point was calculated from the stale preview "
                    "origin instead of the resolved placement point");
            auto three_point_frame_placement = point_direction_placement;
            three_point_frame_placement.references.push_back({
                {}, "top-direction-point", "point", 0.0, false,
                "direction", true, true});
            require(zima::document::orientation_constraint_remaining_dof(
                        three_point_frame_placement.references,
                        point_plane_geometry, true,
                        {10.0, 20.0, 30.0}) == 0 &&
                        zima::document::resolve_placement(
                            three_point_frame_placement,
                            point_plane_geometry) &&
                        std::abs(three_point_frame_placement.rotation_x) < 1.0e-7 &&
                        std::abs(three_point_frame_placement.rotation_y) < 1.0e-7 &&
                        std::abs(three_point_frame_placement.rotation_z) < 1.0e-7,
                    "Three Point placement did not use P1 as origin, P2 as "
                    "FRONT direction, and P3 as TOP direction");

            auto minimum_twist_point =
                zima::document::PartDocument::create_construction(
                    zima::document::ConstructionKind::Point);
            minimum_twist_point.references = minimum_twist_placement.references;
            minimum_twist_point.absolute_rotation = {37.0, -23.0, 81.0};
            require(zima::document::resolve_construction(
                        minimum_twist_point, point_plane_geometry) &&
                        std::abs(minimum_twist_point.rotation.x - 90.0) < 1.0e-6 &&
                        std::abs(minimum_twist_point.rotation.y) < 1.0e-6 &&
                        std::abs(minimum_twist_point.rotation.z + 23.0) < 1.0e-6 &&
                        std::abs(minimum_twist_point.absolute_rotation.x - 90.0) <
                            1.0e-9 &&
                        std::abs(minimum_twist_point.absolute_rotation.y + 23.0) <
                            1.0e-9 &&
                        std::abs(minimum_twist_point.absolute_rotation.z) <
                            1.0e-9,
                "Construction container did not share partial FRONT placement");
            auto point_direction_construction =
                zima::document::PartDocument::create_construction(
                    zima::document::ConstructionKind::Point);
            point_direction_construction.origin = {-100.0, -100.0, -100.0};
            point_direction_construction.references = {
                {{}, "fixture-point", "point"}, direction_point};
            require(zima::document::resolve_construction(
                        point_direction_construction, point_plane_geometry) &&
                        std::abs(point_direction_construction.origin.x - 10.0) < 1.0e-7 &&
                        std::abs(point_direction_construction.origin.y - 20.0) < 1.0e-7 &&
                        std::abs(point_direction_construction.origin.z - 30.0) < 1.0e-7 &&
                        std::abs(point_direction_construction.rotation.x) < 1.0e-7 &&
                        std::abs(point_direction_construction.rotation.y) < 1.0e-7 &&
                        std::abs(point_direction_construction.rotation.z) < 1.0e-7,
                    "Construction directional Point did not share the resolved-"
                    "origin orientation contract");

            zima::document::Placement composed_placement;
            composed_placement.rotation_offset_z = 15.0;
            composed_placement.references = {
                {{}, constructions.document_id + ":origin", "origin:axis:x", 0.0,
                    false, "front", true},
                {{}, constructions.document_id + ":origin", "origin:axis:z", 0.0,
                    false, "top", true}};
            require(zima::document::resolve_placement(
                        composed_placement, document_origin_geometry) &&
                        std::abs(composed_placement.rotation_z - (-75.0)) < 1.0e-4,
                    "Container placement did not compose the manual RZ correction "
                    "on top of the FRONT/TOP reference frame");

            zima::kernel::ViewerReferenceGeometry three_point_geometry;
            three_point_geometry.points = {
                {{0.0, 0.0, 0.0}, {"points", "p1"}},
                {{10.0, 0.0, 0.0}, {"points", "p2"}},
                {{0.0, 10.0, 0.0}, {"points", "p3"}}};
            zima::document::Placement three_point_placement;
            three_point_placement.references = {
                {{}, "points", "p1"}, {{}, "points", "p2"},
                {{}, "points", "p3"}};
            zima::kernel::Vec3 three_point_base;
            bool three_point_oriented = false;
            require(zima::document::resolve_placement(three_point_placement,
                        three_point_geometry, &three_point_base,
                        &three_point_oriented) && three_point_oriented &&
                        std::abs(three_point_base.x) < 1.0e-9 &&
                        std::abs(three_point_base.y) < 1.0e-9 &&
                        std::abs(three_point_base.z) < 1.0e-9,
                    "Three ordered points did not derive X=P1->P2 and Y toward P3");
            three_point_placement.orientation_quarter_turns = 1;
            require(zima::document::resolve_placement(
                        three_point_placement, three_point_geometry) &&
                        std::abs(three_point_placement.rotation_z - 90.0) < 1.0e-6,
                    "ROTATE did not apply one 90-degree turn to the derived frame");

            auto three_point_plane =
                zima::document::PartDocument::create_construction(
                    zima::document::ConstructionKind::Plane);
            three_point_plane.definition =
                zima::document::ConstructionDefinition::ThreePointPlane;
            three_point_plane.references = {
                {{}, "points", "p1"}, {{}, "points", "p2"},
                {{}, "points", "p3"}};
            three_point_plane.offset = 6.0;
            require(zima::document::resolve_construction(
                        three_point_plane, three_point_geometry) &&
                        three_point_plane.direction.z > 0.999 &&
                        three_point_plane.entity_origin.z > 5.999,
                    "Three-point Plane did not offset along its FRONT normal");
            const auto front_plane_origin = three_point_plane.origin;
            three_point_plane.orientation_back = true;
            require(zima::document::resolve_construction(
                        three_point_plane, three_point_geometry) &&
                        three_point_plane.origin == front_plane_origin &&
                        three_point_plane.direction.z < -0.999 &&
                        three_point_plane.entity_origin.z < -5.999,
                    "BACK did not reverse a three-point Plane normal and offset");

            zima::document::Placement xz_offset_placement;
            xz_offset_placement.references = {{{},
                constructions.document_id + ":origin", "origin:plane:xz",
                12.0, true}};
            require(zima::document::resolve_placement(
                        xz_offset_placement, document_origin_geometry) &&
                        std::abs(xz_offset_placement.x) < 1.0e-7 &&
                        std::abs(xz_offset_placement.y - 12.0) < 1.0e-7 &&
                        std::abs(xz_offset_placement.z) < 1.0e-7,
                    "Positive XZ placement offset did not follow canonical +Y");
            const auto xz_offset_origin = zima::kernel::Vec3{
                xz_offset_placement.x, xz_offset_placement.y,
                xz_offset_placement.z};
            xz_offset_placement.orientation_back = true;
            xz_offset_placement.orientation_quarter_turns = 3;
            require(zima::document::resolve_placement(
                        xz_offset_placement, document_origin_geometry) &&
                        std::abs(xz_offset_placement.x - xz_offset_origin.x) < 1.0e-7 &&
                        std::abs(xz_offset_placement.y - xz_offset_origin.y) < 1.0e-7 &&
                        std::abs(xz_offset_placement.z - xz_offset_origin.z) < 1.0e-7,
                    "FRONT/rotation controls relocated the placement origin");

            zima::document::Placement collinear_three_point;
            auto collinear_geometry = three_point_geometry;
            collinear_geometry.points[2].position = {20.0, 0.0, 0.0};
            collinear_three_point.references = three_point_placement.references;
            require(!zima::document::resolve_placement(
                        collinear_three_point, collinear_geometry),
                    "Collinear three-point placement was accepted as a frame");

            zima::document::Placement missing_placement;
            missing_placement.references = {
                {{}, "does-not-exist", "origin:point"}};
            require(!zima::document::resolve_placement(
                        missing_placement, document_origin_geometry),
                    "Container placement accepted an unresolved position reference");
        }
        auto cyclic_point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        cyclic_point.definition =
            zima::document::ConstructionDefinition::PointReference;
        cyclic_point.references = {{{}, cyclic_point.id + ":origin", "point"}};
        constructions.constructions.push_back(cyclic_point);
        constructions.resolve_constructions();
        require(!constructions.constructions.back().reference_valid,
                "Construction dependency accepted a self-cycle");
        constructions.constructions.pop_back();
        auto retained_axis = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        zima::document::ConstructionReference retained_reference;
        retained_reference.owner_id = constructions.document_id + ":origin";
        retained_reference.semantic_key = "origin:axis:x";
        retained_reference.orientation_role = "front";
        retained_reference.orientation_drives_rotation = true;
        retained_axis.references = {retained_reference};
        require(zima::document::resolve_construction(
                    retained_axis, document_origin_geometry),
                "Retained-axis fixture did not initially resolve");
        const auto retained_origin = retained_axis.origin;
        const auto retained_direction = retained_axis.direction;
        require(!zima::document::resolve_construction(
                    retained_axis, zima::kernel::ViewerReferenceGeometry{}) &&
                    retained_axis.origin == retained_origin &&
                    retained_axis.direction == retained_direction,
                "Construction lost its last valid frame with a missing reference");
        zima::document::PartDocument retained_document;
        retained_document.constructions.push_back(retained_axis);
        const auto retained_mesh = retained_document.construction_viewer_mesh();
        require(retained_mesh.axes.size() == 1 &&
                    retained_mesh.axes.front().point == retained_origin &&
                    retained_mesh.axes.front().direction == retained_direction,
                "Invalid-reference Axis disappeared from normal View");
        auto construction_reference_sketch = zima::sketcher::Sketch::create_default();
        // The Plane lies in global YZ. An external Face reference stores its
        // intersection with the Sketch plane, so XZ produces the global-Z
        // infinite line and also keeps the referenced Z axis projectable.
        construction_reference_sketch.plane = zima::sketcher::SketchPlane::XZ;
        auto point_reference = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Point);
        point_reference.source_document_id = constructions.document_id;
        point_reference.source_owner_id = point.id + ":origin";
        point_reference.source_semantic_key = "point";
        point_reference.cached_points = {{0.0, 0.0}};
        construction_reference_sketch.add_external_reference(point_reference);
        auto axis_reference = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Axis);
        axis_reference.source_document_id = constructions.document_id;
        axis_reference.source_owner_id = axis.entity_id;
        axis_reference.source_semantic_key = "axis";
        axis_reference.cached_points = {{-1.0, 0.0}, {1.0, 0.0}};
        construction_reference_sketch.add_external_reference(axis_reference);
        auto plane_reference = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Face);
        plane_reference.source_document_id = constructions.document_id;
        plane_reference.source_owner_id = plane.entity_id;
        plane_reference.source_semantic_key = "plane";
        plane_reference.cached_points = {{0.0, -1.0}, {0.0, 1.0}};
        plane_reference.infinite = true;
        construction_reference_sketch.add_external_reference(plane_reference);
        const bool refreshed = construction_reference_sketch.refresh_external_references(
            constructions.document_id, construction_mesh.original_references);
        require(refreshed &&
                    construction_reference_sketch.external_references[0].cached_points ==
                        std::vector<std::array<double, 2>>{{1.0, -3.0}} &&
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
        require(loaded_constructions.constructions.size() == 10 &&
                    loaded_constructions.constructions[0].origin.z == 3.0 &&
                    loaded_constructions.constructions[2].display_size == 75.0 &&
                    loaded_constructions.constructions[4].definition ==
                        zima::document::ConstructionDefinition::TwoPointAxis &&
                    loaded_constructions.constructions[4].references[0].owner_id ==
                        point.id + ":origin" &&
                    loaded_constructions.constructions[5].definition ==
                        zima::document::ConstructionDefinition::PlaneReference &&
                    // Only the container's own (un-offset) origin and the
                    // persisted `offset` value round-trip through save/load;
                    // `entity_origin` is a transient field recomputed by
                    // resolve_constructions(), not itself persisted.
                    std::abs(loaded_constructions.constructions[6].origin.x) <
                        1.0e-6 &&
                    std::abs(loaded_constructions.constructions[6].offset - 12.0) <
                        1.0e-6 &&
                    std::abs(loaded_constructions.constructions[7].origin.y) <
                        1.0e-6 &&
                    // Index 8 is the origin bulk-fill Plane. The complete
                    // origin triad represents the document identity frame;
                    // save/load must preserve that result instead of
                    // inheriting whichever datum plane filled row 0 first.
                    std::abs(loaded_constructions.constructions[8].direction.x -
                              1.0) < 1.0e-6 &&
                    std::abs(loaded_constructions.constructions[8].direction.y) <
                        1.0e-6 &&
                    std::abs(loaded_constructions.constructions[8].direction.z) <
                        1.0e-6 &&
                    std::abs(loaded_constructions.constructions[9].rotation.x) <
                        1.0e-6 &&
                    std::abs(loaded_constructions.constructions[9].rotation.y) <
                        1.0e-6 &&
                    std::abs(loaded_constructions.constructions[9].rotation.z) <
                        1.0e-6 &&
                    loaded_constructions.constructions.front().container_origin ==
                        point.container_origin,
                "Construction objects did not survive save/load");
        {
            auto local_plane_document =
                zima::document::PartDocument::create_default();
            auto local_plane = zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Plane);
            local_plane.base_plane = zima::document::LocalDatumPlane::XY;
            local_plane.offset = 9.0;
            const auto local_plane_id = local_plane.id;
            local_plane_document.constructions.push_back(local_plane);
            local_plane_document.resolve_constructions();
            const auto* resolved_local_plane =
                local_plane_document.find_construction(local_plane_id);
            require(resolved_local_plane != nullptr &&
                        resolved_local_plane->base_plane ==
                            zima::document::LocalDatumPlane::XY &&
                        std::abs(resolved_local_plane->direction.x) < 1.0e-9 &&
                        std::abs(resolved_local_plane->direction.y) < 1.0e-9 &&
                        std::abs(resolved_local_plane->direction.z - 1.0) < 1.0e-9 &&
                        std::abs(resolved_local_plane->origin.z) < 1.0e-9 &&
                        std::abs(resolved_local_plane->entity_origin.z - 9.0) < 1.0e-9,
                    "Plane did not offset from its selected local Container Origin XY plane");
            const auto local_plane_mesh =
                local_plane_document.construction_viewer_mesh(local_plane_id);
            const auto displayed_plane = std::find_if(local_plane_mesh.edges.begin(),
                local_plane_mesh.edges.end(), [](const auto& edge) {
                    return edge.reference.semantic_key == "border";
                });
            const auto local_origin_xy = std::find_if(local_plane_mesh.edges.begin(),
                local_plane_mesh.edges.end(), [](const auto& edge) {
                    return edge.reference.semantic_key == "origin:plane:xy";
                });
            require(displayed_plane != local_plane_mesh.edges.end() &&
                        local_origin_xy != local_plane_mesh.edges.end() &&
                        std::all_of(displayed_plane->points.begin(),
                            displayed_plane->points.end(), [](const auto& point) {
                                return std::abs(point.z - 9.0) < 1.0e-9;
                            }) &&
                        displayed_plane->points.size() == 5 &&
                        local_origin_xy->points.size() == 5 &&
                        std::abs(std::hypot(
                            displayed_plane->points[1].x -
                                displayed_plane->points[0].x,
                            displayed_plane->points[1].y -
                                displayed_plane->points[0].y) - 5.0) < 1.0e-9,
                    "Displayed Plane quad ignored its selected local XY plane");
            const auto edge_length = [](const auto& edge) {
                const auto& first = edge.points[0];
                const auto& second = edge.points[1];
                return std::hypot(std::hypot(second.x - first.x,
                                             second.y - first.y),
                                  second.z - first.z);
            };
            const auto tiny_scene_plane =
                local_plane_document.construction_viewer_mesh(local_plane_id, 1.0);
            const auto huge_scene_plane =
                local_plane_document.construction_viewer_mesh(local_plane_id, 1.0e6);
            const auto border_length = [&](const auto& mesh) {
                const auto border = std::find_if(mesh.edges.begin(), mesh.edges.end(),
                    [](const auto& edge) {
                        return edge.reference.semantic_key == "border";
                    });
                return border == mesh.edges.end() ? -1.0 : edge_length(*border);
            };
            require(std::abs(edge_length(*displayed_plane) -
                             edge_length(*local_origin_xy)) < 1.0e-9 &&
                        std::abs(border_length(tiny_scene_plane) - 5.0) < 1.0e-9 &&
                        std::abs(border_length(huge_scene_plane) - 5.0) < 1.0e-9,
                    "Work Plane and Container Origin planes do not share one fixed display size");
            require(std::any_of(local_plane_mesh.points.begin(),
                        local_plane_mesh.points.end(), [](const auto& point) {
                            return point.reference.semantic_key ==
                                "preview:plane-offset-point" &&
                                std::abs(point.position.z - 9.0) < 1.0e-9;
                        }),
                    "Editing Plane has no display-only offset-position marker");
            const auto local_plane_path = std::filesystem::temp_directory_path() /
                "zima-cad-cpp-local-base-plane-contract.prtz";
            local_plane_document.save(local_plane_path);
            const auto loaded_local_plane =
                zima::document::PartDocument::load(local_plane_path);
            std::filesystem::remove(local_plane_path);
            require(loaded_local_plane.constructions.size() == 1 &&
                        loaded_local_plane.constructions.front().base_plane ==
                            zima::document::LocalDatumPlane::XY,
                    "Plane local Container Origin plane did not survive save/load");
        }
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
        const auto profile_cap_key = [](std::string_view role,
                                        const std::string& profile_region_id) {
            return std::string(role) + ":from:" +
                std::to_string(profile_region_id.size()) + ":" +
                profile_region_id;
        };
        const auto extrusion_operations = extrusion_document.kernel_operations();
        const auto& extrusion_request =
            std::get<zima::kernel::ExtrusionRequest>(
                extrusion_operations.front().primitive);
        const auto extrusion_start_cap =
            profile_cap_key("start", extrusion_request.profile_region_id);
        const auto extrusion_end_cap =
            profile_cap_key("end", extrusion_request.profile_region_id);
        const auto extrusion_results =
            kernel.evaluate_history(extrusion_operations);
        require(extrusion_results.size() == 1 &&
                    std::abs(extrusion_results.front().volume - 6000.0) < 1.0e-6,
                "Closed Sketch extrusion produced an incorrect solid volume");
        auto rounded_document = zima::document::PartDocument::create_default();
        auto rounded_sketch = zima::sketcher::Sketch::create_default();
        const auto rounded_segments =
            rounded_sketch.add_rectangle(0.0, 0.0, 30.0, 20.0);
        const auto rounded_corner = rounded_sketch.segments[0].second_point_id;
        const auto rounded_point_count = rounded_sketch.points.size();
        const auto rounded_radius = rounded_sketch.add_corner_fillet(
            rounded_segments[0], rounded_segments[1], 2.0);
        const auto rounded_sketch_id = rounded_sketch.id;
        rounded_document.sketches.push_back(rounded_sketch);
        auto rounded_extrusion =
            zima::document::PartDocument::create_extrusion_container(
                rounded_sketch_id);
        rounded_extrusion.extrusion.height = 10.0;
        rounded_document.history.push_back(std::move(rounded_extrusion));
        const auto rounded_results =
            kernel.evaluate_history(rounded_document.kernel_operations());
        require(rounded_results.size() == 1 &&
                    std::abs(rounded_results.front().volume -
                        (600.0 - 4.0 + std::numbers::pi) * 10.0) < 1.0e-5 &&
                    rounded_document.sketches.front().points.size() ==
                        rounded_point_count &&
                    rounded_document.sketches.front().find_point(
                        rounded_corner) != nullptr &&
                    rounded_document.sketches.front().corner_radii.size() == 1 &&
                    rounded_document.sketches.front().corner_radii.front().id ==
                        rounded_radius.arc_id,
                "Corner radius did not remain non-destructive or reach Extrusion");
        const auto rounded_preview_matches_evaluated_profile = [](
                const zima::sketcher::Sketch& source,
                const std::vector<zima::kernel::ViewerEdge>& preview) {
            const auto evaluated = source.evaluated_profile_sketch();
            const auto evaluated_mesh = evaluated.viewer_mesh();
            const auto is_profile_edge = [](const auto& edge) {
                if (edge.construction || edge.points.size() < 2) return false;
                const auto& key = edge.reference.semantic_key;
                return key.starts_with("segment:") ||
                    key.starts_with("circle:") || key.starts_with("arc:") ||
                    key.starts_with("ellipse:") ||
                    key.starts_with("elliptical_arc:") ||
                    key.starts_with("bspline:") || key.starts_with("text:");
            };
            const auto expected = std::count_if(
                evaluated_mesh.edges.begin(), evaluated_mesh.edges.end(),
                is_profile_edge);
            const auto count_role = [&](std::string_view role) {
                return std::count_if(preview.begin(), preview.end(),
                    [&](const auto& edge) {
                        return edge.reference.semantic_key == role;
                    });
            };
            const auto has_sampled_role = [&](std::string_view role) {
                return std::any_of(preview.begin(), preview.end(),
                    [&](const auto& edge) {
                        return edge.reference.semantic_key == role &&
                            edge.points.size() > 2;
                    });
            };
            return expected > 0 &&
                count_role("preview:start") == expected &&
                count_role("preview:end") == expected &&
                has_sampled_role("preview:start") &&
                has_sampled_role("preview:end");
        };
        const auto rounded_extrusion_preview =
            rounded_document.extrusion_preview_edges(
                rounded_document.history.front());
        require(rounded_preview_matches_evaluated_profile(
                    rounded_document.sketches.front(),
                    rounded_extrusion_preview),
                "Rounded Extrusion cyan wire diverged from its evaluated profile");
        auto rounded_thin_extrusion = rounded_document;
        rounded_thin_extrusion.history.front().extrusion.result_type =
            zima::document::ProfileResultType::Thin;
        rounded_thin_extrusion.history.front().extrusion.thin_thickness = 2.0;
        rounded_thin_extrusion.history.front().extrusion.thin_mode =
            zima::document::ThinMode::Symmetric;
        const auto rounded_thin_preview =
            rounded_thin_extrusion.extrusion_preview_edges(
                rounded_thin_extrusion.history.front());
        const auto has_closed_thin_role = [](const auto& preview,
                                             std::string_view role) {
            return std::any_of(preview.begin(), preview.end(),
                [&](const auto& edge) {
                    if (edge.reference.semantic_key != role ||
                        edge.points.size() < 4) return false;
                    const auto& first = edge.points.front();
                    const auto& last = edge.points.back();
                    return std::hypot(std::hypot(
                        first.x-last.x, first.y-last.y), first.z-last.z) < 1.0e-7;
                });
        };
        require(has_closed_thin_role(
                    rounded_thin_preview, "preview:start:thin:inside") &&
                    has_closed_thin_role(
                        rounded_thin_preview, "preview:start:thin:outside"),
                "Rounded Thin Extrusion cyan wire lost its corner-radius arc");
        const auto solid_only_preview =
            extrusion_document.extrusion_preview_edges(
                extrusion_document.history.front());
        auto thin_extrusion_document = extrusion_document;
        thin_extrusion_document.history.front().extrusion.result_type =
            zima::document::ProfileResultType::Thin;
        thin_extrusion_document.history.front().extrusion.thin_thickness = 2.0;
        thin_extrusion_document.history.front().extrusion.thin_mode =
            zima::document::ThinMode::Symmetric;
        const auto thin_extrusion_preview =
            thin_extrusion_document.extrusion_preview_edges(
                thin_extrusion_document.history.front());
        require(!thin_extrusion_preview.empty() &&
                    thin_extrusion_preview.size() != solid_only_preview.size() &&
                    std::ranges::any_of(thin_extrusion_preview, [](const auto& edge) {
                        return edge.reference.semantic_key ==
                            "preview:start:thin:inside";
                    }) &&
                    std::ranges::any_of(thin_extrusion_preview, [](const auto& edge) {
                        return edge.reference.semantic_key ==
                            "preview:start:thin:outside";
                    }),
                "Thin Extrusion preview did not replace the Sketch centreline "
                "with its two offset wall boundaries");
        auto construction_profile_document = extrusion_document;
        static_cast<void>(construction_profile_document.sketches.front()
            .add_segment(-100.0, -100.0, 100.0, 100.0, 1.0e-6, true));
        const auto construction_filtered_preview =
            construction_profile_document.extrusion_preview_edges(
                construction_profile_document.history.front());
        const auto construction_filtered_results = kernel.evaluate_history(
            construction_profile_document.kernel_operations());
        require(construction_filtered_preview.size() ==
                    solid_only_preview.size() &&
                    construction_filtered_results.size() == 1 &&
                    std::abs(construction_filtered_results.front().volume -
                        extrusion_results.front().volume) < 1.0e-6,
                "Construction geometry leaked into the Extrusion preview or solid");
        auto owned_sketch_document = extrusion_document;
        owned_sketch_document.sketches.front().owner_container_id =
            extrusion_container_id;
        owned_sketch_document.insert_history_entry(
            zima::document::PartHistoryKind::Feature, extrusion_container_id);
        const auto owned_sketch_path = std::filesystem::temp_directory_path() /
            "zima-cad-owned-sketch-contract.prtz";
        owned_sketch_document.save(owned_sketch_path, extrusion_results);
        std::vector<zima::kernel::BodyResult> loaded_owned_boundaries;
        const auto loaded_owned_sketch_document =
            zima::document::PartDocument::load(
                owned_sketch_path, &loaded_owned_boundaries);
        std::filesystem::remove(owned_sketch_path);
        require(loaded_owned_sketch_document.history_order.size() == 1 &&
                    loaded_owned_sketch_document.history_order.front().kind ==
                        zima::document::PartHistoryKind::Feature &&
                    loaded_owned_sketch_document.sketches.size() == 1 &&
                    loaded_owned_sketch_document.sketches.front()
                            .owner_container_id == extrusion_container_id &&
                    loaded_owned_sketch_document.history.front().extrusion.sketch_id ==
                        loaded_owned_sketch_document.sketches.front().id &&
                    loaded_owned_boundaries.size() == 1,
                "Owned Sketch was duplicated in history or lost during Part save/load");
        auto moved_owned_sketch_document = loaded_owned_sketch_document;
        moved_owned_sketch_document.history.front().placement.x = 11.0;
        moved_owned_sketch_document.history.front().placement.y = -3.0;
        moved_owned_sketch_document.history.front().placement.z = 20.0;
        moved_owned_sketch_document.history.front().extrusion.profile_plane_offset = 6.0;
        moved_owned_sketch_document.sketches.front().plane_offset = 6.0;
        moved_owned_sketch_document.resolve_constructions();
        const auto moved_profile_origin =
            moved_owned_sketch_document.sketches.front().world_point(0.0, 0.0);
        require(std::abs(moved_profile_origin.x - 11.0) < 1.0e-9 &&
                    std::abs(moved_profile_origin.y + 3.0) < 1.0e-9 &&
                    std::abs(moved_profile_origin.z - 26.0) < 1.0e-9,
                "Owned Extrusion Sketch did not follow container placement and plane offset");
        auto rotated_owned_sketch_document = moved_owned_sketch_document;
        rotated_owned_sketch_document.history.front().placement
            .orientation_quarter_turns = 1;
        rotated_owned_sketch_document.resolve_constructions();
        const auto rotated_profile_point =
            rotated_owned_sketch_document.sketches.front().world_point(30.0, 0.0);
        require(std::abs(rotated_profile_point.x - 11.0) < 1.0e-7 &&
                    std::abs(rotated_profile_point.y - 27.0) < 1.0e-7 &&
                    std::abs(rotated_profile_point.z - 26.0) < 1.0e-7 &&
                    !rotated_owned_sketch_document.extrusion_preview_edges(
                        rotated_owned_sketch_document.history.front()).empty(),
                "ROTATE did not rigidly rotate the owned Sketch and Extrusion preview");
        auto flipped_owned_sketch_document = moved_owned_sketch_document;
        flipped_owned_sketch_document.history.front().placement.orientation_back = true;
        flipped_owned_sketch_document.resolve_constructions();
        const auto flipped_profile_origin =
            flipped_owned_sketch_document.sketches.front().world_point(0.0, 0.0);
        const auto flipped_profile_point =
            flipped_owned_sketch_document.sketches.front().world_point(0.0, 20.0);
        require(std::abs(flipped_profile_origin.x - 11.0) < 1.0e-7 &&
                    std::abs(flipped_profile_origin.y + 3.0) < 1.0e-7 &&
                    std::abs(flipped_profile_origin.z - 14.0) < 1.0e-7 &&
                    std::abs(flipped_profile_point.y + 23.0) < 1.0e-7,
                "FRONT/BACK did not rigidly flip the owned Sketch frame");
        const auto moved_results = kernel.evaluate_history(
            moved_owned_sketch_document.kernel_operations());
        require(moved_results.size() == 1 &&
                    std::any_of(moved_results.front().mesh.vertices.begin(),
                        moved_results.front().mesh.vertices.end(),
                        [&](const auto& vertex) {
                            return std::abs(vertex.x - moved_profile_origin.x) < 1.0e-7 &&
                                std::abs(vertex.y - moved_profile_origin.y) < 1.0e-7 &&
                                std::abs(vertex.z - moved_profile_origin.z) < 1.0e-7;
                        }),
                "Owned Extrusion solid was transformed away from its resolved Sketch plane");
        std::set<std::string> extrusion_faces;
        for (const auto& reference : extrusion_results.front().mesh.original_references.triangle_references) {
            require(reference.owner_id == extrusion_container_id && reference.valid(),
                    "Extrusion face lost its stable history owner");
            extrusion_faces.insert(reference.semantic_key);
        }
        require(extrusion_faces.contains(extrusion_start_cap) &&
                    extrusion_faces.contains(extrusion_end_cap) &&
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
        require(extrusion_results.front().mesh.original_references.axes.empty(),
                "Non-circular Extrusion invented a center axis");
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
        require(std::abs(referenced_face_z(
                    extrusion_results.front(), extrusion_start_cap)) <
                    1.0e-7 &&
                std::abs(referenced_face_z(
                    extrusion_results.front(), extrusion_end_cap) - 10.0) <
                    1.0e-7,
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
                        reverse_extrusion_results.front(),
                        extrusion_start_cap) + 10.0) <
                        1.0e-7 &&
                    std::abs(referenced_face_z(
                        reverse_extrusion_results.front(),
                        extrusion_end_cap)) < 1.0e-7,
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
                        symmetric_extrusion_results.front(),
                        extrusion_start_cap) + 5.0) <
                        1.0e-7 &&
                    std::abs(referenced_face_z(
                        symmetric_extrusion_results.front(),
                        extrusion_end_cap) - 5.0) <
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
        parity_fillet.edge_treatment.primary_size = 1.5;
        parity_fillet.edge_treatment.fillet_mode =
            zima::document::EdgeTreatmentParameters::FilletMode::Linear;
        parity_fillet.edge_treatment.secondary_size = 2.25;
        parity_fillet.edge_treatment.reverse = true;
        require(parity_edge->edge_treatment_endpoint_references.size() == 2,
            "Parity workflow edge has no stable variable-Fillet endpoints");
        parity_fillet.edge_treatment.route_start_vertices = {
            parity_edge->edge_treatment_endpoint_references.front()};
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
                        zima::document::FeatureKind::Fillet &&
                    loaded_parity.history.back().edge_treatment.fillet_mode ==
                        zima::document::EdgeTreatmentParameters::FilletMode::Linear &&
                    loaded_parity.history.back().edge_treatment.primary_size == 1.5 &&
                    loaded_parity.history.back().edge_treatment.secondary_size == 2.25 &&
                    loaded_parity.history.back().edge_treatment.reverse &&
                    loaded_parity.history.back().edge_treatment.
                        route_start_vertices ==
                        parity_fillet.edge_treatment.route_start_vertices &&
                    std::ranges::any_of(
                        loaded_parity_results.back().mesh.edges,
                        [&](const auto& edge) {
                            return std::ranges::find(
                                edge.edge_treatment_owner_ids,
                                parity_fillet.id) !=
                                edge.edge_treatment_owner_ids.end();
                        }),
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
        const auto up_to_operations = up_to_document.kernel_operations();
        const auto& up_to_request =
            std::get<zima::kernel::ExtrusionRequest>(
                up_to_operations.back().primitive);
        const auto up_to_end_cap =
            profile_cap_key("end", up_to_request.profile_region_id);
        require(std::ranges::any_of(
                    up_to_results.back().mesh.triangle_references,
                    [&](const auto& reference) {
                        return reference.owner_id == up_to.id &&
                            reference.semantic_key == up_to_end_cap;
                    }),
                "Up-to-plane Extrusion lost its profile-owned end face");
        // Boolean intersection edges are created by Up-to Extrusion whenever
        // its clipped prism joins an existing body. They must carry a stable
        // ZIMA identity so a following Fillet/Chamfer can select them.
        zima::kernel::CylinderRequest joining_cylinder;
        joining_cylinder.radius = 5.0;
        joining_cylinder.height = 20.0;
        joining_cylinder.translation = {10.0, 10.0, 5.0};
        const auto joined_results = kernel.evaluate_history({
            {"joined-box",
                zima::kernel::BoxRequest{20.0, 20.0, 10.0},
                zima::kernel::BooleanOperation::Add},
            {"joined-cylinder", joining_cylinder,
                zima::kernel::BooleanOperation::Add}});
        const auto boolean_intersection_edge = std::find_if(
            joined_results.back().mesh.edges.begin(),
            joined_results.back().mesh.edges.end(), [](const auto& edge) {
                return edge.reference.owner_id == "joined-cylinder" &&
                    edge.reference.semantic_key.starts_with(
                        "boolean:add:intersection:") &&
                    edge.edge_treatment_side_references.size() == 2;
            });
        require(boolean_intersection_edge !=
                    joined_results.back().mesh.edges.end(),
                "Boolean result left its generated intersection edge "
                "anonymous and unavailable to Fillet");
        zima::kernel::FilletRequest joined_fillet{
            {boolean_intersection_edge->reference}, 0.75};
        const auto joined_fillet_results = kernel.evaluate_history({
            {"joined-box",
                zima::kernel::BoxRequest{20.0, 20.0, 10.0},
                zima::kernel::BooleanOperation::Add},
            {"joined-cylinder", joining_cylinder,
                zima::kernel::BooleanOperation::Add},
            {"joined-fillet", joined_fillet,
                zima::kernel::BooleanOperation::Add}});
        require(joined_fillet_results.size() == 3 &&
                    !joined_fillet_results.back().mesh.triangles.empty(),
                "Fillet could not consume a stable generated Boolean edge");
        const auto joined_fillet_boundary_count = std::ranges::count_if(
            joined_fillet_results.back().mesh.edges, [](const auto& edge) {
                return std::ranges::find(
                    edge.edge_treatment_owner_ids, "joined-fillet") !=
                    edge.edge_treatment_owner_ids.end();
            });
        const auto joined_fillet_owned_count = std::ranges::count_if(
            joined_fillet_results.back().mesh.edges, [](const auto& edge) {
                return edge.reference.owner_id == "joined-fillet";
            });
        const auto joined_fillet_non_boundary_edges_are_internal =
            std::ranges::all_of(joined_fillet_results.back().mesh.edges,
                [](const auto& edge) {
                    if (edge.reference.owner_id != "joined-fillet" ||
                        std::ranges::find(edge.edge_treatment_owner_ids,
                            "joined-fillet") !=
                            edge.edge_treatment_owner_ids.end()) {
                        return true;
                    }
                    // OCCT can split one treatment surface into several
                    // faces. Their shared seam is owned by the treatment but
                    // is intentionally not part of its visible boundary.
                    const auto adjacent =
                        edge.reference.semantic_key.find(":between:");
                    return adjacent != std::string::npos &&
                        edge.reference.semantic_key.find(
                            "joined-fillet", adjacent) != std::string::npos;
                });
        const auto joined_fillet_identity_error =
            "Fillet on a generated Boolean edge stole surviving result "
            "edge identities outside its generated face boundary; boundary=" +
            std::to_string(joined_fillet_boundary_count) + ", owned=" +
            std::to_string(joined_fillet_owned_count);
        require(joined_fillet_boundary_count != 0 &&
                    joined_fillet_owned_count >= joined_fillet_boundary_count &&
                    joined_fillet_non_boundary_edges_are_internal,
                joined_fillet_identity_error.c_str());
        // A protruding box rotated around X creates an interior Boolean seam
        // between a horizontal face and a 15-degree inclined face. Both
        // supporting faces extend past that seam, so a face classifier alone
        // cannot choose the material side. The persisted preview guides must
        // use the oriented OCCT faces and agree with the real Fillet side.
        zima::kernel::BoxRequest sloped_box{50.0, 30.0, 20.0};
        sloped_box.translation = {25.0, 30.0, 5.0};
        sloped_box.rotation_degrees = {15.0, 0.0, 0.0};
        const auto sloped_join_results = kernel.evaluate_history({
            {"sloped-base", zima::kernel::BoxRequest{100.0, 80.0, 10.0},
                zima::kernel::BooleanOperation::Add},
            {"sloped-box", sloped_box,
                zima::kernel::BooleanOperation::Add}});
        const auto sloped_intersection_edge = std::find_if(
            sloped_join_results.back().mesh.edges.begin(),
            sloped_join_results.back().mesh.edges.end(), [](const auto& edge) {
                return edge.reference.owner_id == "sloped-box" &&
                    edge.reference.semantic_key.starts_with(
                        "boolean:add:intersection:") &&
                    edge.points.size() >= 2 &&
                    edge.edge_treatment_side_directions.size() == 2 &&
                    std::ranges::all_of(edge.points, [](const auto& point) {
                        return std::abs(point.z - 10.0) < 1.0e-6 &&
                            std::abs(point.y - 28.6602540378444) < 1.0e-6;
                    });
            });
        require(sloped_intersection_edge !=
                    sloped_join_results.back().mesh.edges.end(),
                "Sloped Boolean result exposed no selectable intersection edge");
        const auto has_preview_direction = [&](const auto& expected) {
            return std::ranges::any_of(
                sloped_intersection_edge->edge_treatment_side_directions,
                [&](const auto& samples) {
                    return !samples.empty() &&
                        std::abs(samples.front().x - expected.x) < 1.0e-6 &&
                        std::abs(samples.front().y - expected.y) < 1.0e-6 &&
                        std::abs(samples.front().z - expected.z) < 1.0e-6;
                });
        };
        require(has_preview_direction(zima::kernel::Vec3{0.0, -1.0, 0.0}) &&
                    has_preview_direction(zima::kernel::Vec3{
                        0.0, -0.258819045102521, 0.965925826289068}),
                "Sloped Boolean Fillet preview selected the supplementary, "
                "non-material side of an oriented face");
        const auto sloped_fillet_results = kernel.evaluate_history({
            {"sloped-base", zima::kernel::BoxRequest{100.0, 80.0, 10.0},
                zima::kernel::BooleanOperation::Add},
            {"sloped-box", sloped_box,
                zima::kernel::BooleanOperation::Add},
            {"sloped-fillet", zima::kernel::FilletRequest{
                {sloped_intersection_edge->reference}, 0.75},
                zima::kernel::BooleanOperation::Add}});
        require(sloped_fillet_results.size() == 3 &&
                    !sloped_fillet_results.back().mesh.triangles.empty(),
                "Fillet failed on the sloped Boolean intersection edge");
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
        face_target_cut.extrusion.target_plane_origin = {0.0, 0.0, 5.0};
        face_target_cut.extrusion.target_plane_normal = {0.0, 0.0, 1.0};
        face_target_document.history.push_back(face_target_cut);
        const auto face_target_results =
            kernel.evaluate_history(face_target_document.kernel_operations());
        require(std::abs(face_target_results.back().volume - 3500.0) < 1e-6,
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
        auto interpolation_document = zima::document::PartDocument::create_default();
        auto interpolation_sketch = zima::sketcher::Sketch::create_default();
        static_cast<void>(interpolation_sketch.add_bspline({
            {-18.0, 0.0}, {-9.0, 16.0}, {9.0, 16.0},
            {18.0, 0.0}, {9.0, -16.0}, {-9.0, -16.0}},
            3, true, false, 1.0e-6, true));
        const auto interpolation_sketch_id = interpolation_sketch.id;
        interpolation_document.sketches.push_back(std::move(interpolation_sketch));
        auto interpolation_feature =
            zima::document::PartDocument::create_extrusion_container(
                interpolation_sketch_id);
        interpolation_feature.extrusion.height = 8.0;
        interpolation_document.history.push_back(interpolation_feature);
        const auto interpolation_results = kernel.evaluate_history(
            interpolation_document.kernel_operations());
        require(interpolation_results.size() == 1 &&
                    interpolation_results.front().volume > 1.0,
                "Closed interpolating spline did not reach exact OCCT profile evaluation");
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
                    std::abs(through_results.back().volume - 3875.0) < 1e-6,
                "Forward Through-all subtractive Extrusion crossed the Sketch plane");
        double through_cut_min_z = std::numeric_limits<double>::infinity();
        for (const auto& edge :
             through_results.back().mesh.original_references.edges) {
            if (edge.reference.owner_id != through.id) continue;
            for (const auto& point : edge.points) {
                through_cut_min_z = std::min(through_cut_min_z, point.z);
            }
        }
        require(through_cut_min_z >= -1.0e-8,
                "Forward Through-all crossed the Sketch plane in reverse");
        double longest_through_reference_edge{};
        for (const auto& edge :
             through_results.back().mesh.original_references.edges) {
            if (edge.reference.owner_id != through.id || edge.points.size() < 2)
                continue;
            for (std::size_t index = 1; index < edge.points.size(); ++index) {
                const auto& first = edge.points[index - 1];
                const auto& second = edge.points[index];
                longest_through_reference_edge = std::max(
                    longest_through_reference_edge,
                    std::hypot(std::hypot(second.x - first.x,
                                         second.y - first.y),
                               second.z - first.z));
            }
        }
        require(longest_through_reference_edge > 4.0 &&
                    longest_through_reference_edge < 100.0,
                "Through-all persisted reference wire is not finite around the input body");
        auto two_sided_calculation = through_document;
        auto& two_sided_container = two_sided_calculation.history.back();
        two_sided_container.extrusion.extent =
            zima::document::ExtrusionExtent::Blind;
        two_sided_container.extrusion.extent_mode =
            zima::document::ProfileExtentMode::TwoSides;
        two_sided_container.extrusion.end_condition_forward =
            zima::document::EndCondition::ThroughAll;
        two_sided_container.extrusion.end_condition_reverse =
            zima::document::EndCondition::ThroughAll;
        // A stale/manual reverse length must not translate a Through-all
        // operand away from its persisted Sketch plane.
        two_sided_container.extrusion.length_reverse = 80.0;
        const auto two_sided_through_results = kernel.evaluate_history(
            two_sided_calculation.kernel_operations());
        require(two_sided_through_results.size() == 2 &&
                    std::abs(two_sided_through_results.back().volume - 3750.0) < 1e-6,
                "Two-sided Through-all operand moved away from the input body");
        const auto through_preview = through_document.extrusion_preview_edges(
            through, through_results.front().mesh);
        const auto preview_request = through_document.kernel_operations().back();
        const auto preview_direction =
            std::get<zima::kernel::ExtrusionRequest>(
                preview_request.primitive).direction;
        const double preview_direction_length = std::hypot(std::hypot(
            preview_direction.x, preview_direction.y), preview_direction.z);
        const zima::kernel::Vec3 preview_unit{
            preview_direction.x / preview_direction_length,
            preview_direction.y / preview_direction_length,
            preview_direction.z / preview_direction_length};
        const auto projection = [&](const auto& point) {
            return point.x * preview_unit.x + point.y * preview_unit.y +
                point.z * preview_unit.z;
        };
        double preview_min = std::numeric_limits<double>::infinity();
        double preview_max = -std::numeric_limits<double>::infinity();
        for (const auto& edge : through_preview) {
            for (const auto& point : edge.points) {
                preview_min = std::min(preview_min, projection(point));
                preview_max = std::max(preview_max, projection(point));
            }
        }
        double input_max = -std::numeric_limits<double>::infinity();
        for (const auto& point : through_results.front().mesh.vertices) {
            input_max = std::max(input_max, projection(point));
        }
        require(!through_preview.empty() && preview_min >= -1.0e-8 &&
                    preview_max > input_max && preview_max < input_max + 2.0,
                "Through-all preview is not a finite forward wire just beyond input body");
        auto two_sided_through = through;
        two_sided_through.extrusion.extent =
            zima::document::ExtrusionExtent::Blind;
        two_sided_through.extrusion.extent_mode =
            zima::document::ProfileExtentMode::TwoSides;
        two_sided_through.extrusion.end_condition_forward =
            zima::document::EndCondition::ThroughAll;
        two_sided_through.extrusion.end_condition_reverse =
            zima::document::EndCondition::ThroughAll;
        double profile_min = std::numeric_limits<double>::infinity();
        for (const auto& edge : through_document.sketches.front().viewer_mesh().edges) {
            if (edge.construction) continue;
            for (const auto& point : edge.points) {
                profile_min = std::min(profile_min, projection(point));
            }
        }
        auto surface_input = through_results.front().mesh;
        double surface_input_min = std::numeric_limits<double>::infinity();
        for (const auto& point : surface_input.vertices) {
            surface_input_min = std::min(surface_input_min, projection(point));
        }
        const double surface_shift = profile_min - surface_input_min;
        for (auto& point : surface_input.vertices) {
            point.x += preview_unit.x * surface_shift;
            point.y += preview_unit.y * surface_shift;
            point.z += preview_unit.z * surface_shift;
        }
        double surface_input_max = -std::numeric_limits<double>::infinity();
        for (const auto& point : surface_input.vertices) {
            surface_input_max = std::max(surface_input_max, projection(point));
        }
        const auto two_sided_through_preview = through_document.extrusion_preview_edges(
            two_sided_through, surface_input);
        double two_sided_min = std::numeric_limits<double>::infinity();
        double two_sided_max = -std::numeric_limits<double>::infinity();
        for (const auto& edge : two_sided_through_preview) {
            for (const auto& point : edge.points) {
                two_sided_min = std::min(two_sided_min, projection(point));
                two_sided_max = std::max(two_sided_max, projection(point));
            }
        }
        require(!two_sided_through_preview.empty() &&
                    std::abs(two_sided_min - (profile_min - 1.0)) < 1.0e-8 &&
                    two_sided_max > surface_input_max &&
                    two_sided_max < surface_input_max + 2.0,
                "Two-sided Through-all preview did not keep a short empty-side tail");
        auto placed_extrusion = extrusion_document;
        placed_extrusion.history.front().placement.x = 5.0;
        const auto placed_extrusion_results =
            kernel.evaluate_history(placed_extrusion.kernel_operations());
        require(placed_extrusion_results.size() == 1 &&
                    std::abs(placed_extrusion_results.front().volume - 6000.0) < 1.0e-6,
                "Extrusion container placement changed the extruded solid's volume");
        const auto min_x = [](const auto& results) {
            double value = std::numeric_limits<double>::infinity();
            for (const auto& vertex : results.front().mesh.vertices) {
                value = std::min(value, vertex.x);
            }
            return value;
        };
        require(std::abs(min_x(placed_extrusion_results) -
                    (min_x(extrusion_results) + 5.0)) < 1.0e-6,
                "Extrusion container placement did not translate the extruded profile");
        auto rotated_extrusion = extrusion_document;
        rotated_extrusion.history.front().placement.rotation_z = 90.0;
        const auto rotated_extrusion_results =
            kernel.evaluate_history(rotated_extrusion.kernel_operations());
        require(rotated_extrusion_results.size() == 1 &&
                    std::abs(rotated_extrusion_results.front().volume - 6000.0) < 1.0e-6,
                "Extrusion container orientation changed the extruded solid's volume");
        const auto bounding_extent = [](const auto& results, char axis) {
            double lo = std::numeric_limits<double>::infinity();
            double hi = -std::numeric_limits<double>::infinity();
            for (const auto& vertex : results.front().mesh.vertices) {
                const double value = axis == 'x' ? vertex.x : vertex.y;
                lo = std::min(lo, value);
                hi = std::max(hi, value);
            }
            return hi - lo;
        };
        require(std::abs(bounding_extent(rotated_extrusion_results, 'x') -
                    bounding_extent(extrusion_results, 'y')) < 1.0e-6 &&
                    std::abs(bounding_extent(rotated_extrusion_results, 'y') -
                        bounding_extent(extrusion_results, 'x')) < 1.0e-6,
                "Extrusion container orientation did not rotate the extruded profile");
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
        circular_sketch.plane_offset = -10.0;
        static_cast<void>(circular_sketch.add_circle(0.0, 0.0, 5.0));
        const auto circular_sketch_id = circular_sketch.id;
        circular_document.sketches.push_back(std::move(circular_sketch));
        auto circular_base = zima::document::PartDocument::create_box_container();
        circular_base.box = {40.0, 40.0, 10.0};
        circular_document.history.push_back(std::move(circular_base));
        auto circular_cut =
            zima::document::PartDocument::create_extrusion_container(
                circular_sketch_id);
        circular_cut.extrusion.height = 20.0;
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
        const auto circular_opening_edge = std::find_if(
            circular_results.back().mesh.original_references.edges.begin(),
            circular_results.back().mesh.original_references.edges.end(),
            [&](const auto& edge) {
                return edge.reference.owner_id == circular_cut_id &&
                    (edge.reference.semantic_key.starts_with("start:") ||
                     edge.reference.semantic_key.starts_with("end:") ||
                     edge.reference.semantic_key.starts_with(
                         "boolean:subtract:intersection:")) &&
                    std::any_of(edge.points.begin(), edge.points.end(),
                        [](const auto& point) {
                            return std::abs(std::abs(point.z) - 5.0) < 1.0e-6;
                        });
            });
        require(circular_opening_edge !=
                    circular_results.back().mesh.original_references.edges.end() &&
                    circular_opening_edge->points.size() > 3,
                "Circular subtract did not persist its visible opening edge "
                "for external Sketch references");
        const auto& circular_axes = circular_results.back().mesh.original_references.axes;
        const auto circular_axis = std::find_if(circular_axes.begin(), circular_axes.end(),
            [&](const auto& value) {
                return value.reference.owner_id == circular_cut_id;
            });
        require(circular_axis != circular_axes.end() &&
                    std::count_if(circular_axes.begin(), circular_axes.end(),
                        [&](const auto& value) {
                            return value.reference.owner_id == circular_cut_id;
                        }) == 1 &&
                    circular_axis->reference.semantic_key == "axis:primary" &&
                    circular_axis->display_length > 20.0 &&
                    circular_axis->display_length < 23.0,
                "Circular Extrusion did not publish one fitted primary axis");
        require(circular_results.back().mesh.axes.size() == 1 &&
                    circular_results.back().mesh.axes.front().reference.owner_id ==
                        circular_cut_id &&
                    circular_results.back().mesh.axes.front().reference.semantic_key ==
                        "axis:primary",
                "Circular subtract Extrusion did not expose its primary axis in View");
        auto persisted_circular_results = circular_results;
        persisted_circular_results.back().mesh.axes.clear();
        zima::document::DocumentSession circular_session(
            circular_document, std::move(persisted_circular_results));
        const auto circular_display = circular_session.calculated_boundary(2);
        require(circular_display && circular_display->mesh.axes.size() == 1 &&
                    circular_display->mesh.axes.front().reference.owner_id ==
                        circular_cut_id,
                "Loaded circular subtract did not republish its persisted axis in View");
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
                    xz_circle_results.front().mesh.original_references.axes.front().direction.y > 0.999,
                "Circular XZ extrusion lost its exact volume or plane normal");
        auto multiple_circle_document = circular_document;
        static_cast<void>(multiple_circle_document.sketches.front().add_circle(
            10.0, 10.0, 2.0));
        const auto multiple_circle_results = kernel.evaluate_history(
            multiple_circle_document.kernel_operations());
        require(std::abs(multiple_circle_results.back().volume -
                    (16000.0 - 290.0 * std::numbers::pi)) < 1.0e-6 &&
                    std::count_if(multiple_circle_results.back().mesh
                            .original_references.axes.begin(),
                        multiple_circle_results.back().mesh.original_references
                            .axes.end(), [&](const auto& value) {
                            return value.reference.owner_id == circular_cut_id;
                        }) == 2,
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
        std::set<std::string> expected_text_cap_keys{
            profile_cap_key("start", text_request.profile_region_id),
            profile_cap_key("end", text_request.profile_region_id)};
        for (const auto& region : text_request.additional_profile_regions) {
            expected_text_cap_keys.insert(
                profile_cap_key("start", region.region_id));
            expected_text_cap_keys.insert(
                profile_cap_key("end", region.region_id));
        }
        std::set<std::string> actual_text_cap_keys;
        bool stable_text_boundary_found = false;
        for (const auto& reference : text_profile_results.front().mesh
                 .original_references.triangle_references) {
            if (reference.owner_id ==
                    text_profile_document.history.front().id &&
                (reference.semantic_key.starts_with("start:from:") ||
                 reference.semantic_key.starts_with("end:from:"))) {
                actual_text_cap_keys.insert(reference.semantic_key);
            }
            if (reference.semantic_key == "generated:" + profile_text_id) {
                stable_text_boundary_found = true;
            }
        }
        require(stable_text_boundary_found &&
                    actual_text_cap_keys == expected_text_cap_keys,
                "Multi-region profile caps lost unique profile-region ancestry");
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
        auto centered_text_cut_sketch = text_profile_sketch;
        centered_text_cut_sketch.plane_offset = -2.5;
        for (auto& text : centered_text_cut_sketch.texts) {
            for (auto& contour : text.contours) {
                for (auto& point : contour) {
                    point[0] -= 10.5;
                    point[1] -= 6.0;
                }
            }
        }
        text_cut_document.sketches.push_back(std::move(centered_text_cut_sketch));
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
        static_cast<void>(add_revolution_axis(text_revolution_sketch));
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
                    420.0 * std::numbers::pi) < 1.0e-6 &&
                    annulus_results.front().mesh.original_references.axes.size() == 1 &&
                    annulus_results.front().mesh.axes.size() == 1 &&
                    annulus_results.front().mesh.original_references.axes.front()
                        .reference.semantic_key == "axis:primary",
                "Nested circular profile did not produce one visible primary axis");

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
                    400.0 * std::numbers::pi) < 1.0e-6 &&
                    ellipse_profile_results.front().mesh.original_references
                        .axes.size() == 1 &&
                    ellipse_profile_results.front().mesh.axes.size() == 1 &&
                    ellipse_profile_results.front().mesh.original_references
                        .axes.front().reference.semantic_key == "axis:primary",
                "Exact Ellipse extrusion has an incorrect volume or visible axis");
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
        static_cast<void>(add_revolution_axis(ellipse_revolution_sketch));
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
        const auto revolution_axis_id = add_revolution_axis(revolution_sketch);
        const auto revolution_sketch_id = revolution_sketch.id;
        revolution_document.sketches.push_back(std::move(revolution_sketch));
        auto revolution_container =
            zima::document::PartDocument::create_revolution_container(
                revolution_sketch_id);
        revolution_container.revolution.axis_segment_id = revolution_axis_id;
        const auto revolution_owner = revolution_container.id;
        revolution_document.history.push_back(std::move(revolution_container));
        const auto revolution_results =
            kernel.evaluate_history(revolution_document.kernel_operations());
        require(revolution_results.size() == 1 &&
                    std::abs(revolution_results.front().volume -
                        390.0 * std::numbers::pi) < 1.0e-6 &&
                    revolution_results.front().mesh.original_references.axes.size() == 1 &&
                    revolution_results.front().mesh.axes.size() == 1 &&
                    revolution_results.front().mesh.original_references.axes.front().direction.x > 0.999 &&
                    revolution_results.front().mesh.original_references.axes.front()
                        .reference.semantic_key == "axis:primary",
                "Full Sketch Revolution has an incorrect volume or axis");
        auto rounded_revolution_document =
            zima::document::PartDocument::create_default();
        auto rounded_revolution_sketch =
            zima::sketcher::Sketch::create_default();
        const auto rounded_revolution_segments =
            rounded_revolution_sketch.add_rectangle(10.0, 5.0, 20.0, 8.0);
        static_cast<void>(rounded_revolution_sketch.add_corner_fillet(
            rounded_revolution_segments[0],
            rounded_revolution_segments[1], 1.0));
        const auto rounded_revolution_axis =
            add_revolution_axis(rounded_revolution_sketch);
        const auto rounded_revolution_sketch_id =
            rounded_revolution_sketch.id;
        rounded_revolution_document.sketches.push_back(
            std::move(rounded_revolution_sketch));
        auto rounded_revolution_container =
            zima::document::PartDocument::create_revolution_container(
                rounded_revolution_sketch_id);
        rounded_revolution_container.revolution.axis_segment_id =
            rounded_revolution_axis;
        rounded_revolution_document.history.push_back(
            std::move(rounded_revolution_container));
        const auto rounded_revolution_results = kernel.evaluate_history(
            rounded_revolution_document.kernel_operations());
        const auto rounded_revolution_preview =
            rounded_revolution_document.revolution_preview_edges(
                rounded_revolution_document.history.front());
        require(rounded_revolution_results.size() == 1 &&
                    rounded_preview_matches_evaluated_profile(
                        rounded_revolution_document.sketches.front(),
                        rounded_revolution_preview),
                "Rounded Revolution cyan wire diverged from its evaluated profile");
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
        const auto half_revolution_operations =
            half_revolution_document.kernel_operations();
        const auto& half_revolution_request =
            std::get<zima::kernel::RevolutionRequest>(
                half_revolution_operations.front().primitive);
        const auto half_revolution_start_cap =
            profile_cap_key(
                "start", half_revolution_request.profile_region_id);
        const auto half_revolution_end_cap =
            profile_cap_key(
                "end", half_revolution_request.profile_region_id);
        const auto half_revolution_results = kernel.evaluate_history(
            half_revolution_operations);
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
        require(partial_revolution_faces.contains(half_revolution_start_cap) &&
                    partial_revolution_faces.contains(half_revolution_end_cap),
                "Partial Revolution lost its start or end profile face");
        auto two_sided_revolution = revolution_document;
        auto& two_sided_parameters =
            two_sided_revolution.history.front().revolution;
        two_sided_parameters.extent_mode =
            zima::document::ProfileExtentMode::TwoSides;
        two_sided_parameters.angle_degrees = 90.0;
        two_sided_parameters.angle_reverse = 45.0;
        two_sided_parameters.direction =
            zima::document::ExtrusionDirection::Reverse;
        const auto two_sided_operations =
            two_sided_revolution.kernel_operations();
        require(!std::get<zima::kernel::RevolutionRequest>(
                    two_sided_operations.front().primitive).first_cap_is_start,
            "Reverse Revolution did not preserve semantic start/end cap ownership");
        const auto two_sided_results = kernel.evaluate_history(
            two_sided_operations);
        const auto two_sided_preview =
            two_sided_revolution.revolution_preview_edges(
                two_sided_revolution.history.front());
        require(std::abs(two_sided_results.front().volume -
                    146.25 * std::numbers::pi) < 1.0e-6 &&
                    two_sided_preview.size() >= 4,
                "Two-sided reversed Revolution has an incorrect body or cyan wire");
        auto thin_revolution_preview_document = two_sided_revolution;
        thin_revolution_preview_document.history.front().revolution.result_type =
            zima::document::ProfileResultType::Thin;
        thin_revolution_preview_document.history.front().revolution.thin_thickness = 1.0;
        const auto thin_revolution_preview =
            thin_revolution_preview_document.revolution_preview_edges(
                thin_revolution_preview_document.history.front());
        require(!thin_revolution_preview.empty(),
                "Thin Revolution did not publish its offset cyan wire preview");
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
                    loaded_revolution.history.front().revolution.axis_segment_id ==
                        revolution_axis_id &&
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
        const auto torus_axis_id = add_revolution_axis(
            torus_revolution_sketch, true);
        const auto torus_revolution_sketch_id = torus_revolution_sketch.id;
        torus_revolution_document.sketches.push_back(
            std::move(torus_revolution_sketch));
        auto torus_revolution =
            zima::document::PartDocument::create_revolution_container(
                torus_revolution_sketch_id);
        torus_revolution.revolution.axis_segment_id = torus_axis_id;
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
        const auto arc_torus_axis_id = add_revolution_axis(
            arc_torus_sketch, true);
        const auto arc_torus_sketch_id = arc_torus_sketch.id;
        arc_torus_document.sketches.push_back(std::move(arc_torus_sketch));
        auto arc_torus_revolution =
            zima::document::PartDocument::create_revolution_container(
                arc_torus_sketch_id);
        arc_torus_revolution.revolution.axis_segment_id = arc_torus_axis_id;
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
        const auto curved_axis_id = add_revolution_axis(
            curved_holed_revolution_sketch, true);
        const auto curved_holed_revolution_sketch_id =
            curved_holed_revolution_sketch.id;
        curved_holed_revolution_document.sketches.push_back(
            std::move(curved_holed_revolution_sketch));
        auto curved_holed_revolution =
            zima::document::PartDocument::create_revolution_container(
                curved_holed_revolution_sketch_id);
        curved_holed_revolution.revolution.axis_segment_id = curved_axis_id;
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
        static_cast<void>(add_revolution_axis(holed_revolution_sketch));
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
        static_cast<void>(add_revolution_axis(yz_revolution_sketch));
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
        const auto xz_axis_id = add_revolution_axis(
            xz_revolution_sketch, true);
        const auto xz_revolution_sketch_id = xz_revolution_sketch.id;
        xz_revolution_document.sketches.push_back(
            std::move(xz_revolution_sketch));
        auto xz_revolution =
            zima::document::PartDocument::create_revolution_container(
                xz_revolution_sketch_id);
        xz_revolution.revolution.axis_segment_id = xz_axis_id;
        xz_revolution_document.history.push_back(std::move(xz_revolution));
        const auto xz_revolution_results = kernel.evaluate_history(
            xz_revolution_document.kernel_operations());
        require(std::abs(xz_revolution_results.front().volume -
                    390.0 * std::numbers::pi) < 1.0e-6 &&
                    xz_revolution_results.front().mesh.original_references.axes.front().direction.z < -0.999,
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

        auto cursor_document = zima::document::PartDocument::create_default();
        auto cursor_box = zima::document::PartDocument::create_box_container();
        const auto cursor_box_id = cursor_box.id;
        cursor_document.history.push_back(std::move(cursor_box));
        cursor_document.insert_history_entry(
            zima::document::PartHistoryKind::Feature, cursor_box_id);
        auto cursor_point = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        const auto cursor_point_id = cursor_point.id;
        cursor_document.constructions.push_back(std::move(cursor_point));
        cursor_document.insert_history_entry(
            zima::document::PartHistoryKind::Construction, cursor_point_id);
        cursor_document.set_history_cursor(1);
        auto cursor_sphere = zima::document::PartDocument::create_sphere_container();
        const auto cursor_sphere_id = cursor_sphere.id;
        cursor_document.history.push_back(std::move(cursor_sphere));
        cursor_document.insert_history_entry(
            zima::document::PartHistoryKind::Feature, cursor_sphere_id);
        require(cursor_document.effective_history_cursor() == 2 &&
                    cursor_document.history_order.size() == 3 &&
                    cursor_document.history_order[0].id == cursor_box_id &&
                    cursor_document.history_order[1].id == cursor_sphere_id &&
                    cursor_document.history_order[2].id == cursor_point_id,
                "Insert Here cursor did not control unified history insertion");
        cursor_document.set_history_cursor(1);
        auto cursor_cylinder =
            zima::document::PartDocument::create_cylinder_container();
        const auto cursor_cylinder_id = cursor_cylinder.id;
        cursor_document.history.push_back(std::move(cursor_cylinder));
        cursor_document.insert_history_entry(
            zima::document::PartHistoryKind::Feature, cursor_cylinder_id);
        auto cursor_sketch_container =
            zima::document::PartDocument::create_sketch_container();
        const auto cursor_sketch_container_id = cursor_sketch_container.id;
        cursor_document.history.push_back(std::move(cursor_sketch_container));
        cursor_document.insert_history_entry(
            zima::document::PartHistoryKind::Feature,
            cursor_sketch_container_id);
        const auto cursor_operations = cursor_document.kernel_operations();
        require(cursor_document.history_order.size() == 5 &&
                    cursor_document.history_order[0].id == cursor_box_id &&
                    cursor_document.history_order[1].id == cursor_cylinder_id &&
                    cursor_document.history_order[2].id ==
                        cursor_sketch_container_id &&
                    cursor_document.history_order[3].id == cursor_sphere_id &&
                    cursor_document.history_order[4].id == cursor_point_id &&
                    cursor_operations.size() == 3 &&
                    cursor_operations[0].owner_id == cursor_box_id &&
                    cursor_operations[1].owner_id == cursor_cylinder_id &&
                    cursor_operations[2].owner_id == cursor_sphere_id &&
                    cursor_document.body_operation_count_at_history_cursor() == 2,
                "Tree order, body operation order, and Insert Here boundary diverged");
        std::vector<zima::kernel::BodyResult> cursor_boundaries(3);
        cursor_boundaries[0].volume = 1.0;
        cursor_boundaries[1].volume = 2.0;
        cursor_boundaries[2].volume = 3.0;
        zima::document::DocumentSession cursor_session(
            cursor_document, std::move(cursor_boundaries));
        const auto cursor_sphere_rollback =
            cursor_session.rollback_boundary(cursor_sphere_id);
        require(cursor_sphere_rollback &&
                    cursor_sphere_rollback->input_body &&
                    cursor_sphere_rollback->input_body->volume == 2.0,
                "Rollback did not use the unified Tree order before a feature");

        zima::kernel::ViewerReferenceGeometry orientation_geometry;
        orientation_geometry.axes.push_back({{}, {1.0, 0.0, 0.0}, 10.0,
            {"axis-x", "axis", {}}});
        orientation_geometry.axes.push_back({{}, {2.0, 0.0, 0.0}, 10.0,
            {"axis-x-parallel", "axis", {}}});
        orientation_geometry.axes.push_back({{}, {0.0, 1.0, 0.0}, 10.0,
            {"axis-y", "axis", {}}});
        std::vector<zima::document::ConstructionReference> orientation_refs{
            {{}, "axis-x", "axis", 0.0, false, "front", true}};
        const auto one_front_state =
            zima::document::orientation_constraint_state(
                orientation_refs, orientation_geometry, true);
        require(one_front_state.remaining_dof == 1 &&
                    one_front_state.constrained_axes ==
                        std::array<bool, 3>{true, false, true},
                "One FRONT direction did not constrain RX/RZ and leave local RY");
        zima::document::Placement one_front_placement;
        one_front_placement.absolute_rotation_x = 12.0;
        one_front_placement.absolute_rotation_y = 37.0;
        one_front_placement.absolute_rotation_z = 23.0;
        one_front_placement.references = orientation_refs;
        zima::kernel::Vec3 one_front_base;
        require(zima::document::resolve_placement(
                    one_front_placement, orientation_geometry, &one_front_base) &&
                    std::abs(one_front_base.x) < 1.0e-7 &&
                    std::abs(one_front_base.y) < 1.0e-7 &&
                    std::abs(one_front_base.z + 90.0) < 1.0e-7,
                "Single FRONT frame consumed its free RY or reported a "
                "base rotation inconsistent with its local Origin");
        const auto rotate_orientation = [](zima::kernel::Vec3 vector,
                                            const auto& placement) {
            constexpr double radians = std::numbers::pi / 180.0;
            const double rx = placement.rotation_x * radians;
            const double ry = placement.rotation_y * radians;
            const double rz = placement.rotation_z * radians;
            const double cx = std::cos(rx), sx = std::sin(rx);
            const double cy = std::cos(ry), sy = std::sin(ry);
            const double cz = std::cos(rz), sz = std::sin(rz);
            const zima::kernel::Vec3 after_x{vector.x,
                cx * vector.y - sx * vector.z,
                sx * vector.y + cx * vector.z};
            const zima::kernel::Vec3 after_y{
                cy * after_x.x + sy * after_x.z, after_x.y,
                -sy * after_x.x + cy * after_x.z};
            return zima::kernel::Vec3{
                cz * after_y.x - sz * after_y.y,
                sz * after_y.x + cz * after_y.y, after_y.z};
        };
        const auto resolved_front = rotate_orientation(
            {0.0, 1.0, 0.0}, one_front_placement);
        require(resolved_front.x > 0.999999 &&
                    std::abs(resolved_front.y) < 1.0e-7 &&
                    std::abs(resolved_front.z) < 1.0e-7 &&
                    std::abs(one_front_placement.absolute_rotation_x) <
                        1.0e-9 &&
                    std::abs(one_front_placement.absolute_rotation_y - 37.0) <
                        1.0e-9 &&
                    std::abs(one_front_placement.absolute_rotation_z + 90.0) <
                        1.0e-9,
                "Single FRONT did not overwrite constrained absolute RX/RZ "
                "while preserving the editable absolute RY");

        const std::vector<zima::document::ConstructionReference> top_only_refs{
            {{}, "axis-y", "axis", 0.0, false, "top", true}};
        const auto one_top_state =
            zima::document::orientation_constraint_state(
                top_only_refs, orientation_geometry, true);
        require(one_top_state.remaining_dof == 1 &&
                    one_top_state.constrained_axes ==
                        std::array<bool, 3>{true, true, false},
                "One TOP direction did not constrain RX/RY and leave local RZ");
        orientation_refs.push_back(
            {{}, "axis-x-parallel", "axis", 0.0, false, "top", true});
        const auto parallel_state = zima::document::orientation_constraint_state(
            orientation_refs, orientation_geometry, true);
        require(parallel_state.remaining_dof == 1 &&
                    parallel_state.constrained_axes ==
                        std::array<bool, 3>{true, false, true},
                "Parallel second direction incorrectly removed rotational DOF");
        orientation_refs.back() =
            {{}, "axis-y", "axis", 0.0, false, "top", true};
        const auto complete_orientation_state =
            zima::document::orientation_constraint_state(
                orientation_refs, orientation_geometry, true);
        require(complete_orientation_state.remaining_dof == 0 &&
                    complete_orientation_state.constrained_axes ==
                        std::array<bool, 3>{true, true, true},
                "Independent second direction did not fully constrain orientation");
        one_front_placement.references = orientation_refs;
        zima::kernel::Vec3 fully_referenced_base;
        require(zima::document::resolve_placement(one_front_placement,
                    orientation_geometry, &fully_referenced_base) &&
                    std::abs(one_front_placement.absolute_rotation_x -
                        fully_referenced_base.x) < 1.0e-9 &&
                    std::abs(one_front_placement.absolute_rotation_y -
                        fully_referenced_base.y) < 1.0e-9 &&
                    std::abs(one_front_placement.absolute_rotation_z -
                        fully_referenced_base.z) < 1.0e-9 &&
                    std::abs(one_front_placement.absolute_rotation_y - 37.0) >
                        1.0e-6,
                "Second independent reference did not take precedence over "
                "the formerly free absolute rotation");

        auto placement_roundtrip =
            zima::document::PartDocument::create_default();
        auto referenced_box =
            zima::document::PartDocument::create_box_container();
        referenced_box.placement = one_front_placement;
        placement_roundtrip.insert_history_entry(
            zima::document::PartHistoryKind::Feature, referenced_box.id);
        placement_roundtrip.history.push_back(referenced_box);
        const auto placement_roundtrip_path =
            std::filesystem::temp_directory_path() /
            "zima-cad-placement-reference-rotation-contract.prtz";
        placement_roundtrip.save(placement_roundtrip_path);
        const auto loaded_placement_roundtrip =
            zima::document::PartDocument::load(placement_roundtrip_path);
        std::filesystem::remove(placement_roundtrip_path);
        require(loaded_placement_roundtrip.history.size() == 1 &&
                    loaded_placement_roundtrip.history.front().placement ==
                        one_front_placement,
                "Resolved absolute/final placement rotation did not survive "
                "save and reopen");
        auto missing_reference_placement =
            loaded_placement_roundtrip.history.front().placement;
        const auto last_valid_placement = missing_reference_placement;
        require(!zima::document::resolve_placement(
                    missing_reference_placement, {}) &&
                    missing_reference_placement.x == last_valid_placement.x &&
                    missing_reference_placement.y == last_valid_placement.y &&
                    missing_reference_placement.z == last_valid_placement.z &&
                    missing_reference_placement.rotation_x ==
                        last_valid_placement.rotation_x &&
                    missing_reference_placement.rotation_y ==
                        last_valid_placement.rotation_y &&
                    missing_reference_placement.rotation_z ==
                        last_valid_placement.rotation_z &&
                    missing_reference_placement.absolute_rotation_x ==
                        last_valid_placement.absolute_rotation_x &&
                    missing_reference_placement.absolute_rotation_y ==
                        last_valid_placement.absolute_rotation_y &&
                    missing_reference_placement.absolute_rotation_z ==
                        last_valid_placement.absolute_rotation_z,
                "A missing placement reference destroyed the last valid "
                "persisted container transform");

        auto sweep_document = zima::document::PartDocument::create_default();
        auto sweep_container =
            zima::document::PartDocument::create_sweep3d_container();
        auto sweep_first = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        sweep_first.name = "Bod 1";
        sweep_first.parent_construction_id = sweep_container.sweep3d.path.id;
        sweep_first.origin = {0.0, 0.0, 0.0};
        auto sweep_second = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        sweep_second.name = "Bod 2";
        sweep_second.parent_construction_id = sweep_container.sweep3d.path.id;
        sweep_second.origin = {0.0, 0.0, 20.0};
        const auto sweep_first_id = sweep_first.id;
        const auto sweep_second_id = sweep_second.id;
        sweep_container.sweep3d.path.curve_points = {
            sweep_first, sweep_second};
        auto sweep_profile = zima::sketcher::Sketch::create_default();
        sweep_profile.owner_container_id = sweep_container.id;
        static_cast<void>(sweep_profile.add_rectangle(-2.0, -3.0, 2.0, 3.0));
        const auto sweep_profile_sketch_id = sweep_profile.id;
        sweep_container.sweep3d.profiles.push_back({
            "sweep-profile-contract", sweep_first_id,
            sweep_profile_sketch_id, sweep_profile.serialized()});
        require(zima::document::PartDocument::reframe_sweep3d_profile(
                    sweep_container, 0),
                "3D Sweep did not resolve its first profile frame");
        const auto first_profile_geometry =
            zima::sketcher::Sketch::from_serialized(
                sweep_container.sweep3d.profiles.front().sketch_serialized);
        sweep_container.sweep3d.profiles.front().point_id = sweep_second_id;
        require(zima::document::PartDocument::reframe_sweep3d_profile(
                    sweep_container, 0),
                "3D Sweep profile could not be reassigned to another Point");
        const auto reassigned_profile = zima::sketcher::Sketch::from_serialized(
            sweep_container.sweep3d.profiles.front().sketch_serialized);
        require(reassigned_profile.id == first_profile_geometry.id &&
                    reassigned_profile.points == first_profile_geometry.points &&
                    reassigned_profile.segments == first_profile_geometry.segments &&
                    std::abs(reassigned_profile.resolved_origin.z - 20.0) < 1.0e-9,
                "Reassigning a 3D Sweep profile changed its Sketch geometry or identity");
        sweep_container.sweep3d.profiles.front().point_id = sweep_first_id;
        require(zima::document::PartDocument::reframe_sweep3d_profile(
                    sweep_container, 0),
                "3D Sweep profile could not return to its first Point");
        auto sweep_end_profile = zima::sketcher::Sketch::create_default();
        sweep_end_profile.owner_container_id = sweep_container.id;
        static_cast<void>(
            sweep_end_profile.add_rectangle(-2.0, -3.0, 2.0, 3.0));
        sweep_container.sweep3d.profiles.push_back({
            "sweep-profile-contract-end", sweep_second_id,
            sweep_end_profile.id, sweep_end_profile.serialized()});
        require(zima::document::PartDocument::reframe_sweep3d_profile(
                    sweep_container, 1),
                "3D Sweep did not resolve its second profile frame");
        sweep_document.history.push_back(sweep_container);
        sweep_document.resolve_constructions();
        const auto once_resolved_sweep_history = sweep_document.history;
        sweep_document.resolve_constructions();
        require(sweep_document.history == once_resolved_sweep_history,
                "3D Sweep Point/profile resolution is not idempotent");
        const auto sweep_boundaries = kernel.evaluate_history(
            sweep_document.kernel_operations());
        require(sweep_boundaries.size() == 1 &&
                    std::abs(sweep_boundaries.front().volume - 480.0) < 1.0e-5,
                "Straight polyline 3D Sweep through two profiles produced an "
                "incorrect solid volume");

        auto one_profile_sweep = sweep_container;
        one_profile_sweep.sweep3d.profiles.resize(1);
        auto one_profile_document =
            zima::document::PartDocument::create_default();
        one_profile_document.history.push_back(one_profile_sweep);
        const auto one_profile_boundaries = kernel.evaluate_history(
            one_profile_document.kernel_operations());
        require(one_profile_boundaries.size() == 1 &&
                    std::abs(one_profile_boundaries.back().volume - 480.0) <
                        1.0e-5 &&
                    !one_profile_boundaries.back().mesh.triangles.empty(),
                "3D Sweep with one start profile did not create a visible solid");

        auto sweep_boolean_box =
            zima::document::PartDocument::create_box_container();
        sweep_boolean_box.box = {10.0, 10.0, 20.0};
        sweep_boolean_box.placement.x = -5.0;
        sweep_boolean_box.placement.y = -5.0;
        auto additive_sweep_document =
            zima::document::PartDocument::create_default();
        additive_sweep_document.history = {
            sweep_boolean_box, one_profile_sweep};
        const auto additive_sweep_boundaries = kernel.evaluate_history(
            additive_sweep_document.kernel_operations());
        require(additive_sweep_boundaries.size() == 2 &&
                    additive_sweep_boundaries.back().volume > 2000.0 + 1.0e-5 &&
                    additive_sweep_boundaries.back().volume < 2480.0 - 1.0e-5 &&
                    !additive_sweep_boundaries.back().mesh.triangles.empty(),
                "Additive 3D Sweep removed the existing visible solid");

        auto subtractive_sweep = one_profile_sweep;
        subtractive_sweep.combine_mode =
            zima::document::CombineMode::Subtract;
        auto subtractive_sweep_document =
            zima::document::PartDocument::create_default();
        subtractive_sweep_document.history = {
            sweep_boolean_box, subtractive_sweep};
        const auto subtractive_sweep_boundaries = kernel.evaluate_history(
            subtractive_sweep_document.kernel_operations());
        require(subtractive_sweep_boundaries.size() == 2 &&
                    std::abs(subtractive_sweep_boundaries.back().volume -
                        (additive_sweep_boundaries.back().volume - 480.0)) <
                        1.0e-5 &&
                    !subtractive_sweep_boundaries.back().mesh.triangles.empty(),
                "Subtractive 3D Sweep did not cut the existing visible solid");

        auto spline_sweep_document = zima::document::PartDocument::create_default();
        auto spline_sweep_container = sweep_container;
        spline_sweep_container.sweep3d.path.curve_type =
            zima::document::Curve3DType::InterpolatingSpline;
        spline_sweep_document.history.push_back(spline_sweep_container);
        const auto spline_sweep_boundaries = kernel.evaluate_history(
            spline_sweep_document.kernel_operations());
        require(spline_sweep_boundaries.size() == 1 &&
                    std::abs(spline_sweep_boundaries.front().volume - 480.0) <
                        1.0e-5,
                "Interpolating-spline 3D Sweep did not execute its exact "
                "Bezier path through two profiles");

        // A vertical polygon concealed two independent Sweep frame defects:
        // exact Circle/Ellipse profiles need their Sketch-plane normal, and
        // the profile plane must use the very same Bezier derivative as the
        // visible ordinary Curve3D. Exercise both on a genuinely oblique,
        // non-collinear spatial spline.
        auto oblique_sweep_document =
            zima::document::PartDocument::create_default();
        auto oblique_sweep =
            zima::document::PartDocument::create_sweep3d_container();
        oblique_sweep.sweep3d.path.curve_type =
            zima::document::Curve3DType::InterpolatingSpline;
        for (const auto position : std::array{
                 zima::kernel::Vec3{0.0, 0.0, 0.0},
                 zima::kernel::Vec3{15.0, 4.0, 8.0},
                 zima::kernel::Vec3{30.0, 0.0, 16.0}}) {
            auto point = zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Point);
            point.parent_construction_id = oblique_sweep.sweep3d.path.id;
            point.origin = position;
            oblique_sweep.sweep3d.path.curve_points.push_back(
                std::move(point));
        }
        auto oblique_profile = zima::sketcher::Sketch::create_default();
        oblique_profile.owner_container_id = oblique_sweep.id;
        static_cast<void>(oblique_profile.add_circle(0.0, 0.0, 0.75));
        oblique_sweep.sweep3d.profiles.push_back({
            "oblique-sweep-circle-profile",
            oblique_sweep.sweep3d.path.curve_points.front().id,
            oblique_profile.id, oblique_profile.serialized()});
        oblique_sweep_document.history.push_back(std::move(oblique_sweep));
        const auto oblique_operations =
            oblique_sweep_document.kernel_operations();
        const auto* oblique_request =
            std::get_if<zima::kernel::Sweep3DRequest>(
                &oblique_operations.front().primitive);
        require(oblique_request != nullptr &&
                    oblique_request->sections.size() == 1 &&
                    oblique_request->path_segments.size() == 2 &&
                    oblique_request->path_segments.front()
                        .bezier_control_points.size() == 4,
                "Oblique 3D Sweep did not preserve its exact spline request");
        const auto& oblique_section = oblique_request->sections.front();
        const auto& oblique_controls = oblique_request->path_segments.front()
            .bezier_control_points;
        const zima::kernel::Vec3 oblique_derivative{
            oblique_controls[1].x - oblique_controls[0].x,
            oblique_controls[1].y - oblique_controls[0].y,
            oblique_controls[1].z - oblique_controls[0].z};
        const double derivative_length = std::hypot(
            std::hypot(oblique_derivative.x, oblique_derivative.y),
            oblique_derivative.z);
        const double section_normal_length = std::hypot(
            std::hypot(oblique_section.profile_normal.x,
                       oblique_section.profile_normal.y),
            oblique_section.profile_normal.z);
        const double frame_alignment =
            (oblique_derivative.x * oblique_section.profile_normal.x +
             oblique_derivative.y * oblique_section.profile_normal.y +
             oblique_derivative.z * oblique_section.profile_normal.z) /
            (derivative_length * section_normal_length);
        require(frame_alignment > 0.999999 &&
                    std::abs(oblique_section.profile_normal.z) < 0.999 &&
                    std::get_if<
                        zima::kernel::ExtrusionRequest::CircleProfile>(
                        &oblique_section.profile.outer_profile) != nullptr,
                "3D Sweep profile plane diverged from the visible spline "
                "tangent or lost its exact circular profile");
        auto differently_framed_operations = oblique_operations;
        auto& differently_framed_request =
            std::get<zima::kernel::Sweep3DRequest>(
                differently_framed_operations.front().primitive);
        differently_framed_request.sections.front().profile_normal =
            {0.0, 0.0, 1.0};
        require(zima::kernel::history_fingerprint(oblique_operations, 1) !=
                    zima::kernel::history_fingerprint(
                        differently_framed_operations, 1),
                "3D Sweep Sketch-plane normal is missing from the body "
                "calculation fingerprint");
        const auto oblique_sweep_boundaries = kernel.evaluate_history(
            oblique_operations);
        require(oblique_sweep_boundaries.size() == 1 &&
                    std::isfinite(oblique_sweep_boundaries.front().volume) &&
                    oblique_sweep_boundaries.front().volume > 1.0 &&
                    !oblique_sweep_boundaries.front().mesh.triangles.empty(),
                "OCCT did not build the circular profile in its oblique "
                "ZIMA Sketch plane");
        const auto sweep_path = std::filesystem::temp_directory_path() /
            "zima-cad-sweep3d-contract.prtz";
        sweep_document.save(sweep_path, sweep_boundaries);
        std::vector<zima::kernel::BodyResult> loaded_sweep_boundaries;
        const auto loaded_sweep = zima::document::PartDocument::load(
            sweep_path, &loaded_sweep_boundaries);
        std::filesystem::remove(sweep_path);
        require(loaded_sweep.history.size() == 1 &&
                    loaded_sweep.history.front().feature_kind ==
                        zima::document::FeatureKind::Sweep3D &&
                    loaded_sweep.history.front().sweep3d.profiles.size() == 2 &&
                    loaded_sweep.history.front().sweep3d.profiles.front().point_id ==
                        sweep_first_id &&
                    loaded_sweep.history.front().sweep3d.profiles.back().point_id ==
                        sweep_second_id &&
                    loaded_sweep_boundaries.size() == 1 &&
                    std::abs(loaded_sweep_boundaries.front().volume - 480.0) <
                        1.0e-5,
                "3D Sweep path/profile relation did not survive save and reopen");
        auto hole_document = zima::document::PartDocument::create_default();
        auto hole_base = zima::document::PartDocument::create_box_container();
        hole_base.box = {40.0, 40.0, 40.0};
        hole_document.history.push_back(std::move(hole_base));
        auto hole = zima::document::PartDocument::create_hole_container();
        hole.placement.z = -20.0;
        hole.hole.type = zima::document::HoleType::MetricThread;
        hole.hole.diameter = 8.5;
        hole.hole.bore_end_condition = zima::document::EndCondition::ThroughAll;
        hole.hole.bore_length = 40.0;
        hole.hole.thread_enabled = true;
        hole.hole.thread_nominal_diameter = 10.0;
        hole.hole.thread_pitch = 1.5;
        hole.hole.thread_end_condition = zima::document::EndCondition::Length;
        hole.hole.thread_length = 15.0;
        hole_document.history.push_back(hole);
        const auto hole_operations = hole_document.kernel_operations();
        require(hole_operations.size() == 2 &&
                    hole_operations.back().operation ==
                        zima::kernel::BooleanOperation::Subtract &&
                    std::get_if<zima::kernel::FeatureGroupRequest>(
                        &hole_operations.back().primitive) != nullptr,
                "Hole did not translate to one subtractive feature group");
        const auto hole_boundaries = kernel.evaluate_history(hole_operations);
        require(hole_boundaries.size() == 2 &&
                    hole_boundaries.back().volume < 64000.0,
                "Hole did not remove material from its input body");
        const auto hole_path = std::filesystem::temp_directory_path() /
            "zima-cad-hole-contract.prtz";
        hole_document.save(hole_path, hole_boundaries);
        const auto loaded_hole = zima::document::PartDocument::load(hole_path);
        std::filesystem::remove(hole_path);
        require(loaded_hole.history.size() == 2 &&
                    loaded_hole.history.back().feature_kind ==
                        zima::document::FeatureKind::Hole &&
                    loaded_hole.history.back().hole == hole.hole &&
                    loaded_hole.history.back().combine_mode ==
                        zima::document::CombineMode::Subtract,
                "Hole parameters or independent bore/thread lengths did not round-trip");
        auto thread_document = zima::document::PartDocument::create_default();
        auto thread_base = zima::document::PartDocument::create_box_container();
        thread_base.box = {40.0, 40.0, 40.0};
        thread_document.history.push_back(thread_base);
        auto thread = zima::document::PartDocument::create_thread_container();
        thread.placement.z = -20.0;
        thread.thread.nominal_diameter = 10.0;
        thread.thread.pitch = 1.5;
        thread.thread.length_forward = 25.0;
        thread_document.history.push_back(thread);
        const auto thread_operations = thread_document.kernel_operations();
        require(thread_operations.size() == 2 &&
                    std::holds_alternative<zima::kernel::ThreadSurfaceRequest>(
                        thread_operations.back().primitive),
            "Standalone Thread did not enter OCCT as technological sheet history");
        const auto thread_boundaries = kernel.evaluate_history(thread_operations);
        require(thread_boundaries.size() == 2 &&
                    std::abs(thread_boundaries.back().volume -
                        thread_boundaries.front().volume) < 1.0e-7 &&
                    std::ranges::any_of(
                        thread_boundaries.back().mesh.triangle_references,
                        [&](const auto& reference) {
                            return reference.owner_id == thread.id &&
                                reference.semantic_key.starts_with(
                                    "thread:surface:");
                        }),
            "Standalone Thread sheet changed solid volume or was not meshed");
        auto trimmed_thread_operations = thread_operations;
        zima::kernel::BoxRequest thread_cutter{20.0, 20.0, 10.0};
        thread_cutter.translation = {-10.0, -10.0, -10.0};
        trimmed_thread_operations.push_back({"thread-cutter", thread_cutter,
            zima::kernel::BooleanOperation::Subtract});
        const auto trimmed_thread_boundaries =
            kernel.evaluate_history(trimmed_thread_operations);
        const auto& trimmed_thread_mesh = trimmed_thread_boundaries.back().mesh;
        bool has_trimmed_thread_surface = false;
        for (std::size_t triangle = 0;
             triangle < trimmed_thread_mesh.triangle_references.size(); ++triangle) {
            const auto& reference =
                trimmed_thread_mesh.triangle_references[triangle];
            if (reference.owner_id != thread.id ||
                !reference.semantic_key.starts_with("thread:surface:")) continue;
            has_trimmed_thread_surface = true;
            const auto first = trimmed_thread_mesh.triangles[triangle * 3];
            const auto second = trimmed_thread_mesh.triangles[triangle * 3 + 1];
            const auto third = trimmed_thread_mesh.triangles[triangle * 3 + 2];
            const double center_z = (trimmed_thread_mesh.vertices[first].z +
                trimmed_thread_mesh.vertices[second].z +
                trimmed_thread_mesh.vertices[third].z) / 3.0;
            require(center_z <= -10.0 + 1.0e-6 || center_z >= -1.0e-6,
                "A later subtractive operation did not trim the Thread sheet");
        }
        require(has_trimmed_thread_surface,
            "Thread sheet disappeared completely after a partial trim");
        const auto thread_wire = thread_document.thread_edges(thread, nullptr);
        require(thread_wire.size() == 6 &&
                    std::ranges::all_of(thread_wire, [&](const auto& edge) {
                        return edge.reference.owner_id.empty() &&
                            edge.reference.semantic_key.starts_with("thread:wire:") &&
                            edge.display_owner_id == thread.id;
                    }),
            "Standalone Thread is not a non-referenceable four-ring/two-line wire");
        auto external_thread = thread;
        external_thread.thread.side = zima::document::ThreadSide::External;
        external_thread.thread.profile_diameter = 8.1593;
        const auto external_thread_wire =
            thread_document.thread_edges(external_thread, nullptr);
        require(!external_thread_wire.empty() &&
                    std::abs(std::hypot(
                        external_thread_wire.front().points.front().x-
                            external_thread.placement.x,
                        external_thread_wire.front().points.front().y-
                            external_thread.placement.y)-5.0) < 1.0e-6,
                "External Thread did not use its nominal continuous cylinder");
        auto internal_thread = thread;
        internal_thread.thread.side = zima::document::ThreadSide::Internal;
        const auto internal_thread_wire =
            thread_document.thread_edges(internal_thread, nullptr);
        require(!internal_thread_wire.empty() &&
                    std::abs(std::hypot(
                        internal_thread_wire.front().points.front().x-
                            internal_thread.placement.x,
                        internal_thread_wire.front().points.front().y-
                            internal_thread.placement.y)-
                            internal_thread.thread.profile_diameter*0.5) < 1.0e-6,
                "Internal Thread did not use its root continuous cylinder");
        auto axis_plane_thread = thread;
        axis_plane_thread.placement = {};
        zima::document::ConstructionReference thread_axis_reference;
        thread_axis_reference.owner_id = "source-axis";
        thread_axis_reference.semantic_key = "axis:primary";
        zima::document::ConstructionReference thread_plane_reference;
        thread_plane_reference.owner_id = "source-face";
        thread_plane_reference.semantic_key = "z_max";
        thread_plane_reference.supports_offset = true;
        axis_plane_thread.placement.references = {
            thread_axis_reference, thread_plane_reference};
        const auto axis_plane_wire =
            thread_document.thread_edges(axis_plane_thread, nullptr);
        require(axis_plane_wire.size() == 6 &&
                    axis_plane_wire[1].points.front().y < -24.999,
            "Axis + Plane Thread forward direction did not enter the "
            "FRONT side consistently");
        axis_plane_thread.thread.direction =
            zima::document::ExtrusionDirection::Reverse;
        const auto reversed_axis_plane_wire =
            thread_document.thread_edges(axis_plane_thread, nullptr);
        require(reversed_axis_plane_wire.size() == 6 &&
                    reversed_axis_plane_wire[1].points.front().y > 24.999,
            "Axis + Plane Thread reverse direction did not invert its wire");
        auto bidirectional_thread_document = thread_document;
        auto& bidirectional_thread =
            bidirectional_thread_document.history.back().thread;
        bidirectional_thread.extent_mode =
            zima::document::ProfileExtentMode::TwoSides;
        bidirectional_thread.length_forward = 20.0;
        bidirectional_thread.length_reverse = 5.0;
        bidirectional_thread.end_condition_forward =
            zima::document::EndCondition::Length;
        bidirectional_thread.end_condition_reverse =
            zima::document::EndCondition::Length;
        bidirectional_thread.runout_pitch_factor = 2.0;
        bidirectional_thread.side = zima::document::ThreadSide::Internal;
        const auto bidirectional_operations =
            bidirectional_thread_document.kernel_operations();
        const auto& bidirectional_request =
            std::get<zima::kernel::ThreadSurfaceRequest>(
                bidirectional_operations.back().primitive);
        require(std::abs(bidirectional_request.start_offset + 5.0) < 1.0e-9 &&
                    std::abs(bidirectional_request.length - 25.0) < 1.0e-9 &&
                    std::abs(bidirectional_request.runout_start - 3.0) < 1.0e-9 &&
                    std::abs(bidirectional_request.runout_end - 3.0) < 1.0e-9 &&
                    bidirectional_request.side ==
                        zima::kernel::ThreadSurfaceRequest::Side::Internal,
            "Two-sided Thread did not map extents, side and 2xP runouts to its sheet request");
        const auto thread_path = std::filesystem::temp_directory_path() /
            "zima-cad-thread-contract.prtz";
        require(thread_boundaries.front().source_fingerprint ==
                    zima::kernel::history_fingerprint(
                        thread_document.kernel_operations(), 1),
            "Standalone Thread changed the preceding body fingerprint");
        thread_document.save(thread_path, thread_boundaries);
        const auto loaded_thread = zima::document::PartDocument::load(thread_path);
        std::filesystem::remove(thread_path);
        require(loaded_thread.history.size() == 2 &&
                    loaded_thread.history.back().feature_kind ==
                        zima::document::FeatureKind::Thread &&
                    loaded_thread.history.back().thread == thread.thread,
            "Standalone Thread did not survive save and reopen");
        std::cout << "C++ document and OCCT contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

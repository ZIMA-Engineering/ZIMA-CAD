#include <zima/assembly/assembly_document.hpp>
#include <zima/assembly/assembly_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/viewer/picking.hpp>

#include <iostream>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        const auto fixture_dir = std::filesystem::current_path() /
            "tests/fixtures/cross_language";
        const auto fixture_assembly = zima::assembly::AssemblyDocument::load(
            fixture_dir / "nested.asmz");
        require(fixture_assembly.document_id == "assembly-fixture-001" &&
                    fixture_assembly.name == "Fixture Assembly" &&
                    fixture_assembly.components.size() == 1 &&
                    fixture_assembly.components.front().nested_snapshot.size() == 1 &&
                    fixture_assembly.components.front().nested_snapshot.front()
                        .source_document_id == "part-fixture-001",
                "Python nested Assembly fixture lost occurrence identity");
        const zima::assembly::InstancePath nested_path{{"top", "sub", "part"}};
        const auto parent_path = nested_path.parent();
        require(parent_path &&
                    parent_path->occurrence_ids ==
                        std::vector<std::string>{"top", "sub"} &&
                    parent_path->parent() &&
                    parent_path->parent()->occurrence_ids ==
                        std::vector<std::string>{"top"} &&
                    !parent_path->parent()->parent(),
                "Select Parent did not move exactly one instance-path level");
        zima::kernel::OcctKernel kernel;

        // Edit/regenerate/reopen: append a new Part occurrence to the
        // Python-produced nested Assembly fixture, save it, and reopen it to
        // prove the fixture also survives an explicit edit-regenerate-reopen
        // cycle, matching the Part-level coverage in contract_tests.cpp.
        auto edited_fixture_assembly = fixture_assembly;
        const auto fixture_edit_source = kernel.evaluate_history({
            {"fixture-edit-source-container",
             zima::kernel::BoxRequest{6.0, 6.0, 6.0},
             zima::kernel::BooleanOperation::Add},
        });
        auto fixture_edit_occurrence =
            zima::assembly::AssemblyDocument::create_part_occurrence(
                "Fixture Edit Part", "part-fixture-001", "part.prtz",
                fixture_edit_source.back());
        fixture_edit_occurrence.placement.x = 12.0;
        const auto fixture_edit_occurrence_id =
            fixture_edit_occurrence.occurrence_id;
        edited_fixture_assembly.components.push_back(
            std::move(fixture_edit_occurrence));
        const auto fixture_edit_assembly_path =
            std::filesystem::temp_directory_path() /
            "zima-cad-fixture-assembly-edit-regenerate-reopen-contract.asmz";
        edited_fixture_assembly.save(fixture_edit_assembly_path);
        const auto reopened_fixture_assembly =
            zima::assembly::AssemblyDocument::load(fixture_edit_assembly_path);
        std::filesystem::remove(fixture_edit_assembly_path);
        require(reopened_fixture_assembly.document_id == "assembly-fixture-001" &&
                    reopened_fixture_assembly.components.size() == 2 &&
                    reopened_fixture_assembly.components.back().occurrence_id ==
                        fixture_edit_occurrence_id &&
                    reopened_fixture_assembly.components.back()
                        .source_document_id == "part-fixture-001" &&
                    reopened_fixture_assembly.components.back().placement.x == 12.0 &&
                    std::abs(reopened_fixture_assembly.components.back()
                        .calculated_source.volume - 216.0) < 1.0e-6,
                "Edited Python Assembly fixture did not survive "
                "regenerate/save/reopen");

        const auto source = kernel.evaluate_history({
            {"same-source-container", zima::kernel::BoxRequest{10.0, 10.0, 10.0},
             zima::kernel::BooleanOperation::Add},
        });
        auto assembly = zima::assembly::AssemblyDocument::create_default();
        auto first = zima::assembly::AssemblyDocument::create_part_occurrence(
            "První", "same-part-document", "same.prtz", source.back());
        auto second = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Druhý", "same-part-document", "same.prtz", source.back());
        first.placement.z = 5.0;
        second.placement.z = 30.0;
        const std::string first_id = first.occurrence_id;
        const std::string second_id = second.occurrence_id;
        assembly.components.push_back(std::move(first));
        assembly.components.push_back(std::move(second));
        const auto scene = assembly.build_scene();
        const std::set<std::string> expected_instance_paths{
            zima::assembly::InstancePath{}.child(first_id).encoded(),
            zima::assembly::InstancePath{}.child(second_id).encoded()};
        std::set<std::string> display_wire_paths;
        std::size_t component_wire_count = 0;
        for (const auto& edge : scene.edges) {
            if (!expected_instance_paths.contains(edge.reference.instance_path))
                continue;
            ++component_wire_count;
            require(!edge.reference.valid(),
                    "Assembly display wire gained persistent topology ownership "
                    "or lost occurrence identity");
            display_wire_paths.insert(edge.reference.instance_path);
        }
        require(component_wire_count == source.back().mesh.edges.size() * 2 &&
                    display_wire_paths == expected_instance_paths,
                "Assembly display wires do not carry their exact occurrence paths");
        std::set<std::string> display_triangle_paths;
        std::size_t component_triangle_count = 0;
        for (const auto& reference : scene.triangle_references) {
            if (!expected_instance_paths.contains(reference.instance_path))
                continue;
            ++component_triangle_count;
            require(!reference.valid(),
                    "Assembly display triangles gained persistent topology ownership "
                    "or lost occurrence identity");
            display_triangle_paths.insert(reference.instance_path);
        }
        require(component_triangle_count ==
                    source.back().mesh.triangle_references.size() * 2 &&
                    display_triangle_paths == expected_instance_paths,
                "Assembly display triangles do not carry their exact occurrence paths");
        std::set<std::string> instance_paths;
        for (const auto& reference : scene.original_references.triangle_references) {
            if (reference.owner_id == assembly.document_id + ":origin") continue;
            require(reference.owner_id == "same-source-container" &&
                        !reference.instance_path.empty(),
                    "Assembly changed source ownership or lost occurrence identity");
            instance_paths.insert(reference.instance_path);
        }
        require(instance_paths.size() == 2,
                "Repeated source Part occurrences collapsed into one identity");
        std::set<std::string> assembly_axis_paths;
        for (const auto& axis : scene.original_references.axes) {
            if (axis.reference.owner_id == assembly.document_id + ":origin") continue;
            assembly_axis_paths.insert(axis.reference.instance_path);
        }
        require(assembly_axis_paths == instance_paths,
                "Assembly did not transform and distinguish occurrence axes");
        const auto rollback_scene = assembly.build_scene_with_part_override(
            first_id, zima::kernel::BodyResult{});
        std::set<std::string> rollback_paths;
        for (const auto& reference : rollback_scene.original_references.triangle_references) {
            if (reference.owner_id == assembly.document_id + ":origin") continue;
            rollback_paths.insert(reference.instance_path);
        }
        require(rollback_paths.size() == 1 && rollback_paths.contains(
                    zima::assembly::InstancePath{}.child(second_id).encoded()),
                "Part rollback replaced more than the exact active occurrence");
        require(instance_paths.contains(
                    zima::assembly::InstancePath{}.child(first_id).encoded()) &&
                    instance_paths.contains(
                        zima::assembly::InstancePath{}.child(second_id).encoded()),
                "Assembly scene does not use stable occurrence paths");
        const auto candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                scene, {5.0, 5.0, 100.0}, {0.0, 0.0, -1.0}, 0.1),
            {zima::viewer::CandidateKind::Container});
        require(candidates.size() == 2 &&
                    candidates[0].instance_path != candidates[1].instance_path &&
                    candidates[0].owner_id == candidates[1].owner_id &&
                    candidates[0].geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference &&
                    candidates[1].geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference,
                "Viewer did not distinguish repeated occurrences of one source owner");
        const auto occurrence_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                scene, {5.0, 5.0, 100.0}, {0.0, 0.0, -1.0}, 0.1),
            {zima::viewer::CandidateKind::Occurrence});
        require(occurrence_candidates.size() == 2 &&
                    occurrence_candidates[0].owner_id.empty() &&
                    occurrence_candidates[0].instance_path !=
                        occurrence_candidates[1].instance_path,
                "Assembly default selection did not offer leaf Part occurrences");
        const auto nested = zima::assembly::InstancePath{}
            .child("a:b").child("c").encoded();
        require(nested == "3:a:b1:c",
                "Length-prefixed instance path encoding is ambiguous");
        require(zima::assembly::InstancePath::decode(nested).occurrence_ids ==
                    std::vector<std::string>{"a:b", "c"},
                "Length-prefixed instance path did not round-trip");
        bool malformed_path_rejected = false;
        try {
            static_cast<void>(zima::assembly::InstancePath::decode("4:abc"));
        } catch (const std::invalid_argument&) {
            malformed_path_rejected = true;
        }
        require(malformed_path_rejected,
                "Malformed instance path was accepted");
        const auto assembly_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-assembly-contract.asmz";
        assembly.user_parameters = {{"clearance", "0.15 mm"}};
        assembly.user_parameter_order = {"clearance"};
        assembly.user_parameter_labels["clearance"]["en"] = "Clearance";
        assembly.user_parameter_values["clearance"][""] = "0.15 mm";
        assembly.relations = {{"double_clearance", "clearance * 2"}};
        assembly.document_units["Length"] = "in";
        assembly.document_precision["mesh_deflection"] = "0.05";
        assembly.physical_parameters["MATERIAL_NAME"] = "Steel";
        assembly.family_table = R"({"columns":[],"instances":[]})";
        auto assembly_sketch = zima::sketcher::Sketch::create_default();
        assembly.sketches.push_back(assembly_sketch);
        auto cut_definition = zima::document::PartDocument::create_extrusion_container(
            assembly_sketch.id);
        cut_definition.combine_mode = zima::document::CombineMode::Subtract;
        cut_definition.extrusion.extent = zima::document::ExtrusionExtent::ThroughAll;
        assembly.cuts.push_back({cut_definition, {first_id, second_id}});
        assembly.cuts.back().input_component_bodies.emplace(
            first_id, assembly.components.front().calculated_source);
        assembly.cuts.back().input_component_bodies.emplace(
            second_id, assembly.components.back().calculated_source);
        assembly.save(assembly_path);
        std::ifstream assembly_file(assembly_path);
        const std::string assembly_text(
            std::istreambuf_iterator<char>(assembly_file), {});
        require(assembly_text.find("[Document]\n") != std::string::npos &&
                    assembly_text.find("format_version=11\n") != std::string::npos &&
                    assembly_text.find("[DocumentUnits]\n") != std::string::npos &&
                    assembly_text.find("[DocumentPrecision]\n") != std::string::npos &&
                    assembly_text.find("[Material]\n") != std::string::npos &&
                    assembly_text.find("[UserParameters]\n") != std::string::npos &&
                    assembly_text.find("[UserParameterLabels]\n") != std::string::npos &&
                    assembly_text.find("[UserParameterValues]\n") != std::string::npos &&
                    assembly_text.find("[Relations]\n") != std::string::npos &&
                    assembly_text.find("[Containers]\n") != std::string::npos &&
                    assembly_text.find("[CachedBodies]\n") != std::string::npos &&
                    assembly_text.find("[Container." + assembly.document_id + "]\n") !=
                        std::string::npos &&
                    assembly_text.find("[Container." + first_id + "]\n") !=
                        std::string::npos &&
                    assembly_text.find("[Container." + second_id + "]\n") !=
                        std::string::npos &&
                    assembly_text.find("[Entity." + cut_definition.feature_id + "]\n") !=
                        std::string::npos &&
                    assembly_text.find("[Children." + cut_definition.id + "]\n") !=
                        std::string::npos &&
                    !assembly_text.empty() && assembly_text.front() == '[',
                "Assembly save did not produce Python-compatible INI sections");
        const auto loaded = zima::assembly::AssemblyDocument::load(assembly_path);
        assembly_file.close();
        std::filesystem::remove(assembly_path);
        require(loaded.document_id == assembly.document_id &&
                    loaded.components.size() == 2 &&
                    loaded.components.front().occurrence_id == first_id &&
                    loaded.components.back().occurrence_id == second_id &&
                    loaded.components.back().source_document_id == "same-part-document" &&
                    loaded.components.back().source_kind ==
                        zima::assembly::ComponentSourceKind::Part &&
                    loaded.components.back().placement.z == 30.0 &&
                    loaded.user_parameters == assembly.user_parameters &&
                    loaded.user_parameter_order == assembly.user_parameter_order &&
                    loaded.user_parameter_labels == assembly.user_parameter_labels &&
                    loaded.user_parameter_values == assembly.user_parameter_values &&
                    loaded.relations == assembly.relations &&
                    loaded.document_units == assembly.document_units &&
                    loaded.document_precision == assembly.document_precision &&
                    loaded.physical_parameters == assembly.physical_parameters &&
                    loaded.family_table == assembly.family_table &&
                    loaded.sketches.size() == 1 &&
                    loaded.sketches.front().serialized() ==
                        assembly.sketches.front().serialized() &&
                    loaded.cuts == assembly.cuts &&
                    loaded.cuts.front().input_component_bodies.size() == 2 &&
                    loaded.cuts.front().input_component_bodies.at(first_id).volume ==
                        assembly.components.front().calculated_source.volume &&
                    loaded.find_cut(cut_definition.id) != nullptr,
                "Assembly identity and placement did not survive save/load");
        const auto loaded_scene = loaded.build_scene();
        std::set<std::string> loaded_paths;
        for (const auto& reference : loaded_scene.original_references.triangle_references) {
            if (reference.semantic_key.starts_with("origin:")) continue;
            loaded_paths.insert(reference.instance_path);
        }
        require(loaded_paths == instance_paths,
                "Assembly save/load changed stable occurrence paths");
        auto nested_source = zima::assembly::AssemblyDocument::create_default();
        nested_source.components.push_back(
            zima::assembly::AssemblyDocument::create_part_occurrence(
                "Vnořený díl", "nested-part-document", "nested.prtz",
                source.back()));
        auto nested_parent = zima::assembly::AssemblyDocument::create_default();
        auto nested_occurrence =
            zima::assembly::AssemblyDocument::create_assembly_occurrence(
                "Vnořená sestava", "nested-assembly-document", "nested.asmz",
                nested_source);
        const auto nested_occurrence_id = nested_occurrence.occurrence_id;
        nested_parent.components.push_back(std::move(nested_occurrence));
        const auto nested_file_path = std::filesystem::current_path() /
            "zima-cad-cpp-nested-assembly-contract.asmz";
        nested_parent.save(nested_file_path);
        std::ifstream nested_file(nested_file_path);
        const std::string nested_text(
            std::istreambuf_iterator<char>(nested_file), {});
        const auto loaded_nested =
            zima::assembly::AssemblyDocument::load(nested_file_path);
        nested_file.close();
        std::filesystem::remove(nested_file_path);
        require(nested_text.find("[Container." + nested_occurrence_id + "]\n") !=
                    std::string::npos &&
                    loaded_nested.components.size() == 1 &&
                    loaded_nested.components.front().source_kind ==
                        zima::assembly::ComponentSourceKind::Assembly &&
                    loaded_nested.components.front().nested_snapshot.size() == 1 &&
                    loaded_nested.components.front().nested_snapshot.front()
                            .source_document_id == "nested-part-document",
                "Nested Assembly occurrence did not survive INI save/load");
        auto repeated_inner = zima::assembly::AssemblyDocument::create_default();
        auto inner_part_a = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Repeated inner part A", "repeated-part-document", "repeated.prtz",
            source.back());
        auto inner_part_b = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Repeated inner part B", "repeated-part-document", "repeated.prtz",
            source.back());
        inner_part_a.placement.x = 10.0;
        inner_part_b.placement.x = 30.0;
        repeated_inner.components.push_back(std::move(inner_part_a));
        repeated_inner.components.push_back(std::move(inner_part_b));

        auto repeated_middle = zima::assembly::AssemblyDocument::create_default();
        auto middle_inner_a =
            zima::assembly::AssemblyDocument::create_assembly_occurrence(
                "Repeated inner assembly A", "repeated-inner-document",
                "repeated-inner.asmz", repeated_inner);
        auto middle_inner_b =
            zima::assembly::AssemblyDocument::create_assembly_occurrence(
                "Repeated inner assembly B", "repeated-inner-document",
                "repeated-inner.asmz", repeated_inner);
        middle_inner_a.placement.y = 20.0;
        middle_inner_b.placement.y = 40.0;
        const auto middle_inner_a_id = middle_inner_a.occurrence_id;
        const auto middle_inner_b_id = middle_inner_b.occurrence_id;
        repeated_middle.components.push_back(std::move(middle_inner_a));
        repeated_middle.components.push_back(std::move(middle_inner_b));

        auto repeated_top = zima::assembly::AssemblyDocument::create_default();
        auto top_middle_a =
            zima::assembly::AssemblyDocument::create_assembly_occurrence(
                "Repeated middle assembly A", "repeated-middle-document",
                "repeated-middle.asmz", repeated_middle);
        auto top_middle_b =
            zima::assembly::AssemblyDocument::create_assembly_occurrence(
                "Repeated middle assembly B", "repeated-middle-document",
                "repeated-middle.asmz", repeated_middle);
        top_middle_a.placement.x = 100.0;
        top_middle_b.placement.x = 200.0;
        const auto top_middle_a_id = top_middle_a.occurrence_id;
        const auto top_middle_b_id = top_middle_b.occurrence_id;
        repeated_top.components.push_back(std::move(top_middle_a));
        repeated_top.components.push_back(std::move(top_middle_b));

        auto cut_target_a = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Cut target A", "repeated-part-document", "repeated.prtz", source.back());
        auto cut_target_b = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Cut target B", "repeated-part-document", "repeated.prtz", source.back());
        auto cut_excluded = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Excluded target", "repeated-part-document", "repeated.prtz", source.back());
        cut_target_a.placement.x = 0.0;
        cut_target_b.placement.x = 30.0;
        cut_excluded.placement.x = 60.0;
        const auto cut_target_a_id = cut_target_a.occurrence_id;
        const auto cut_target_b_id = cut_target_b.occurrence_id;
        const auto cut_excluded_id = cut_excluded.occurrence_id;
        repeated_top.components.push_back(std::move(cut_target_a));
        repeated_top.components.push_back(std::move(cut_target_b));
        repeated_top.components.push_back(std::move(cut_excluded));

        const auto repeated_scene = repeated_top.build_scene();
        std::set<std::string> repeated_paths;
        for (const auto& reference :
             repeated_scene.original_references.triangle_references) {
            if (reference.owner_id == "same-source-container") {
                repeated_paths.insert(reference.instance_path);
            }
        }
        require(repeated_paths.size() == 11,
                "Nested repeated sources did not retain distinct occurrence paths");
        for (const auto& path : repeated_paths) {
            require(zima::assembly::InstancePath::decode(path).occurrence_ids.size() ==
                        3 ||
                    zima::assembly::InstancePath::decode(path).occurrence_ids.size() ==
                        1,
                "Nested occurrence path did not preserve its hierarchy");
        }
        const auto& middle_snapshot =
            repeated_top.components.front().nested_snapshot;
        require(middle_snapshot.size() == 2 &&
                    middle_snapshot.front().placement.x == 0.0 &&
                    middle_snapshot.back().placement.x == 0.0 &&
                    middle_snapshot.front().children.size() == 2 &&
                    middle_snapshot.front().children.front().placement.x == 10.0 &&
                    middle_snapshot.front().children.back().placement.x == 30.0 &&
                    repeated_top.find_occurrence(top_middle_a_id)->placement.x == 100.0 &&
                    repeated_top.find_occurrence(top_middle_b_id)->placement.x == 200.0,
                "Parent Assembly took ownership of nested internal placement");
        auto cut_sketch = zima::sketcher::Sketch::create_default();
        repeated_top.sketches.push_back(cut_sketch);
        auto repeated_cut =
            zima::document::PartDocument::create_extrusion_container(cut_sketch.id);
        repeated_cut.combine_mode = zima::document::CombineMode::Subtract;
        repeated_cut.extrusion.extent = zima::document::ExtrusionExtent::ThroughAll;
        repeated_top.cuts.push_back({
            std::move(repeated_cut), {cut_target_a_id, cut_target_b_id}, {}});
        repeated_top.cuts.back().input_component_bodies.emplace(
            cut_target_a_id, repeated_top.find_occurrence(cut_target_a_id)
                ->calculated_source);
        repeated_top.cuts.back().input_component_bodies.emplace(
            cut_target_b_id, repeated_top.find_occurrence(cut_target_b_id)
                ->calculated_source);
        static_cast<void>(repeated_top.build_scene());

        const auto cutter = kernel.make_box({50.0, 20.0, 5.0});
        std::map<std::string, zima::kernel::BodyResult> cut_results;
        for (const auto& target_id : repeated_top.cuts.back().target_occurrence_ids) {
            const auto* target = repeated_top.find_occurrence(target_id);
            require(target != nullptr && target_id != cut_excluded_id,
                    "Assembly cut target list did not exclude the exception occurrence");
            cut_results.emplace(target_id, kernel.subtract_bodies(
                target->calculated_source, cutter,
                {target->placement.x, target->placement.y, target->placement.z},
                {target->placement.rotation_x, target->placement.rotation_y,
                 target->placement.rotation_z}));
        }
        require(cut_results.size() == 2 &&
                    cut_results.at(cut_target_a_id).volume <
                        repeated_top.find_occurrence(cut_target_a_id)
                            ->calculated_source.volume &&
                    cut_results.at(cut_target_b_id).volume <
                        repeated_top.find_occurrence(cut_target_b_id)
                            ->calculated_source.volume &&
                    repeated_top.find_occurrence(cut_excluded_id)
                            ->calculated_source.volume == source.back().volume,
                "Assembly cut changed a non-target occurrence");
        bool nested_cut_rejected = false;
        repeated_top.cuts.back().target_occurrence_ids.push_back(top_middle_a_id);
        try {
            static_cast<void>(repeated_top.build_scene());
        } catch (const std::runtime_error&) {
            nested_cut_rejected = true;
        }
        require(!nested_cut_rejected,
                "Assembly cut rejected a nested Assembly component occurrence");
        repeated_top.cuts.back().target_occurrence_ids.pop_back();
        require(middle_inner_a_id != middle_inner_b_id &&
                    top_middle_a_id != top_middle_b_id,
                "Repeated Assembly sources reused occurrence identity");
        auto broken_cut_target = loaded;
        broken_cut_target.cuts.front().definition.extrusion.extent =
            zima::document::ExtrusionExtent::UpToPlane;
        broken_cut_target.cuts.front().definition.extrusion.target_face = {
            "missing-owner", "missing-face",
            zima::assembly::InstancePath{}.child(first_id).encoded()};
        bool broken_cut_target_rejected = false;
        try {
            static_cast<void>(broken_cut_target.build_scene());
        } catch (const std::runtime_error&) {
            broken_cut_target_rejected = true;
        }
        require(broken_cut_target_rejected,
                "Assembly accepted a cut with a missing persisted target face");
        const auto point_for_path = [&](const std::string& path) {
            const auto found = std::find_if(
                loaded_scene.original_references.points.begin(),
                loaded_scene.original_references.points.end(),
                [&](const auto& point) {
                    return point.reference.instance_path == path;
                });
            if (found == loaded_scene.original_references.points.end()) {
                throw std::runtime_error("Original solid point reference is missing");
            }
            return *found;
        };
        const auto dependent_point_source =
            point_for_path(zima::assembly::InstancePath{}.child(second_id).encoded());
        const auto prerequisite_point_source =
            point_for_path(zima::assembly::InstancePath{}.child(first_id).encoded());

        // Embedded placement-reference table (component.placement_references,
        // the Python-style reference stored directly on the occurrence rather
        // than a separate top-level Mate object): PointCoincident row placed
        // on the second occurrence, referencing the first occurrence's point,
        // solved via calculate_placement_references() and round-tripped
        // through save/load exactly like the top-level mates vector above.
        auto placement_reference_assembly = loaded;
        auto placement_second_it = std::find_if(
            placement_reference_assembly.components.begin(),
            placement_reference_assembly.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        require(placement_second_it != placement_reference_assembly.components.end(),
                "Second occurrence missing before placement-reference test");
        auto* placement_second = &*placement_second_it;
        placement_second->placement_references.push_back(
            zima::assembly::ComponentPlacementReference{
                zima::assembly::MateKind::PointCoincident,
                {zima::assembly::MateReferenceKind::Point,
                 zima::assembly::InstancePath{}.child(second_id),
                 dependent_point_source.reference.owner_id,
                 dependent_point_source.reference.semantic_key},
                {zima::assembly::MateReferenceKind::Point,
                 zima::assembly::InstancePath{}.child(first_id),
                 prerequisite_point_source.reference.owner_id,
                 prerequisite_point_source.reference.semantic_key},
                0.0, false});
        placement_reference_assembly.calculate_placement_references();
        auto placement_solved_it = std::find_if(
            placement_reference_assembly.components.begin(),
            placement_reference_assembly.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        const auto* placement_solved = placement_solved_it !=
            placement_reference_assembly.components.end() ? &*placement_solved_it : nullptr;
        require(placement_solved != nullptr &&
                    std::abs(placement_solved->placement.x - first.placement.x) < 1.0e-7 &&
                    std::abs(placement_solved->placement.y - first.placement.y) < 1.0e-7 &&
                    std::abs(placement_solved->placement.z - first.placement.z) < 1.0e-7,
                "calculate_placement_references() did not align the occurrence "
                "with its embedded PointCoincident reference");
        const auto placement_reference_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-placement-reference-contract.asmz";
        placement_reference_assembly.save(placement_reference_path);
        const auto loaded_placement_reference =
            zima::assembly::AssemblyDocument::load(placement_reference_path);
        std::filesystem::remove(placement_reference_path);
        auto reloaded_placement_component_it = std::find_if(
            loaded_placement_reference.components.begin(),
            loaded_placement_reference.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        const auto* reloaded_placement_component = reloaded_placement_component_it !=
            loaded_placement_reference.components.end() ? &*reloaded_placement_component_it : nullptr;
        require(reloaded_placement_component != nullptr &&
                    reloaded_placement_component->placement_references ==
                        placement_solved->placement_references,
                "Embedded placement_references did not survive save/load");
        // The removed top-level add_mate() ownership rejection no longer
        // applies: per-component rows have no shared Assembly-wide mate ID /
        // parent-ownership validation layer to reject nested storage up front.
        const auto resolved_dependent_point =
            placement_reference_assembly.resolve_point(
                placement_solved->placement_references.front().component_reference);
        const auto resolved_prerequisite_point =
            placement_reference_assembly.resolve_point(
                placement_solved->placement_references.front().target_reference);
        require(std::abs(resolved_dependent_point.point.x -
                             resolved_prerequisite_point.point.x) < 1.0e-7 &&
                    std::abs(resolved_dependent_point.point.y -
                             resolved_prerequisite_point.point.y) < 1.0e-7 &&
                    std::abs(resolved_dependent_point.point.z -
                             resolved_prerequisite_point.point.z) < 1.0e-7 &&
                    placement_reference_assembly.remaining_degrees_of_freedom(second_id) == 3,
                "Point placement reference did not align persisted original-solid points");
        auto redundant_point_assembly = placement_reference_assembly;
        auto redundant_point_it = std::find_if(
            redundant_point_assembly.components.begin(),
            redundant_point_assembly.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        require(redundant_point_it != redundant_point_assembly.components.end(),
                "Second occurrence missing before redundant point-reference test");
        redundant_point_it->placement_references.push_back(
            placement_solved->placement_references.front());
        redundant_point_assembly.calculate_placement_references();
        require(redundant_point_assembly.remaining_degrees_of_freedom(second_id) == 3,
                "Redundant placement reference was counted as three new constraints");

        auto placement_reference_angle_assembly = loaded;
        auto placement_angle_component_it = std::find_if(
            placement_reference_angle_assembly.components.begin(),
            placement_reference_angle_assembly.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        require(placement_angle_component_it !=
                    placement_reference_angle_assembly.components.end(),
                "Second occurrence missing before placement-reference dimension test");
        placement_angle_component_it->placement_references.clear();
        placement_angle_component_it->placement_references.push_back(
            {zima::assembly::MateKind::AxisAngle,
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "axis:z"},
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "axis:z"},
             45.0, false});
        placement_reference_angle_assembly.calculate_placement_references();
        const auto placement_angle_scene =
            placement_reference_angle_assembly.build_scene();
        const std::string expected_placement_dimension_key =
            "placement-reference:" + second_id + ":0";
        auto placement_dimension_it = std::find_if(
            placement_angle_scene.dimensions.begin(),
            placement_angle_scene.dimensions.end(),
            [&](const auto& dimension) {
                return dimension.reference.semantic_key ==
                    expected_placement_dimension_key;
            });
        require(placement_dimension_it != placement_angle_scene.dimensions.end() &&
                    placement_dimension_it->unit_suffix == " °" &&
                    std::abs(placement_dimension_it->value - 45.0) < 1.0e-7,
                "build_scene() did not emit a dimension overlay for the "
                "embedded AxisAngle placement reference");

        auto angled_assembly = loaded;
        auto angled_component_it = std::find_if(
            angled_assembly.components.begin(), angled_assembly.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        require(angled_component_it != angled_assembly.components.end(),
                "Second occurrence missing before axis-angle test");
        angled_component_it->placement_references.push_back(
            {zima::assembly::MateKind::AxisAngle,
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "axis:z"},
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "axis:z"},
             60.0, false, 30.0, 90.0});
        angled_assembly.calculate_placement_references();
        const auto angled_dependent = angled_assembly.resolve_axis(
            angled_component_it->placement_references.front().component_reference);
        const auto angled_prerequisite = angled_assembly.resolve_axis(
            angled_component_it->placement_references.front().target_reference);
        const double angle_alignment =
            angled_dependent.axis.direction.x * angled_prerequisite.axis.direction.x +
            angled_dependent.axis.direction.y * angled_prerequisite.axis.direction.y +
            angled_dependent.axis.direction.z * angled_prerequisite.axis.direction.z;
        require(std::abs(angle_alignment - 0.5) < 1.0e-7 &&
                    angled_assembly.remaining_degrees_of_freedom(second_id) == 5,
                "Axis angle placement reference did not reach its requested angle");
        const auto angled_placement = angled_assembly.components.back().placement;
        angled_assembly.calculate_placement_references();
        require(angled_assembly.components.back().placement.rotation_x ==
                    angled_placement.rotation_x &&
                    angled_assembly.components.back().placement.rotation_y ==
                    angled_placement.rotation_y &&
                    angled_assembly.components.back().placement.rotation_z ==
                    angled_placement.rotation_z,
                "Axis angle placement reference was not idempotent");
        const auto angled_scene = angled_assembly.build_scene();
        require(angled_scene.dimensions.size() == 1 &&
                    angled_scene.dimensions.front().reference.owner_id ==
                        angled_assembly.document_id &&
                    angled_scene.dimensions.front().reference.semantic_key ==
                        "placement-reference:" + second_id + ":0" &&
                    angled_scene.dimensions.front().unit_suffix == " °" &&
                    angled_scene.dimensions.front().value == 60.0,
                "Axis angle placement reference did not create its editable viewer dimension");
        const auto limited_mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-limited-mate-contract.asmz";
        angled_assembly.save(limited_mate_path);
        const auto loaded_limited_mate =
            zima::assembly::AssemblyDocument::load(limited_mate_path);
        std::filesystem::remove(limited_mate_path);
        const auto& loaded_limited_reference =
            loaded_limited_mate.components.back().placement_references.front();
        require(loaded_limited_reference.lower_limit == 30.0 &&
                    loaded_limited_reference.upper_limit == 90.0,
                "Placement reference absolute limits did not survive save/load");
        auto value_edit = loaded_limited_mate;
        auto& limited_value_row =
            value_edit.components.back().placement_references.front();
        limited_value_row.offset = 75.0;
        value_edit.calculate_placement_references();
        require(limited_value_row.offset == 75.0,
                "Placement reference value edit did not preserve the requested offset");
        const auto valid_value_placement = value_edit.components.back().placement;
        limited_value_row.offset = 100.0;
        value_edit.calculate_placement_references();
        require(limited_value_row.offset == 100.0 &&
                    value_edit.components.back().placement.rotation_x !=
                        valid_value_placement.rotation_x &&
                    value_edit.components.back().placement.rotation_y ==
                        valid_value_placement.rotation_y,
                "Placement reference edit outside persisted limits did not re-solve as stored");
        limited_value_row.offset = 75.0;
        value_edit.calculate_placement_references();
        require(value_edit.components.back().placement.rotation_x ==
                    valid_value_placement.rotation_x &&
                    value_edit.components.back().placement.rotation_y ==
                        valid_value_placement.rotation_y &&
                    value_edit.components.back().placement.rotation_z ==
                        valid_value_placement.rotation_z,
                "Restoring a valid placement-reference value did not restore the valid solve");
        require(std::abs(zima::assembly::AssemblyDocument::project_linear_drag_value(
                    {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
                    {5.0, 0.0, 7.0}, {-1.0, 0.0, 0.0}) - 7.0) < 1.0e-9 &&
                    std::abs(zima::assembly::AssemblyDocument::project_linear_drag_value(
                    {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
                    {0.0, 0.0, 7.0}, {0.0, 0.0, 1.0}) - 7.0) < 1.0e-9,
                "Assembly linear drag ray was not projected onto the mate axis");
        require(std::abs(
                    zima::assembly::AssemblyDocument::project_angular_drag_value(
                        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                        {0.0, 10.0, 10.0}, {0.0, 0.0, -1.0}) - 90.0) < 1.0e-9 &&
                    std::abs(
                        zima::assembly::AssemblyDocument::project_angular_drag_value(
                            {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                            {10.0, 0.0, 10.0}, {0.0, 0.0, -1.0})) < 1.0e-9,
                "Assembly angular drag ray did not produce a stable 0-180 degree value");
        auto plane_angled_assembly = loaded;
        auto plane_angle_component_it = std::find_if(
            plane_angled_assembly.components.begin(),
            plane_angled_assembly.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        require(plane_angle_component_it != plane_angled_assembly.components.end(),
                "Second occurrence missing before plane-angle test");
        plane_angle_component_it->placement_references.push_back(
            {zima::assembly::MateKind::PlaneAngle,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "z_min"},
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "z_max"},
             45.0, false});
        plane_angled_assembly.calculate_placement_references();
        const auto plane_angled_dependent = plane_angled_assembly.resolve_plane(
            plane_angle_component_it->placement_references.front().component_reference);
        const auto plane_angled_prerequisite = plane_angled_assembly.resolve_plane(
            plane_angle_component_it->placement_references.front().target_reference);
        const double plane_angle_alignment =
            plane_angled_dependent.plane.normal.x *
                plane_angled_prerequisite.plane.normal.x +
            plane_angled_dependent.plane.normal.y *
                plane_angled_prerequisite.plane.normal.y +
            plane_angled_dependent.plane.normal.z *
                plane_angled_prerequisite.plane.normal.z;
        require(std::abs(plane_angle_alignment - std::sqrt(0.5)) < 1.0e-7 &&
                    plane_angled_assembly.remaining_degrees_of_freedom(second_id) == 5,
                "Plane angle placement reference did not reach its requested angle");
        auto mated_assembly = loaded;
        auto mated_component_it = std::find_if(
            mated_assembly.components.begin(), mated_assembly.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        require(mated_component_it != mated_assembly.components.end(),
                "Second occurrence missing before plane-coincident test");
        mated_component_it->placement_references.push_back(
            {zima::assembly::MateKind::PlaneCoincident,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "z_min"},
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "z_max"},
             2.5, false});
        const auto dependent_plane = mated_assembly.resolve_plane(
            mated_component_it->placement_references.front().component_reference);
        const auto prerequisite_plane = mated_assembly.resolve_plane(
            mated_component_it->placement_references.front().target_reference);
        require(dependent_plane.status == zima::assembly::MateStatus::Valid &&
                    prerequisite_plane.status == zima::assembly::MateStatus::Valid &&
                    std::abs(dependent_plane.plane.point.z - 30.0) < 1.0e-7 &&
                    std::abs(prerequisite_plane.plane.point.z - 15.0) < 1.0e-7,
                "Persisted viewer packet did not resolve the selected planes");
        auto missing_reference =
            mated_component_it->placement_references.front().component_reference;
        missing_reference.semantic_key = "missing-face";
        require(mated_assembly.resolve_plane(missing_reference).status ==
                    zima::assembly::MateStatus::MissingReference,
                "Missing persisted face reference was not detected");
        mated_assembly.calculate_placement_references();
        const auto calculated_dependent = mated_assembly.resolve_plane(
            mated_component_it->placement_references.front().component_reference);
        const auto calculated_prerequisite = mated_assembly.resolve_plane(
            mated_component_it->placement_references.front().target_reference);
        const auto& calculated_normal = calculated_prerequisite.plane.normal;
        const double calculated_offset =
            (calculated_dependent.plane.point.x - calculated_prerequisite.plane.point.x) *
                calculated_normal.x +
            (calculated_dependent.plane.point.y - calculated_prerequisite.plane.point.y) *
                calculated_normal.y +
            (calculated_dependent.plane.point.z - calculated_prerequisite.plane.point.z) *
                calculated_normal.z;
        const auto calculated_placement = mated_assembly.components.back().placement;
        mated_assembly.calculate_placement_references();
        require(std::abs(calculated_offset - 2.5) < 1.0e-7 &&
                    mated_assembly.components.back().placement.x == calculated_placement.x &&
                    mated_assembly.components.back().placement.y == calculated_placement.y &&
                    mated_assembly.components.back().placement.z == calculated_placement.z,
                "Plane placement reference did not calculate its offset idempotently");
        const auto mated_scene = mated_assembly.build_scene();
        require(mated_scene.dimensions.size() == 1 &&
                    mated_scene.dimensions.front().reference.owner_id ==
                        mated_assembly.document_id &&
                    mated_scene.dimensions.front().reference.semantic_key ==
                        "placement-reference:" + second_id + ":0" &&
                    mated_scene.dimensions.front().unit_suffix == " mm" &&
                    mated_scene.dimensions.front().value == 2.5,
                "Plane placement reference did not create its editable viewer dimension");
        auto flipped_plane_assembly = loaded;
        auto flipped_component_it = std::find_if(
            flipped_plane_assembly.components.begin(),
            flipped_plane_assembly.components.end(),
            [&](const auto& component) { return component.occurrence_id == second_id; });
        require(flipped_component_it != flipped_plane_assembly.components.end(),
                "Second occurrence missing before flipped-plane test");
        flipped_component_it->placement_references.push_back(
            {zima::assembly::MateKind::PlaneCoincident,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "z_min"},
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "z_max"},
             0.0, true});
        flipped_plane_assembly.calculate_placement_references();
        const auto flipped_dependent = flipped_plane_assembly.resolve_plane(
            flipped_component_it->placement_references.front().component_reference);
        const auto flipped_prerequisite = flipped_plane_assembly.resolve_plane(
            flipped_component_it->placement_references.front().target_reference);
        const double flipped_alignment =
            flipped_dependent.plane.normal.x * flipped_prerequisite.plane.normal.x +
            flipped_dependent.plane.normal.y * flipped_prerequisite.plane.normal.y +
            flipped_dependent.plane.normal.z * flipped_prerequisite.plane.normal.z;
        require(std::abs(flipped_alignment + 1.0) < 1.0e-7,
                "Flipped plane placement reference did not preserve opposite face orientation");
        const auto flipped_mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-flipped-mate-contract.asmz";
        flipped_plane_assembly.save(flipped_mate_path);
        const auto loaded_flipped_mates =
            zima::assembly::AssemblyDocument::load(flipped_mate_path);
        std::filesystem::remove(flipped_mate_path);
        require(loaded_flipped_mates.components.back().placement_references.front().flip,
                "Placement reference Flip did not survive save/load");
        auto broken_mate_assembly = mated_assembly;
        broken_mate_assembly.components.back().placement_references.front()
            .component_reference.semantic_key = "missing-face";
        broken_mate_assembly.calculate_placement_references();
        require(broken_mate_assembly.effectively_suppressed_occurrences().empty(),
                "Broken placement reference unexpectedly suppressed its dependent component");
        broken_mate_assembly.components.back().placement_references.front()
            .component_reference.semantic_key = "z_min";
        broken_mate_assembly.calculate_placement_references();
        const auto mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-mate-contract.asmz";
        mated_assembly.save(mate_path);
        const auto loaded_mates = zima::assembly::AssemblyDocument::load(mate_path);
        std::filesystem::remove(mate_path);
        require(loaded_mates.components.back().placement_references ==
                    mated_assembly.components.back().placement_references,
                "Placement reference did not survive save/load");
        auto lifecycle = loaded_mates;
        lifecycle.components.front().suppressed = true;
        lifecycle.calculate_placement_references();
        const auto suppressed_lifecycle =
            lifecycle.effectively_suppressed_occurrences();
        require(suppressed_lifecycle.contains(first_id) &&
                    !suppressed_lifecycle.contains(second_id),
                "Suppressed prerequisite component unexpectedly propagated through placement references");
        lifecycle.components.front().suppressed = false;
        lifecycle.components.back().placement_references.front().offset = 7.0;
        lifecycle.calculate_placement_references();
        require(lifecycle.components.back().placement_references.size() == 1 &&
                    lifecycle.components.back().placement_references.front().offset ==
                        7.0,
                "Editing a placement reference lost its row identity or calculation");
        lifecycle.components.back().placement_references.clear();
        require(lifecycle.components.back().placement_references.empty(),
                "Removing a placement reference left its row behind");
        zima::assembly::AssemblySession mate_session(mated_assembly);
        auto suppressed_revision = mate_session.document();
        suppressed_revision.components.back().placement_references.clear();
        suppressed_revision.calculate_placement_references();
        mate_session.commit(std::move(suppressed_revision));
        require(mate_session.revision() == 1 &&
                    mate_session.document().components.back().placement_references.empty() &&
                    mate_session.undo() &&
                    !mate_session.document().components.back().placement_references.empty() &&
                    mate_session.redo() &&
                    mate_session.document().components.back().placement_references.empty(),
                "Placement-reference row removal did not behave as one Undo/Redo revision");
        auto removed_revision = mate_session.document();
        removed_revision.components.back().placement_references.clear();
        mate_session.commit(std::move(removed_revision));
        require(mate_session.document().components.back().placement_references.empty() &&
                    mate_session.undo(),
                "Placement-reference removal did not restore its revision through Undo");
        auto rotated_axis_mate = loaded;
        rotated_axis_mate.components.back().placement.x = 25.0;
        rotated_axis_mate.components.back().placement.rotation_x = 90.0;
        rotated_axis_mate.components.back().placement_references.push_back(
            {zima::assembly::MateKind::AxisCoincident,
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "axis:z"},
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "axis:z"},
             0.0, false});
        rotated_axis_mate.calculate_placement_references();
        const auto rotated_axis_dependent = rotated_axis_mate.resolve_axis(
            rotated_axis_mate.components.back().placement_references.front()
                .component_reference);
        const auto rotated_axis_prerequisite = rotated_axis_mate.resolve_axis(
            rotated_axis_mate.components.back().placement_references.front()
                .target_reference);
        const double rotated_axis_alignment =
            rotated_axis_dependent.axis.direction.x *
                rotated_axis_prerequisite.axis.direction.x +
            rotated_axis_dependent.axis.direction.y *
                rotated_axis_prerequisite.axis.direction.y +
            rotated_axis_dependent.axis.direction.z *
                rotated_axis_prerequisite.axis.direction.z;
        const auto rotated_axis_placement = rotated_axis_mate.components.back().placement;
        rotated_axis_mate.calculate_placement_references();
        require(std::abs(std::abs(rotated_axis_alignment) - 1.0) < 1.0e-7 &&
                    std::abs(rotated_axis_mate.components.back().placement.rotation_x -
                             rotated_axis_placement.rotation_x) < 1.0e-7 &&
                    std::abs(rotated_axis_mate.components.back().placement.rotation_y -
                             rotated_axis_placement.rotation_y) < 1.0e-7 &&
                    std::abs(rotated_axis_mate.components.back().placement.rotation_z -
                             rotated_axis_placement.rotation_z) < 1.0e-7,
                "Axis placement reference did not rotate a perpendicular axis idempotently");
        const auto rotated_mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-rotated-mate-contract.asmz";
        rotated_axis_mate.save(rotated_mate_path);
        const auto loaded_rotated_mate =
            zima::assembly::AssemblyDocument::load(rotated_mate_path);
        std::filesystem::remove(rotated_mate_path);
        require(std::abs(loaded_rotated_mate.components.back().placement.rotation_x -
                         rotated_axis_placement.rotation_x) < 1.0e-7 &&
                    std::abs(loaded_rotated_mate.components.back().placement.rotation_y -
                             rotated_axis_placement.rotation_y) < 1.0e-7 &&
                    std::abs(loaded_rotated_mate.components.back().placement.rotation_z -
                             rotated_axis_placement.rotation_z) < 1.0e-7 &&
                    loaded_rotated_mate.components.back().placement_references.size() == 1,
                "Calculated rotational placement reference did not survive save/load");
        auto rotated_plane_mate = loaded;
        rotated_plane_mate.components.back().placement.rotation_y = 90.0;
        rotated_plane_mate.components.back().placement_references.push_back(
            {zima::assembly::MateKind::PlaneCoincident,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "z_min"},
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "z_max"},
             4.0, false});
        rotated_plane_mate.calculate_placement_references();
        const auto rotated_plane_dependent = rotated_plane_mate.resolve_plane(
            rotated_plane_mate.components.back().placement_references.front()
                .component_reference);
        const auto rotated_plane_prerequisite = rotated_plane_mate.resolve_plane(
            rotated_plane_mate.components.back().placement_references.front()
                .target_reference);
        const double rotated_plane_alignment =
            rotated_plane_dependent.plane.normal.x *
                rotated_plane_prerequisite.plane.normal.x +
            rotated_plane_dependent.plane.normal.y *
                rotated_plane_prerequisite.plane.normal.y +
            rotated_plane_dependent.plane.normal.z *
                rotated_plane_prerequisite.plane.normal.z;
        const double rotated_plane_offset =
            (rotated_plane_dependent.plane.point.x -
             rotated_plane_prerequisite.plane.point.x) *
                rotated_plane_prerequisite.plane.normal.x +
            (rotated_plane_dependent.plane.point.y -
             rotated_plane_prerequisite.plane.point.y) *
                rotated_plane_prerequisite.plane.normal.y +
            (rotated_plane_dependent.plane.point.z -
             rotated_plane_prerequisite.plane.point.z) *
                rotated_plane_prerequisite.plane.normal.z;
        require(std::abs(std::abs(rotated_plane_alignment) - 1.0) < 1.0e-7 &&
                    std::abs(rotated_plane_offset - 4.0) < 1.0e-7,
                "Plane placement reference did not rotate and offset a perpendicular plane");
        auto combined_mates = loaded;
        combined_mates.components.back().placement.x = 20.0;
        combined_mates.components.back().placement_references.push_back(
            {zima::assembly::MateKind::AxisCoincident,
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "axis:z"},
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "axis:z"},
             0.0, false});
        combined_mates.components.back().placement_references.push_back(
            {zima::assembly::MateKind::PlaneCoincident,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "z_min"},
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "z_max"},
             0.0, false});
        combined_mates.calculate_placement_references();
        const auto combined_axis_dependent = combined_mates.resolve_axis(
            combined_mates.components.back().placement_references.front()
                .component_reference);
        const auto combined_axis_prerequisite = combined_mates.resolve_axis(
            combined_mates.components.back().placement_references.front()
                .target_reference);
        require(combined_mates.components.back().placement_references.size() == 2 &&
                    std::abs(combined_axis_dependent.axis.point.x -
                             combined_axis_prerequisite.axis.point.x) < 1.0e-7 &&
                    std::abs(combined_axis_dependent.axis.point.y -
                             combined_axis_prerequisite.axis.point.y) < 1.0e-7,
                "Axis and plane mates did not preserve their independent constraints");
        auto two_plane_mates = loaded;
        two_plane_mates.components.back().placement = {23.0, 9.0, 14.0, 0.0, 0.0, 0.0};
        two_plane_mates.components.back().placement_references.push_back(
            {zima::assembly::MateKind::PlaneCoincident,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "z_min"},
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "z_max"},
             0.0, false});
        two_plane_mates.components.back().placement_references.push_back(
            {zima::assembly::MateKind::PlaneCoincident,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "x_min"},
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "x_max"},
             0.0, false});
        two_plane_mates.calculate_placement_references();
        require(two_plane_mates.components.back().placement_references.size() == 2,
                "Two independent plane placement references on one component were not preserved");
        auto two_axis_mates = loaded;
        two_axis_mates.components.back().placement = {23.0, 9.0, 14.0, 0.0, 0.0, 0.0};
        for (const auto* axis : {"axis:z", "axis:x"}) {
            two_axis_mates.components.back().placement_references.push_back(
                {zima::assembly::MateKind::AxisCoincident,
                 {zima::assembly::MateReferenceKind::Axis,
                  zima::assembly::InstancePath{}.child(second_id),
                  "same-source-container", axis},
                 {zima::assembly::MateReferenceKind::Axis,
                  zima::assembly::InstancePath{}.child(first_id),
                  "same-source-container", axis},
                 0.0, false});
        }
        two_axis_mates.calculate_placement_references();
        require(two_axis_mates.components.back().placement_references.size() == 2,
                "Two independent axis placement references on one component were not preserved");
        auto conflicting_mates = loaded;
        conflicting_mates.components.back().placement.x = 17.0;
        conflicting_mates.components.back().placement_references.push_back(
            {zima::assembly::MateKind::AxisCoincident,
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "axis:z"},
             {zima::assembly::MateReferenceKind::Axis,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "axis:z"},
             0.0, false});
        conflicting_mates.components.back().placement_references.push_back(
            {zima::assembly::MateKind::PlaneCoincident,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(second_id),
              "same-source-container", "z_min"},
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{}.child(first_id),
              "same-source-container", "x_max"},
             0.0, false});
        conflicting_mates.calculate_placement_references();
        require(conflicting_mates.components.back().placement_references.size() == 2,
                "Conflicting placement references were not retained on the component");
        auto state_document = loaded;
        state_document.components.front().suppressed = true;
        state_document.components.front().grounded = true;
        state_document.components.front().body_color = "#3F7652";
        state_document.components.front().body_color_override = "#806B5A8E";
        state_document.components.back().visible = false;
        require(state_document.build_scene().triangles.empty() &&
                    state_document.components.size() == 2 &&
                    !state_document.components.front().calculated_source.mesh.vertices.empty(),
                "Suppression/hide deleted component data or left it displayed");
        const auto state_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-assembly-state-contract.asmz";
        state_document.save(state_path);
        const auto loaded_state = zima::assembly::AssemblyDocument::load(state_path);
        std::filesystem::remove(state_path);
        require(loaded_state.components.front().suppressed &&
                    loaded_state.components.front().grounded &&
                    loaded_state.components.front().body_color == "#3F7652" &&
                    loaded_state.components.front().body_color_override ==
                        std::optional<std::string>{"#806B5A8E"} &&
                    loaded_state.remaining_degrees_of_freedom(first_id) == 0 &&
                    !loaded_state.components.back().visible,
                "Assembly suppression, visibility or grounding were not persisted separately");
        auto dependent_assembly = loaded;
        auto third = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Třetí", "third-part-document", "third.prtz", source.back());
        third.placement.z = 60.0;
        const std::string third_id = third.occurrence_id;
        dependent_assembly.components.push_back(std::move(third));
        dependent_assembly.add_dependency(
            zima::assembly::AssemblyDocument::create_dependency(
                second_id, first_id,
                zima::assembly::ComponentDependencyKind::PlacementReference));
        dependent_assembly.add_dependency(
            zima::assembly::AssemblyDocument::create_dependency(
                third_id, second_id,
                zima::assembly::ComponentDependencyKind::ExternalSketchReference));
        dependent_assembly.components.front().suppressed = true;
        const auto all_suppressed =
            dependent_assembly.effectively_suppressed_occurrences();
        require(all_suppressed.size() == 3 &&
                    all_suppressed.contains(first_id) &&
                    all_suppressed.contains(second_id) &&
                    all_suppressed.contains(third_id) &&
                    dependent_assembly.build_scene().triangles.empty(),
                "Explicit component suppression did not cascade transitively");
        dependent_assembly.components.front().suppressed = false;
        dependent_assembly.components.front().visible = false;
        require(dependent_assembly.effectively_suppressed_occurrences().empty() &&
                    !dependent_assembly.build_scene().triangles.empty(),
                "Display visibility incorrectly cascaded as model suppression");
        dependent_assembly.components.front().visible = true;
        dependent_assembly.components[1].suppressed = true;
        const auto branch_suppressed =
            dependent_assembly.effectively_suppressed_occurrences();
        require(branch_suppressed.size() == 2 &&
                    !branch_suppressed.contains(first_id) &&
                    branch_suppressed.contains(second_id) &&
                    branch_suppressed.contains(third_id),
                "Component suppression propagated against dependency direction");
        const auto dependency_snapshot = dependent_assembly.occurrence_snapshot();
        require(dependency_snapshot.size() == 3 &&
                    !dependency_snapshot[0].dependency_suppressed &&
                    dependency_snapshot[1].manually_suppressed &&
                    dependency_snapshot[2].dependency_suppressed,
                "Occurrence snapshot lost manual/dependency suppression distinction");
        bool component_cycle_rejected = false;
        try {
            dependent_assembly.add_dependency(
                zima::assembly::AssemblyDocument::create_dependency(
                    first_id, third_id,
                    zima::assembly::ComponentDependencyKind::PlacementReference));
        } catch (const std::invalid_argument&) {
            component_cycle_rejected = true;
        }
        require(component_cycle_rejected,
                "Component dependency graph accepted an indirect cycle");
        const auto dependency_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-component-dependency-contract.asmz";
        dependent_assembly.save(dependency_path);
        const auto loaded_dependencies =
            zima::assembly::AssemblyDocument::load(dependency_path);
        std::filesystem::remove(dependency_path);
        require(loaded_dependencies.dependencies.size() == 2 &&
                    loaded_dependencies.dependencies[0].kind ==
                        zima::assembly::ComponentDependencyKind::PlacementReference &&
                    loaded_dependencies.dependencies[1].kind ==
                        zima::assembly::ComponentDependencyKind::ExternalSketchReference &&
                    loaded_dependencies.effectively_suppressed_occurrences() ==
                        branch_suppressed,
                "Component dependency identity or suppression changed on save/load");
        zima::assembly::DependencyGraph dependencies;
        dependencies.add_dependency("top", "sub-a");
        dependencies.add_dependency("sub-a", "sub-b");
        require(dependencies.would_create_cycle("sub-b", "top") &&
                    dependencies.would_create_cycle("top", "top") &&
                    !dependencies.would_create_cycle("top", "part"),
                "Dependency graph did not detect direct/indirect cycle conditions");
        bool cycle_rejected = false;
        try {
            dependencies.add_dependency("sub-b", "top");
        } catch (const std::invalid_argument&) {
            cycle_rejected = true;
        }
        require(cycle_rejected,
                "Dependency graph accepted an indirect Assembly cycle");
        auto datum_assembly = assembly;
        auto datum_plane = zima::assembly::AssemblyDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        datum_plane.name = "Montážní rovina";
        datum_plane.origin = {0.0, 0.0, 50.0};
        datum_plane.direction = {0.0, 0.0, 1.0};
        datum_plane.display_size = 80.0;
        const auto datum_id = datum_plane.id;
        const auto datum_plane_entity_id = datum_plane.entity_id;
        auto datum_point = zima::assembly::AssemblyDocument::create_construction(
            zima::document::ConstructionKind::Point);
        auto datum_axis = zima::assembly::AssemblyDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        const auto datum_point_id = datum_point.id;
        const auto datum_axis_id = datum_axis.id;
        const auto datum_axis_entity_id = datum_axis.entity_id;
        datum_assembly.constructions = {datum_point, datum_axis, datum_plane};
        const auto datum_scene = datum_assembly.build_scene();
        require(datum_assembly.find_construction(datum_id) != nullptr &&
                    std::any_of(datum_scene.original_references.triangle_references.begin(),
                        datum_scene.original_references.triangle_references.end(),
                        [&](const auto& reference) {
                            return reference.instance_path.empty() &&
                                reference.owner_id == datum_plane_entity_id &&
                                reference.semantic_key == "plane";
                        }) &&
                    std::any_of(datum_scene.original_references.points.begin(),
                        datum_scene.original_references.points.end(),
                        [&](const auto& point) {
                            return point.reference.instance_path.empty() &&
                                point.reference.owner_id ==
                                    datum_point_id + ":origin";
                        }) &&
                    std::any_of(datum_scene.original_references.axes.begin(),
                        datum_scene.original_references.axes.end(),
                        [&](const auto& axis) {
                            return axis.reference.instance_path.empty() &&
                                axis.reference.owner_id == datum_axis_entity_id;
                        }),
                "Assembly-owned point, axis or plane lost viewer identity");
        const auto dependent_face = *std::find_if(
            datum_scene.original_references.triangle_references.begin(),
            datum_scene.original_references.triangle_references.end(),
            [&](const auto& reference) {
                return reference.instance_path ==
                    zima::assembly::InstancePath{}.child(first_id).encoded() &&
                    (reference.semantic_key == "z_min" ||
                        reference.semantic_key == "z_max");
            });
        auto associative_assembly = assembly;
        auto referenced_plane =
            zima::assembly::AssemblyDocument::create_construction(
                zima::document::ConstructionKind::Plane);
        referenced_plane.definition =
            zima::document::ConstructionDefinition::PlaneReference;
        referenced_plane.references = {{
            zima::assembly::InstancePath{{first_id}}.encoded(),
            dependent_face.owner_id, dependent_face.semantic_key}};
        // The per-reference offset (not the vestigial object-level
        // ConstructionObject::offset, which the new generic
        // resolve_construction() no longer reads -- see
        // placement_solve_position()'s `+ reference.offset`) shifts the
        // resolved origin along the picked face's normal.
        referenced_plane.references.front().offset = 7.0;
        associative_assembly.constructions.push_back(referenced_plane);
        auto second_referenced_plane = referenced_plane;
        second_referenced_plane.id += "-second-occurrence";
        second_referenced_plane.entity_id =
            second_referenced_plane.id + ":entity";
        second_referenced_plane.entity_parent_id = second_referenced_plane.id;
        second_referenced_plane.container_origin =
            zima::document::create_container_origin(second_referenced_plane.id);
        second_referenced_plane.references.front().instance_path =
            zima::assembly::InstancePath{{second_id}}.encoded();
        associative_assembly.constructions.push_back(second_referenced_plane);
        associative_assembly.resolve_constructions();
        // A single Face/plane pick only constrains the origin along that
        // face's own normal (see resolve_construction()'s generic
        // placement_solve_position() -- an intentional design matching
        // Python's _solve_point_constraints(), leaving the two in-plane
        // axes at their prior/default value); which world axis that is
        // depends on which face of the box was matched first, so compare
        // the whole resolved origin rather than assuming Z specifically.
        require(associative_assembly.constructions.front().reference_valid &&
                    associative_assembly.constructions.back().reference_valid &&
                    !(associative_assembly.constructions.front().origin ==
                        associative_assembly.constructions.back().origin) &&
                    associative_assembly.construction_viewer_mesh()
                        .original_references.triangle_references.size() == 4,
                "Assembly datum plane did not distinguish repeated occurrence paths");
        const auto associative_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-associative-assembly-datum.asmz";
        associative_assembly.save(associative_path);
        auto loaded_associative =
            zima::assembly::AssemblyDocument::load(associative_path);
        std::filesystem::remove(associative_path);
        loaded_associative.resolve_constructions();
        require(loaded_associative.constructions.size() == 2 &&
                    loaded_associative.constructions.front().references.front()
                        .instance_path ==
                        zima::assembly::InstancePath{{first_id}}.encoded() &&
                    loaded_associative.constructions.back().reference_valid,
                "Associative Assembly datum references changed on save/load");
        datum_assembly.components.front().placement_references.push_back(
            {zima::assembly::MateKind::PlaneCoincident,
             {zima::assembly::MateReferenceKind::Face,
              zima::assembly::InstancePath{{first_id}}, dependent_face.owner_id,
              dependent_face.semantic_key},
             {zima::assembly::MateReferenceKind::Face, {},
              datum_plane_entity_id, "plane"},
             0.0, false});
        datum_assembly.calculate_placement_references();
        require(datum_assembly.dependencies.empty(),
                "Component datum placement reference unexpectedly created a dependency edge");
        const auto datum_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-assembly-datum-contract.asmz";
        datum_assembly.save(datum_path);
        const auto loaded_datum =
            zima::assembly::AssemblyDocument::load(datum_path);
        std::filesystem::remove(datum_path);
        require(loaded_datum.constructions.size() == 3 &&
                    loaded_datum.constructions.back().id == datum_id &&
                    loaded_datum.constructions.back().name == "Montážní rovina" &&
                    loaded_datum.components.front().placement_references.back()
                        .target_reference.instance_path.occurrence_ids.empty(),
                "Assembly datum identity or placement-reference target changed on save/load");
        zima::assembly::AssemblySession session(loaded);
        auto placed = session.document();
        placed.components.front().placement.x = 42.0;
        session.commit(std::move(placed));
        require(session.revision() == 1 && session.is_dirty() && session.can_undo(),
                "Assembly placement did not create one dirty revision");
        session.mark_saved();
        require(!session.is_dirty(), "Assembly savepoint did not become clean");
        auto refreshed = session.document();
        refreshed.components.front().calculated_source.volume += 1.0;
        session.update_dependency_snapshots(std::move(refreshed));
        require(session.revision() == 1 && session.is_dirty(),
                "Dependency refresh created a model revision or stayed clean");
        require(session.undo() && session.document().components.front().placement.x == 0.0,
                "Assembly Undo did not restore component-owned placement");
        require(session.redo() && session.document().components.front().placement.x == 42.0,
                "Assembly Redo did not restore component-owned placement");
        zima::assembly::AssemblySession datum_session(assembly);
        auto with_axis = datum_session.document();
        with_axis.constructions.push_back(
            zima::assembly::AssemblyDocument::create_construction(
                zima::document::ConstructionKind::Axis));
        datum_session.commit(std::move(with_axis));
        require(datum_session.document().constructions.size() == 1 &&
                    datum_session.undo() &&
                    datum_session.document().constructions.empty() &&
                    datum_session.redo() &&
                    datum_session.document().constructions.size() == 1,
                "Assembly datum creation did not participate in Undo/Redo");
        std::cout << "C++ assembly occurrence contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include <zima/assembly/assembly_document.hpp>
#include <zima/assembly/assembly_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/viewer/picking.hpp>

#include <iostream>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        zima::kernel::OcctKernel kernel;
        const auto source = kernel.evaluate_history({
            {"same-source-container", zima::kernel::BoxRequest{10.0, 10.0, 10.0},
             zima::kernel::BooleanOperation::Add},
        });
        auto assembly = zima::assembly::AssemblyDocument::create_default();
        auto first = zima::assembly::AssemblyDocument::create_part_occurrence(
            "První", "same-part-document", "same.zcp.json", source.back());
        auto second = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Druhý", "same-part-document", "same.zcp.json", source.back());
        first.placement.z = 5.0;
        second.placement.z = 30.0;
        const std::string first_id = first.occurrence_id;
        const std::string second_id = second.occurrence_id;
        assembly.components.push_back(std::move(first));
        assembly.components.push_back(std::move(second));
        const auto scene = assembly.build_scene();
        std::set<std::string> instance_paths;
        for (const auto& reference : scene.triangle_references) {
            require(reference.owner_id == "same-source-container" &&
                        !reference.instance_path.empty(),
                    "Assembly changed source ownership or lost occurrence identity");
            instance_paths.insert(reference.instance_path);
        }
        require(instance_paths.size() == 2,
                "Repeated source Part occurrences collapsed into one identity");
        const auto rollback_scene = assembly.build_scene_with_part_override(
            first_id, zima::kernel::BodyResult{});
        std::set<std::string> rollback_paths;
        for (const auto& reference : rollback_scene.triangle_references) {
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
                    candidates[0].owner_id == candidates[1].owner_id,
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
            "zima-cad-cpp-assembly-contract.zca.json";
        assembly.save(assembly_path);
        const auto loaded = zima::assembly::AssemblyDocument::load(assembly_path);
        std::filesystem::remove(assembly_path);
        require(loaded.document_id == assembly.document_id &&
                    loaded.components.size() == 2 &&
                    loaded.components.front().occurrence_id == first_id &&
                    loaded.components.back().occurrence_id == second_id &&
                    loaded.components.back().source_document_id == "same-part-document" &&
                    loaded.components.back().source_kind ==
                        zima::assembly::ComponentSourceKind::Part &&
                    loaded.components.back().placement.z == 30.0,
                "Assembly identity and placement did not survive save/load");
        const auto loaded_scene = loaded.build_scene();
        std::set<std::string> loaded_paths;
        for (const auto& reference : loaded_scene.triangle_references) {
            loaded_paths.insert(reference.instance_path);
        }
        require(loaded_paths == instance_paths,
                "Assembly save/load changed stable occurrence paths");
        auto state_document = loaded;
        state_document.components.front().suppressed = true;
        state_document.components.back().visible = false;
        require(state_document.build_scene().triangles.empty() &&
                    state_document.components.size() == 2 &&
                    !state_document.components.front().calculated_source.mesh.vertices.empty(),
                "Suppression/hide deleted component data or left it displayed");
        const auto state_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-assembly-state-contract.zca.json";
        state_document.save(state_path);
        const auto loaded_state = zima::assembly::AssemblyDocument::load(state_path);
        std::filesystem::remove(state_path);
        require(loaded_state.components.front().suppressed &&
                    !loaded_state.components.back().visible,
                "Assembly suppression and visibility were not persisted separately");
        auto dependent_assembly = loaded;
        auto third = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Třetí", "third-part-document", "third.zcp.json", source.back());
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
            "zima-cad-cpp-component-dependency-contract.zca.json";
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
        std::cout << "C++ assembly occurrence contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

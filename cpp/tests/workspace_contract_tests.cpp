#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <iostream>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        zima::workspace::Workspace workspace;
        zima::kernel::OcctKernel kernel;
        auto part = zima::document::PartDocument::create_default();
        part.history.push_back(zima::document::PartDocument::create_box_container());
        auto part_calculation = kernel.evaluate_history(part.kernel_operations());
        auto assembly = zima::assembly::AssemblyDocument::create_default();
        const std::string part_id = part.document_id;
        const std::string assembly_id = assembly.document_id;
        workspace.add_part(std::move(part), part_calculation, "open-part.zcp.json");
        workspace.add_assembly(std::move(assembly));
        require(workspace.size() == 2 && workspace.open_part(part_id) != nullptr &&
                    workspace.open_assembly(assembly_id) != nullptr,
                "Workspace did not retain typed open documents");
        workspace.display_top_level(assembly_id);
        workspace.activate(part_id);
        require(workspace.active_document_id() == part_id &&
                    workspace.displayed_document_id() == assembly_id,
                "Part activation replaced the displayed top-level Assembly");
        workspace.activate(assembly_id);
        require(workspace.displayed_document_id() == assembly_id,
                "Assembly activation unexpectedly changed display ownership");
        const auto occurrence_id = workspace.insert_open_part(
            assembly_id, part_id, "Vložený díl");
        const auto* inserted = workspace.open_assembly(assembly_id)
            ->session.document().find_occurrence(occurrence_id);
        require(inserted != nullptr && inserted->source_document_id == part_id,
                "Workspace did not insert the authoritative open Part");
        const double old_volume = inserted->calculated_source.volume;
        auto changed_part = workspace.open_part(part_id)->session.document();
        changed_part.history.front().box.length *= 2.0;
        auto changed_calculation = kernel.evaluate_history(changed_part.kernel_operations());
        workspace.open_part(part_id)->session.commit(
            std::move(changed_part), changed_calculation);
        require(workspace.open_assembly(assembly_id)
                    ->session.document().find_occurrence(occurrence_id)
                    ->calculated_source.volume == old_volume,
                "Part edit implicitly regenerated its parent Assembly");
        workspace.regenerate_assembly_from_open_dependencies(assembly_id);
        require(workspace.open_assembly(assembly_id)
                    ->session.document().find_occurrence(occurrence_id)
                    ->calculated_source.volume == changed_calculation.back().volume,
                "Explicit Assembly Regenerate ignored authoritative in-memory Part");
        bool duplicate_rejected = false;
        try {
            workspace.add_part(
                workspace.open_part(part_id)->session.document());
        } catch (const std::invalid_argument&) {
            duplicate_rejected = true;
        }
        require(duplicate_rejected,
                "Workspace accepted duplicate document identity");

        auto subassembly = zima::assembly::AssemblyDocument::create_default();
        subassembly.name = "Podsestava";
        const std::string subassembly_id = subassembly.document_id;
        workspace.add_assembly(std::move(subassembly), "subassembly.zca.json");
        const std::string nested_part_occurrence = workspace.insert_open_part(
            subassembly_id, part_id, "Vnitřní díl");
        auto topassembly = zima::assembly::AssemblyDocument::create_default();
        topassembly.name = "Horní sestava";
        const std::string topassembly_id = topassembly.document_id;
        workspace.add_assembly(std::move(topassembly), "topassembly.zca.json");
        const std::string subassembly_occurrence = workspace.insert_open_assembly(
            topassembly_id, subassembly_id, "Vložená podsestava");
        const std::string direct_part_occurrence = workspace.insert_open_part(
            topassembly_id, part_id, "Přímý kontextový díl");
        const auto* inserted_subassembly = workspace.open_assembly(topassembly_id)
            ->session.document().find_occurrence(subassembly_occurrence);
        require(inserted_subassembly != nullptr &&
                    inserted_subassembly->nested_snapshot.size() == 1 &&
                    inserted_subassembly->nested_snapshot.front().name ==
                        "Vnitřní díl",
                "Assembly insertion did not capture its structural snapshot");
        const auto nested_scene = workspace.open_assembly(topassembly_id)
            ->session.document().build_scene();
        const std::string expected_nested_path =
            zima::assembly::InstancePath{}.child(subassembly_occurrence)
                .child(nested_part_occurrence).encoded();
        require(!nested_scene.triangle_references.empty() &&
                    nested_scene.triangle_references.front().instance_path ==
                        expected_nested_path,
                "Nested Assembly flattened or lost its stable leaf instance path");
        const auto resolved_nested = workspace.resolve_occurrence(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path));
        require(resolved_nested.has_value() &&
                    resolved_nested->owner_assembly_document_id == subassembly_id &&
                    resolved_nested->occurrence_id == nested_part_occurrence &&
                    resolved_nested->source_document_id == part_id,
                "Workspace did not resolve exact nested occurrence ownership");
        const auto rollback_scene = workspace.build_scene_with_part_override(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path), {});
        const std::string direct_part_path = zima::assembly::InstancePath{}
            .child(direct_part_occurrence).encoded();
        require(!rollback_scene.triangle_references.empty() &&
                    std::all_of(rollback_scene.triangle_references.begin(),
                        rollback_scene.triangle_references.end(),
                        [&](const auto& reference) {
                            return reference.instance_path == direct_part_path;
                        }),
                "Nested rollback changed passive sibling context or kept target body");
        const auto maximum_y = [](const zima::kernel::ViewerMesh& scene) {
            return std::max_element(scene.vertices.begin(), scene.vertices.end(),
                [](const auto& left, const auto& right) {
                    return left.y < right.y;
                })->y;
        };
        const double old_nested_maximum_y = maximum_y(nested_scene);
        auto changed_again = workspace.open_part(part_id)->session.document();
        changed_again.history.front().box.width *= 2.0;
        auto changed_again_calculation =
            kernel.evaluate_history(changed_again.kernel_operations());
        workspace.open_part(part_id)->session.commit(
            std::move(changed_again), changed_again_calculation);
        require(maximum_y(workspace.open_assembly(topassembly_id)
                    ->session.document().build_scene()) == old_nested_maximum_y,
                "Nested Part edit implicitly regenerated a top-level Assembly");
        auto renamed_subassembly = workspace.open_assembly(subassembly_id)
            ->session.document();
        renamed_subassembly.components.front().name = "Přejmenovaný vnitřní díl";
        workspace.open_assembly(subassembly_id)->session.commit(
            std::move(renamed_subassembly));
        require(workspace.open_assembly(topassembly_id)->session.document()
                    .find_occurrence(subassembly_occurrence)
                    ->nested_snapshot.front().name == "Vnitřní díl",
                "Source Assembly edit leaked into parent structural snapshot");
        workspace.regenerate_assembly_from_open_dependencies(topassembly_id);
        const auto regenerated_nested_scene = workspace.open_assembly(topassembly_id)
            ->session.document().build_scene();
        require(maximum_y(regenerated_nested_scene) > old_nested_maximum_y &&
                    regenerated_nested_scene.triangle_references.front().instance_path ==
                        expected_nested_path,
                "Top-level Regenerate did not pull nested geometry or preserve identity");
        require(workspace.open_assembly(topassembly_id)->session.document()
                    .find_occurrence(subassembly_occurrence)
                    ->nested_snapshot.front().name == "Přejmenovaný vnitřní díl",
                "Top-level Regenerate did not refresh nested structural snapshot");
        const auto nested_save_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-nested-snapshot-contract.zca.json";
        workspace.open_assembly(topassembly_id)->session.document().save(
            nested_save_path);
        const auto loaded_nested = zima::assembly::AssemblyDocument::load(
            nested_save_path);
        std::filesystem::remove(nested_save_path);
        require(loaded_nested.find_occurrence(subassembly_occurrence)
                    ->nested_snapshot.front().name ==
                        "Přejmenovaný vnitřní díl",
                "Nested structural snapshot did not survive save/load");
        bool assembly_cycle_rejected = false;
        try {
            static_cast<void>(workspace.insert_open_assembly(
                subassembly_id, topassembly_id, "Zakázaný cyklus"));
        } catch (const std::invalid_argument&) {
            assembly_cycle_rejected = true;
        }
        require(assembly_cycle_rejected,
                "Workspace accepted an indirect nested Assembly cycle");
        std::cout << "C++ Workspace contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

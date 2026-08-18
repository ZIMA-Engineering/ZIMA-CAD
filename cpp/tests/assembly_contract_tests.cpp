#include <zima/assembly/assembly_document.hpp>
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
        const auto nested = zima::assembly::InstancePath{}
            .child("a:b").child("c").encoded();
        require(nested == "3:a:b1:c",
                "Length-prefixed instance path encoding is ambiguous");
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
                    loaded.components.back().placement.z == 30.0,
                "Assembly identity and placement did not survive save/load");
        const auto loaded_scene = loaded.build_scene();
        std::set<std::string> loaded_paths;
        for (const auto& reference : loaded_scene.triangle_references) {
            loaded_paths.insert(reference.instance_path);
        }
        require(loaded_paths == instance_paths,
                "Assembly save/load changed stable occurrence paths");
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
        std::cout << "C++ assembly occurrence contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

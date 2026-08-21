#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/part_document.hpp>
#include <zima/viewer/picking.hpp>
#include <zima/workspace/workspace.hpp>

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

template <typename Function>
double milliseconds(Function&& function, int repetitions = 3) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repetitions));
    for (int index = 0; index < repetitions; ++index) {
        const auto start = Clock::now();
        function();
        const auto finish = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(
            finish - start).count());
    }
    double total = 0.0;
    for (const auto sample : samples) total += sample;
    return total / static_cast<double>(samples.size());
}

zima::document::PartDocument part_fixture(std::size_t feature_count) {
    auto part = zima::document::PartDocument::create_default();
    part.history.clear();
    part.history_order.clear();
    for (std::size_t index = 0; index < feature_count; ++index) {
        auto feature = zima::document::PartDocument::create_box_container();
        feature.id = "benchmark-box-" + std::to_string(index);
        feature.name = feature.id;
        feature.placement.x = static_cast<double>(index % 8) * 12.0;
        feature.placement.y = static_cast<double>(index / 8) * 12.0;
        feature.placement.z = 0.0;
        feature.box.length = 20.0;
        feature.box.width = 20.0;
        feature.box.height = 20.0;
        part.history.push_back(feature);
        part.insert_history_entry(zima::document::PartHistoryKind::Feature,
                                  feature.id);
    }
    return part;
}

}  // namespace

int main() {
    constexpr std::size_t part_features = 24;
    constexpr std::size_t assembly_components = 256;
    constexpr std::size_t nested_levels = 3;
    constexpr int repetitions = 3;

    zima::kernel::OcctKernel kernel;
    const auto part = part_fixture(part_features);
    const auto operations = part.kernel_operations();
    const auto part_boundaries = kernel.evaluate_history(operations);
    if (part_boundaries.size() != part_features) {
        std::cerr << "part fixture calculation produced unexpected boundaries\n";
        return EXIT_FAILURE;
    }

    zima::assembly::AssemblyDocument scene_fixture =
        zima::assembly::AssemblyDocument::create_default();
    for (std::size_t index = 0; index < assembly_components; ++index) {
        auto occurrence = zima::assembly::AssemblyDocument::create_part_occurrence(
            "benchmark-component-" + std::to_string(index), "benchmark-part",
            {}, kernel.make_box({10.0, 10.0, 10.0}));
        occurrence.placement.x = static_cast<double>(index % 32) * 14.0;
        occurrence.placement.y = static_cast<double>(index / 32) * 14.0;
        scene_fixture.components.push_back(std::move(occurrence));
    }
    const auto scene = scene_fixture.build_scene();
    const auto scene_ms = milliseconds([&] {
        const auto built = scene_fixture.build_scene();
        if (built.triangles.size() != scene.triangles.size()) std::abort();
    }, repetitions);
    std::size_t picking_candidates = 0;
    const auto picking_ms = milliseconds([&] {
        const auto candidates = zima::viewer::ordered_viewer_candidates(
            scene, {0.0, 0.0, -100.0}, {0.0, 0.0, 1.0}, 0.5);
        if (candidates.empty()) std::abort();
        picking_candidates = candidates.size();
    }, repetitions);

    zima::workspace::Workspace workspace;
    auto source_part = part_fixture(4);
    source_part.document_id = "benchmark-source-part";
    const auto source_boundaries = kernel.evaluate_history(
        source_part.kernel_operations());
    workspace.add_part(std::move(source_part), source_boundaries);
    auto nested = zima::assembly::AssemblyDocument::create_default();
    nested.document_id = "benchmark-nested-assembly";
    workspace.add_assembly(std::move(nested));
    static_cast<void>(workspace.insert_open_part(
        "benchmark-nested-assembly", "benchmark-source-part", "nested-part"));
    for (std::size_t level = 1; level < nested_levels; ++level) {
        auto parent = zima::assembly::AssemblyDocument::create_default();
        parent.document_id = "benchmark-parent-" + std::to_string(level);
        workspace.add_assembly(std::move(parent));
        static_cast<void>(workspace.insert_open_assembly(
            "benchmark-parent-" + std::to_string(level),
            level == 1 ? "benchmark-nested-assembly"
                       : "benchmark-parent-" + std::to_string(level - 1),
            "nested-assembly"));
    }
    const std::string top_id =
        "benchmark-parent-" + std::to_string(nested_levels - 1);
    const auto regeneration_ms = milliseconds([&] {
        workspace.regenerate_assembly_from_open_dependencies(top_id);
    }, repetitions);

    std::cout << std::fixed << std::setprecision(3)
              << "ZIMA_PERF_V1 repetitions=" << repetitions << '\n'
              << "part_history features=" << part_features
              << " boundaries=" << part_boundaries.size()
              << " mean_ms=" << milliseconds([&] {
                     const auto result = kernel.evaluate_history(operations);
                     if (result.size() != part_features) std::abort();
                 }, repetitions) << '\n'
              << "assembly_scene components=" << assembly_components
              << " triangles=" << scene.triangles.size()
              << " mean_ms=" << scene_ms << '\n'
              << "assembly_picking components=" << assembly_components
              << " candidates=" << picking_candidates
              << " mean_ms=" << picking_ms << '\n'
              << "nested_regeneration levels=" << nested_levels
              << " mean_ms=" << regeneration_ms << '\n';
    return EXIT_SUCCESS;
}

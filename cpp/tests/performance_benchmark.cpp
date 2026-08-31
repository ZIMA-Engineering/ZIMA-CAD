#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/viewer/picking.hpp>
#include <zima/workspace/workspace.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

template <typename Function>
double milliseconds(Function&& function, int repetitions = 3);

int step_benchmark(const std::filesystem::path& source) {
    if (!std::filesystem::is_regular_file(source)) {
        std::cerr << "STEP benchmark input does not exist: " << source << '\n';
        return EXIT_FAILURE;
    }

    auto document = zima::document::PartDocument::create_default();
    document.history.clear();
    document.history_order.clear();
    auto step = zima::document::PartDocument::create_imported_step_container(
        std::filesystem::absolute(source));
    step.name = "STEP benchmark";

    zima::kernel::OcctKernel kernel;
    zima::kernel::BodyResult imported;
    const auto import_ms = milliseconds([&] {
        auto results = kernel.import_step_components(
            {{std::filesystem::absolute(source).string(), {}, {}, {},
              step.id, {}}});
        if (results.size() != 1 || results.front().kernel_shape.empty()) {
            std::abort();
        }
        imported = std::move(results.front());
    }, 1);

    step.imported_step.frozen_brep =
        std::make_shared<const std::string>(imported.kernel_shape);
    step.imported_step.topology = imported.imported_step_topology;
    document.history.push_back(step);
    document.insert_history_entry(
        zima::document::PartHistoryKind::Feature, step.id);

    auto marker = zima::document::PartDocument::create_box_container();
    marker.name = "Rollback marker";
    marker.suppressed = true;
    document.history.push_back(marker);
    document.insert_history_entry(
        zima::document::PartHistoryKind::Feature, marker.id);

    const auto document_operations = document.kernel_operations();
    auto boundaries = std::vector<zima::kernel::BodyResult>{imported, imported};
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        boundaries[index].source_fingerprint = zima::kernel::history_fingerprint(
            document_operations, index + 1);
    }
    zima::document::DocumentSession session(document, std::move(boundaries));
    const auto body_copy_ms = milliseconds([&] {
        auto copy = imported;
        if (copy.mesh.vertices.size() != imported.mesh.vertices.size()) std::abort();
    });
    const auto rollback_ms = milliseconds([&] {
        const auto boundary = session.rollback_boundary(marker.id);
        if (!boundary || !boundary->input_body) std::abort();
    });

    zima::kernel::Vec3 minimum{}, maximum{};
    if (!imported.mesh.vertices.empty()) {
        minimum = maximum = imported.mesh.vertices.front();
        for (const auto& point : imported.mesh.vertices) {
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
        }
    }
    const zima::kernel::Vec3 ray_origin{
        (minimum.x + maximum.x) * 0.5,
        (minimum.y + maximum.y) * 0.5,
        minimum.z - std::max(1.0, maximum.z - minimum.z)};
    std::size_t candidate_count{};
    bool source_face_candidate{};
    const auto picking_ms = milliseconds([&] {
        const auto candidates = zima::viewer::ordered_viewer_candidates(
            imported.mesh, ray_origin, {0.0, 0.0, 1.0},
            std::max(1.0e-6, (maximum.x - minimum.x) * 1.0e-4));
        candidate_count = candidates.size();
        source_face_candidate = std::ranges::any_of(candidates,
            [&](const auto& candidate) {
                return candidate.kind == zima::viewer::CandidateKind::Face &&
                    candidate.geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference &&
                    candidate.owner_id == step.id &&
                    candidate.semantic_key.starts_with("step:face:#");
            });
    });

    auto operations = document_operations;
    operations.resize(1);
    const auto explicit_recalculate_ms = milliseconds([&] {
        const auto result = kernel.evaluate_history(operations);
        if (result.size() != operations.size()) std::abort();
    }, 1);

    const auto temporary = std::filesystem::temp_directory_path() /
        "zima-step-performance-benchmark.prtz";
    const auto save_ms = milliseconds([&] {
        document.save(temporary, session.calculated_boundaries());
    }, 1);
    std::vector<zima::kernel::BodyResult> loaded_boundaries;
    std::size_t loaded_topology_count{};
    const auto load_ms = milliseconds([&] {
        loaded_boundaries.clear();
        const auto loaded = zima::document::PartDocument::load(
            temporary, &loaded_boundaries);
        if (loaded.history.empty() || loaded_boundaries.empty()) std::abort();
        loaded_topology_count = loaded.history.front().imported_step.topology.size();
        if (loaded_topology_count != imported.imported_step_topology.size()) {
            std::abort();
        }
    }, 1);
    const auto part_file_bytes = std::filesystem::file_size(temporary);
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);

    const auto& references = imported.mesh.original_references;
    std::size_t topology_faces{};
    std::size_t topology_edges{};
    std::size_t topology_vertices{};
    for (const auto& identity : imported.imported_step_topology) {
        using Kind = zima::kernel::StepRequest::TopologyIdentity::Kind;
        if (identity.semantic_key.find("step:") != 0 ||
            identity.semantic_key.find(":#") == std::string::npos) {
            std::cerr << "STEP topology does not use source entity identity\n";
            return EXIT_FAILURE;
        }
        switch (identity.kind) {
            case Kind::Face: ++topology_faces; break;
            case Kind::Edge: ++topology_edges; break;
            case Kind::Vertex: ++topology_vertices; break;
        }
    }
    if (topology_faces == 0 || references.triangle_references.empty()) {
        std::cerr << "STEP import produced no persistent source faces\n";
        return EXIT_FAILURE;
    }
    if (!source_face_candidate) {
        std::cerr << "Viewer did not offer an imported STEP source face\n";
        return EXIT_FAILURE;
    }
    std::cout << std::fixed << std::setprecision(3)
              << "ZIMA_STEP_PERF_V1 path=\"" << source.string() << "\"\n"
              << "input bytes=" << std::filesystem::file_size(source) << '\n'
              << "geometry vertices=" << imported.mesh.vertices.size()
              << " triangles=" << imported.mesh.triangles.size() / 3
              << " brep_bytes=" << imported.kernel_shape.size()
              << " reference_triangles=" << references.triangle_references.size()
              << " reference_edges=" << references.edges.size()
              << " topology_faces=" << topology_faces
              << " topology_edges=" << topology_edges
              << " topology_vertices=" << topology_vertices << '\n'
              << "step_import mean_ms=" << import_ms << '\n'
              << "body_copy mean_ms=" << body_copy_ms << '\n'
              << "properties_rollback mean_ms=" << rollback_ms << '\n'
              << "viewer_picking candidates=" << candidate_count
              << " source_face=1"
              << " mean_ms=" << picking_ms << '\n'
              << "explicit_recalculate mean_ms=" << explicit_recalculate_ms << '\n'
              << "part_save mean_ms=" << save_ms << '\n'
              << "part_load bytes=" << part_file_bytes
              << " topology=" << loaded_topology_count
              << " mean_ms=" << load_ms << '\n';
    return EXIT_SUCCESS;
}

template <typename Function>
double milliseconds(Function&& function, int repetitions) {
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

zima::sketcher::Sketch solver_fixture(std::size_t branch_count) {
    auto sketch = zima::sketcher::Sketch::create_default();
    for (std::size_t index = 0; index < branch_count; ++index) {
        auto anchor = zima::sketcher::Sketch::create_point(
            static_cast<double>(index) * 3.0,
            static_cast<double>(index) * 2.0);
        anchor.fixed = true;
        auto moving = zima::sketcher::Sketch::create_point(
            anchor.x + 3.0, anchor.y + 0.75);
        const auto anchor_id = anchor.id;
        const auto moving_id = moving.id;
        sketch.points.push_back(std::move(anchor));
        sketch.points.push_back(std::move(moving));
        auto segment = zima::sketcher::Sketch::create_segment(
            anchor_id, moving_id);
        const auto segment_id = segment.id;
        sketch.segments.push_back(std::move(segment));
        sketch.constraints.push_back({
            "perf-h:" + std::to_string(index),
            zima::sketcher::ConstraintKind::Horizontal,
            anchor_id, moving_id, false, segment_id});
        sketch.dimensions.push_back({
            "perf-d:" + std::to_string(index),
            zima::sketcher::DimensionKind::Distance,
            anchor_id, moving_id, 5.0});
    }
    return sketch;
}

zima::sketcher::Sketch connected_solver_fixture(std::size_t segment_count) {
    auto sketch = zima::sketcher::Sketch::create_default();
    auto first = zima::sketcher::Sketch::create_point(0.0, 0.0);
    first.fixed = true;
    sketch.points.push_back(std::move(first));
    for (std::size_t index = 0; index < segment_count; ++index) {
        auto point = zima::sketcher::Sketch::create_point(
            static_cast<double>(index + 1) * 3.0,
            index % 2 == 0 ? 0.5 : -0.5);
        const auto first_id = sketch.points.back().id;
        const auto second_id = point.id;
        sketch.points.push_back(std::move(point));
        auto segment = zima::sketcher::Sketch::create_segment(first_id, second_id);
        const auto segment_id = segment.id;
        sketch.segments.push_back(std::move(segment));
        sketch.constraints.push_back({
            "chain-h:" + std::to_string(index),
            zima::sketcher::ConstraintKind::Horizontal,
            first_id, second_id, false, segment_id});
        sketch.dimensions.push_back({
            "chain-d:" + std::to_string(index),
            zima::sketcher::DimensionKind::Distance,
            first_id, second_id, 3.0});
    }
    return sketch;
}

std::size_t mobility_component_count(const zima::sketcher::Sketch& sketch) {
    std::unordered_map<std::string, std::vector<std::string>> neighbors;
    for (const auto& point : sketch.points) neighbors[point.id];
    const auto connect = [&](const std::string& first, const std::string& second) {
        if (first.empty() || second.empty() || first == second ||
            !neighbors.contains(first) || !neighbors.contains(second)) return;
        neighbors[first].push_back(second);
        neighbors[second].push_back(first);
    };
    for (const auto& segment : sketch.segments) {
        connect(segment.first_point_id, segment.second_point_id);
    }
    for (const auto& constraint : sketch.constraints) {
        connect(constraint.first_point_id, constraint.second_point_id);
    }
    for (const auto& dimension : sketch.dimensions) {
        connect(dimension.first_point_id, dimension.second_point_id);
    }
    std::unordered_set<std::string> visited;
    std::size_t components{};
    for (const auto& [root, _] : neighbors) {
        if (visited.contains(root)) continue;
        ++components;
        std::vector<std::string> pending{root};
        while (!pending.empty()) {
            auto point = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(point).second) continue;
            for (const auto& neighbor : neighbors.at(point)) {
                if (!visited.contains(neighbor)) pending.push_back(neighbor);
            }
        }
    }
    return components;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--step") {
        try {
            return step_benchmark(argv[2]);
        } catch (const std::exception& error) {
            std::cerr << "STEP benchmark failed: " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
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
    const auto reference_count = [](const auto& geometry) {
        return geometry.triangle_references.size() + geometry.edges.size() +
            geometry.points.size() + geometry.axes.size();
    };
    std::size_t stored_reference_items = 0;
    std::size_t stored_kernel_bytes = 0;
    for (const auto& boundary : part_boundaries) {
        stored_reference_items += reference_count(
            boundary.mesh.original_references);
        stored_kernel_bytes += boundary.kernel_shape.size();
    }
    std::unordered_map<std::string, std::size_t> owner_reference_items;
    const auto& final_references =
        part_boundaries.back().mesh.original_references;
    for (const auto& reference : final_references.triangle_references) {
        ++owner_reference_items[reference.owner_id];
    }
    for (const auto& edge : final_references.edges) {
        ++owner_reference_items[edge.reference.owner_id];
    }
    for (const auto& point : final_references.points) {
        ++owner_reference_items[point.reference.owner_id];
    }
    for (const auto& axis : final_references.axes) {
        ++owner_reference_items[axis.reference.owner_id];
    }
    std::size_t cumulative_reference_items = 0;
    for (std::size_t index = 0; index < operations.size(); ++index) {
        cumulative_reference_items += owner_reference_items[operations[index].owner_id] *
            (operations.size() - index);
    }
    auto changed_part = part;
    changed_part.history.back().box.height = 24.0;
    const auto changed_operations = changed_part.kernel_operations();
    const auto part_incremental_ms = milliseconds([&] {
        const auto result = kernel.evaluate_history_incremental(
            changed_operations, part_boundaries);
        if (result.size() != part_features ||
            std::abs(result.back().volume - 93120.0) > 1.0e-6) std::abort();
    }, repetitions);
    const auto part_cold_incremental_ms = milliseconds([&] {
        zima::kernel::OcctKernel cold_kernel;
        const auto result = cold_kernel.evaluate_history_incremental(
            changed_operations, part_boundaries);
        if (result.size() != part_features ||
            std::abs(result.back().volume - 93120.0) > 1.0e-6) std::abort();
    }, repetitions);

    auto fillet_base_operations = operations;
    for (std::size_t index = 0; index < fillet_base_operations.size(); ++index) {
        auto& box = std::get<zima::kernel::BoxRequest>(
            fillet_base_operations[index].primitive);
        box.translation = {static_cast<double>(index) * 30.0, 0.0, 0.0};
    }
    const auto fillet_base_boundaries =
        kernel.evaluate_history(fillet_base_operations);
    const auto fillet_edge = std::find_if(
        fillet_base_boundaries.back().mesh.original_references.edges.begin(),
        fillet_base_boundaries.back().mesh.original_references.edges.end(),
        [](const auto& edge) {
            return edge.reference.owner_id == "benchmark-box-23";
        });
    if (fillet_edge ==
        fillet_base_boundaries.back().mesh.original_references.edges.end()) {
        std::cerr << "part fixture has no stable edge for Fillet benchmark\n";
        return EXIT_FAILURE;
    }
    auto fillet_operations = fillet_base_operations;
    fillet_operations.push_back({"benchmark-fillet",
        zima::kernel::FilletRequest{{fillet_edge->reference},
            zima::kernel::EdgeSelectionOrigin::OriginalEntity, 1.0},
        zima::kernel::BooleanOperation::Add});
    const auto fillet_boundaries = kernel.evaluate_history(fillet_operations);
    auto changed_fillet_operations = fillet_operations;
    std::get<zima::kernel::FilletRequest>(
        changed_fillet_operations.back().primitive).radius = 1.5;
    const auto fillet_incremental_ms = milliseconds([&] {
        const auto result = kernel.evaluate_history_incremental(
            changed_fillet_operations, fillet_boundaries);
        if (result.size() != fillet_operations.size() ||
            result.back().volume <= 0.0) std::abort();
    }, repetitions);
    const auto fillet_cold_incremental_ms = milliseconds([&] {
        zima::kernel::OcctKernel cold_kernel;
        const auto result = cold_kernel.evaluate_history_incremental(
            changed_fillet_operations, fillet_boundaries);
        if (result.size() != fillet_operations.size() ||
            result.back().volume <= 0.0) std::abort();
    }, repetitions);

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

    const auto sketch_solve_ms = [&](std::size_t branches) {
        const auto fixture = solver_fixture(branches);
        return milliseconds([&] {
            auto candidate = fixture;
            const auto solved = candidate.solve();
            if (solved.status != zima::sketcher::SolveStatus::Solved ||
                solved.remaining_degrees_of_freedom != 0 ||
                solved.maximum_residual > 1.0e-7) std::abort();
        }, repetitions);
    };
    const auto sketch_10_ms = sketch_solve_ms(10);
    const auto sketch_40_ms = sketch_solve_ms(40);
    const auto sketch_100_ms = sketch_solve_ms(100);
    const auto connected_solve_ms = [&](std::size_t segments) {
        const auto fixture = connected_solver_fixture(segments);
        return milliseconds([&] {
            auto candidate = fixture;
            const auto solved = candidate.solve();
            if (solved.status != zima::sketcher::SolveStatus::Solved ||
                solved.remaining_degrees_of_freedom != 0 ||
                solved.maximum_residual > 1.0e-7) std::abort();
        }, repetitions);
    };
    const auto chain_10_ms = connected_solve_ms(10);
    const auto chain_40_ms = connected_solve_ms(40);
    const auto chain_100_ms = connected_solve_ms(100);
    const auto chain_250_ms = connected_solve_ms(250);
    const auto graph_fixture = solver_fixture(1000);
    std::size_t graph_components{};
    const auto graph_1000_ms = milliseconds([&] {
        graph_components = mobility_component_count(graph_fixture);
        if (graph_components != 1000) std::abort();
    }, 25);
    auto drag_fixture = solver_fixture(100);
    if (drag_fixture.solve().status != zima::sketcher::SolveStatus::Solved) {
        std::abort();
    }
    const auto drag_point_id = drag_fixture.dimensions.back().second_point_id;
    drag_fixture.dimensions.pop_back();
    const auto drag_100_ms = milliseconds([&] {
        auto candidate = drag_fixture;
        const auto* point = candidate.find_point(drag_point_id);
        if (point == nullptr ||
            !candidate.move_point(drag_point_id, point->x + 1.0, point->y)) {
            std::abort();
        }
    }, repetitions);
    const auto dimension_fixture = solver_fixture(100);
    const auto dimension_edit_100_ms = milliseconds([&] {
        auto candidate = dimension_fixture;
        if (!candidate.set_dimension_value("perf-d:99", 6.0)) std::abort();
    }, repetitions);
    const auto constraint_fixture = solver_fixture(100);
    const auto constraint_remove_100_ms = milliseconds([&] {
        auto candidate = constraint_fixture;
        candidate.remove_constraint("perf-h:99");
    }, repetitions);
    auto cached_solve_fixture = solver_fixture(100);
    if (cached_solve_fixture.solve().status !=
        zima::sketcher::SolveStatus::Solved) std::abort();
    const auto cached_full_solve_100_ms = milliseconds([&] {
        auto candidate = cached_solve_fixture;
        if (candidate.solve().status != zima::sketcher::SolveStatus::Solved) {
            std::abort();
        }
    }, repetitions);
    auto constraint_add_fixture = solver_fixture(100);
    const auto added_segment_id = constraint_add_fixture.segments.back().id;
    constraint_add_fixture.constraints.pop_back();
    if (constraint_add_fixture.solve().remaining_degrees_of_freedom != 1) {
        std::abort();
    }
    const auto constraint_add_100_ms = milliseconds([&] {
        auto candidate = constraint_add_fixture;
        static_cast<void>(candidate.add_segment_constraint(
            added_segment_id, zima::sketcher::ConstraintKind::Horizontal));
    }, repetitions);
    auto large_drag_fixture = solver_fixture(1000);
    if (large_drag_fixture.solve().status != zima::sketcher::SolveStatus::Solved) {
        std::abort();
    }
    const auto large_drag_point_id =
        large_drag_fixture.dimensions.back().second_point_id;
    large_drag_fixture.dimensions.pop_back();
    const auto drag_1000_ms = milliseconds([&] {
        auto candidate = large_drag_fixture;
        const auto* point = candidate.find_point(large_drag_point_id);
        if (point == nullptr ||
            !candidate.move_point(
                large_drag_point_id, point->x + 1.0, point->y)) std::abort();
    }, 1);
    const auto large_dimension_fixture = solver_fixture(1000);
    const auto dimension_edit_1000_ms = milliseconds([&] {
        auto candidate = large_dimension_fixture;
        if (!candidate.set_dimension_value("perf-d:999", 6.0)) std::abort();
    }, 1);
    const auto copy_1000_ms = milliseconds([&] {
        auto candidate = large_dimension_fixture;
        if (candidate.points.size() != 2000) std::abort();
    }, 3);
    const auto validate_1000_ms = milliseconds([&] {
        large_dimension_fixture.validate();
    }, 3);

    std::cout << std::fixed << std::setprecision(3)
              << "ZIMA_PERF_V1 repetitions=" << repetitions << '\n'
              << "part_history features=" << part_features
              << " boundaries=" << part_boundaries.size()
              << " volume=" << part_boundaries.back().volume
              << " mean_ms=" << milliseconds([&] {
                     const auto result = kernel.evaluate_history(operations);
                     if (result.size() != part_features) std::abort();
                 }, repetitions) << '\n'
              << "part_incremental changed_index=" << (part_features - 1)
              << " reused_boundaries=" << (part_features - 1)
              << " mean_ms=" << part_incremental_ms << '\n'
              << "part_cold_incremental changed_index=" << (part_features - 1)
              << " persisted_brep=final_only"
              << " mean_ms=" << part_cold_incremental_ms << '\n'
              << "part_reference_cache stored_items=" << stored_reference_items
              << " cumulative_items=" << cumulative_reference_items
              << " kernel_bytes=" << stored_kernel_bytes << '\n'
              << "part_fillet_incremental changed_index=" << part_features
              << " reused_boundaries=" << part_features
              << " mean_ms=" << fillet_incremental_ms << '\n'
              << "part_fillet_cold_incremental changed_index=" << part_features
              << " mean_ms=" << fillet_cold_incremental_ms << '\n'
              << "assembly_scene components=" << assembly_components
              << " triangles=" << scene.triangles.size()
              << " mean_ms=" << scene_ms << '\n'
              << "assembly_picking components=" << assembly_components
              << " candidates=" << picking_candidates
              << " mean_ms=" << picking_ms << '\n'
              << "nested_regeneration levels=" << nested_levels
              << " mean_ms=" << regeneration_ms << '\n'
              << "sketch_solver branches=10 variables=20 equations=20"
              << " mean_ms=" << sketch_10_ms << '\n'
              << "sketch_solver branches=40 variables=80 equations=80"
              << " mean_ms=" << sketch_40_ms << '\n'
              << "sketch_solver branches=100 variables=200 equations=200"
              << " mean_ms=" << sketch_100_ms << '\n'
              << "sketch_solver_chain segments=10 components=1 mean_ms="
              << chain_10_ms << '\n'
              << "sketch_solver_chain segments=40 components=1 mean_ms="
              << chain_40_ms << '\n'
              << "sketch_solver_chain segments=100 components=1 mean_ms="
              << chain_100_ms << '\n'
              << "sketch_solver_chain segments=250 components=1 mean_ms="
              << chain_250_ms << '\n'
              << "sketch_mobility_graph points=2000 components="
              << graph_components << " mean_ms=" << graph_1000_ms << '\n'
              << "sketch_interaction operation=drag branches=100 mean_ms="
              << drag_100_ms << '\n'
              << "sketch_interaction operation=dimension_edit branches=100 mean_ms="
              << dimension_edit_100_ms << '\n'
              << "sketch_interaction operation=constraint_remove branches=100 mean_ms="
              << constraint_remove_100_ms << '\n'
              << "sketch_interaction operation=cached_full_solve branches=100 mean_ms="
              << cached_full_solve_100_ms << '\n'
              << "sketch_interaction operation=constraint_add branches=100 mean_ms="
              << constraint_add_100_ms << '\n'
              << "sketch_interaction operation=drag branches=1000 mean_ms="
              << drag_1000_ms << '\n'
              << "sketch_interaction operation=dimension_edit branches=1000 mean_ms="
              << dimension_edit_1000_ms << '\n'
              << "sketch_interaction operation=copy branches=1000 mean_ms="
              << copy_1000_ms << '\n'
              << "sketch_interaction operation=validate branches=1000 mean_ms="
              << validate_1000_ms << '\n';
    return EXIT_SUCCESS;
}

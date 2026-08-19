#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <tuple>
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
        part.constructions.push_back(zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point));
        part.constructions.push_back(zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Axis));
        part.constructions.push_back(zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane));
        auto part_calculation = kernel.evaluate_history(part.kernel_operations());
        auto assembly = zima::assembly::AssemblyDocument::create_default();
        const std::string part_id = part.document_id;
        const std::string assembly_id = assembly.document_id;
        workspace.add_part(std::move(part), part_calculation, "open-part.prtz");
        workspace.add_assembly(std::move(assembly));
        auto drawing = zima::drawing::DrawingDocument::create_default();
        const std::string drawing_id = drawing.document_id;
        workspace.add_drawing(std::move(drawing), "open-drawing.drwz");
        require(workspace.size() == 3 && workspace.open_part(part_id) != nullptr &&
                    workspace.open_assembly(assembly_id) != nullptr &&
                    workspace.open_drawing(drawing_id) != nullptr,
                "Workspace did not retain typed open documents");
        require(workspace.document_id_for_path("open-part.prtz") == part_id &&
                    !workspace.authoritative_viewer_mesh(part_id).triangles.empty(),
                "Workspace did not expose the authoritative open Drawing source");
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
        const auto repeated_occurrence_id = workspace.insert_open_part(
            assembly_id, part_id, "Druhý výskyt stejného dílu");
        require(repeated_occurrence_id != occurrence_id,
                "Repeated Part insertion reused occurrence identity");
        const auto* inserted = workspace.open_assembly(assembly_id)
            ->session.document().find_occurrence(occurrence_id);
        require(inserted != nullptr && inserted->source_document_id == part_id,
                "Workspace did not insert the authoritative open Part");
        const auto first_path = zima::assembly::InstancePath{}.child(occurrence_id);
        const auto repeated_path =
            zima::assembly::InstancePath{}.child(repeated_occurrence_id);
        const auto activated_repeated = workspace.activate_occurrence(
            assembly_id, repeated_path);
        require(activated_repeated &&
                    activated_repeated->instance_path == repeated_path &&
                    workspace.active_document_id() == part_id &&
                    workspace.displayed_document_id() == assembly_id,
                "Exact repeated occurrence activation replaced its top-level Assembly");
        const auto activated_first = workspace.activate_occurrence(
            assembly_id, first_path);
        require(activated_first && activated_first->instance_path == first_path,
                "Repeated source occurrences were ambiguous during activation");
        require(inserted->calculated_source.mesh.points.size() >= 1 &&
                    inserted->calculated_source.mesh.axes.size() >= 1 &&
                    std::any_of(
                        inserted->calculated_source.mesh.original_references
                            .triangle_references.begin(),
                        inserted->calculated_source.mesh.original_references
                            .triangle_references.end(),
                        [](const auto& reference) {
                            return reference.semantic_key == "plane";
                        }),
                "Part construction references were not captured by Assembly insertion");
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
        workspace.add_assembly(std::move(subassembly), "subassembly.asmz");
        const std::string nested_part_occurrence = workspace.insert_open_part(
            subassembly_id, part_id, "Vnitřní díl");
        auto placed_subassembly = workspace.open_assembly(subassembly_id)
            ->session.document();
        placed_subassembly.find_occurrence(nested_part_occurrence)->placement = {
            10.0, 0.0, 0.0, 0.0, 0.0, 90.0};
        workspace.open_assembly(subassembly_id)->session.commit(
            std::move(placed_subassembly));
        auto topassembly = zima::assembly::AssemblyDocument::create_default();
        topassembly.name = "Horní sestava";
        const std::string topassembly_id = topassembly.document_id;
        workspace.add_assembly(std::move(topassembly), "topassembly.asmz");
        const std::string subassembly_occurrence = workspace.insert_open_assembly(
            topassembly_id, subassembly_id, "Vložená podsestava");
        const std::string direct_part_occurrence = workspace.insert_open_part(
            topassembly_id, part_id, "Přímý kontextový díl");
        auto reference_part = zima::document::PartDocument::create_default();
        reference_part.name = "Zdroj externí reference";
        reference_part.history.push_back(
            zima::document::PartDocument::create_box_container());
        reference_part.history.front().box.length = 35.0;
        const std::string reference_part_id = reference_part.document_id;
        workspace.add_part(reference_part,
            kernel.evaluate_history(reference_part.kernel_operations()),
            "reference-part.prtz");
        const std::string reference_occurrence = workspace.insert_open_part(
            topassembly_id, reference_part_id, "Zdrojový díl");
        auto placed_topassembly = workspace.open_assembly(topassembly_id)
            ->session.document();
        placed_topassembly.find_occurrence(reference_occurrence)->placement.x = 200.0;
        workspace.open_assembly(topassembly_id)->session.commit(
            std::move(placed_topassembly));
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
        require(!nested_scene.original_references.triangle_references.empty() &&
                    nested_scene.original_references.triangle_references.front().instance_path ==
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
        const auto scene_point = workspace.occurrence_point_to_scene(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path),
            {1.0, 2.0, 3.0});
        require(std::abs(scene_point.x - 8.0) < 1.0e-9 &&
                    std::abs(scene_point.y - 1.0) < 1.0e-9 &&
                    std::abs(scene_point.z - 3.0) < 1.0e-9,
                "Nested occurrence point transform did not compose placements");
        const auto local_point = workspace.occurrence_point_from_scene(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path), scene_point);
        require(std::abs(local_point.x - 1.0) < 1.0e-9 &&
                    std::abs(local_point.y - 2.0) < 1.0e-9 &&
                    std::abs(local_point.z - 3.0) < 1.0e-9,
                "Nested occurrence point transform did not round-trip");
        const auto scene_direction = workspace.occurrence_direction_to_scene(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path),
            {1.0, 2.0, 0.0});
        const auto local_direction = workspace.occurrence_direction_from_scene(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path),
            scene_direction);
        require(std::abs(scene_direction.x + 2.0) < 1.0e-9 &&
                    std::abs(scene_direction.y - 1.0) < 1.0e-9 &&
                    std::abs(local_direction.x - 1.0) < 1.0e-9 &&
                    std::abs(local_direction.y - 2.0) < 1.0e-9,
                "Nested occurrence direction transform did not round-trip");
        auto moved_source_assembly = workspace.open_assembly(subassembly_id)
            ->session.document();
        moved_source_assembly.find_occurrence(nested_part_occurrence)->placement.x = 100.0;
        workspace.open_assembly(subassembly_id)->session.commit(
            std::move(moved_source_assembly));
        const auto persisted_scene_point = workspace.occurrence_point_to_scene(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path),
            {1.0, 2.0, 3.0});
        require(std::abs(persisted_scene_point.x - 8.0) < 1.0e-9 &&
                    std::abs(persisted_scene_point.y - 1.0) < 1.0e-9,
                "Open source placement leaked through the parent Assembly snapshot");
        const auto rollback_scene = workspace.build_scene_with_part_override(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path), {});
        const std::string direct_part_path = zima::assembly::InstancePath{}
            .child(direct_part_occurrence).encoded();
        const std::string reference_part_path = zima::assembly::InstancePath{}
            .child(reference_occurrence).encoded();
        const auto external_geometry =
            workspace.authoritative_external_reference_geometry(
                topassembly_id,
                zima::assembly::InstancePath::decode(expected_nested_path),
                reference_part_id);
        require(!external_geometry.edges.empty() &&
                    std::all_of(external_geometry.edges.begin(),
                        external_geometry.edges.end(), [&](const auto& edge) {
                            return edge.reference.instance_path == reference_part_path;
                        }),
                "Authoritative external geometry lost exact source occurrence identity");
        const auto authoritative_scene = workspace.open_assembly(topassembly_id)
            ->session.document().build_scene();
        const auto source_scene_edge = std::find_if(
            authoritative_scene.original_references.edges.begin(),
            authoritative_scene.original_references.edges.end(),
            [&](const auto& edge) {
                return edge.reference.instance_path == reference_part_path &&
                    edge.reference.owner_id ==
                        external_geometry.edges.front().reference.owner_id &&
                    edge.reference.semantic_key ==
                        external_geometry.edges.front().reference.semantic_key;
            });
        require(source_scene_edge !=
                    authoritative_scene.original_references.edges.end(),
                "External reference source edge is unavailable in Assembly scene");
        require(std::abs(external_geometry.edges.front().points.front().x -
                    source_scene_edge->points.front().x) > 1.0e-9 ||
                    std::abs(external_geometry.edges.front().points.front().y -
                    source_scene_edge->points.front().y) > 1.0e-9,
                "External reference geometry was not transformed into dependent Part space");
        const auto persisted_reference_scene = workspace.open_assembly(topassembly_id)
            ->session.document().build_scene();
        auto changed_reference_part = workspace.open_part(reference_part_id)
            ->session.document();
        changed_reference_part.history.front().box.length += 41.0;
        changed_reference_part.history.front().box.width += 13.0;
        auto changed_reference_calculation = kernel.evaluate_history(
            changed_reference_part.kernel_operations());
        workspace.open_part(reference_part_id)->session.commit(
            std::move(changed_reference_part),
            std::move(changed_reference_calculation));
        const auto refreshed_external_geometry =
            workspace.authoritative_external_reference_geometry(
                topassembly_id,
                zima::assembly::InstancePath::decode(expected_nested_path),
                reference_part_id);
        const auto point_tuples = [](const auto& points) {
            std::set<std::tuple<double, double, double>> result;
            for (const auto& point : points) {
                result.emplace(point.x, point.y, point.z);
            }
            return result;
        };
        require(point_tuples(refreshed_external_geometry.vertices) !=
                    point_tuples(external_geometry.vertices) &&
                    point_tuples(workspace.open_assembly(topassembly_id)
                        ->session.document().build_scene().vertices) ==
                    point_tuples(persisted_reference_scene.vertices),
                "Explicit external refresh did not use open unsaved source or mutated parent cache");
        workspace.add_external_sketch_dependency(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path),
            zima::assembly::InstancePath::decode(reference_part_path));
        require(std::any_of(workspace.open_assembly(topassembly_id)
                    ->session.document().dependencies.begin(),
                    workspace.open_assembly(topassembly_id)
                    ->session.document().dependencies.end(), [&](const auto& dependency) {
                        return dependency.dependent_occurrence_id ==
                                subassembly_occurrence &&
                            dependency.prerequisite_occurrence_id ==
                                reference_occurrence &&
                            dependency.kind == zima::assembly::
                                ComponentDependencyKind::ExternalSketchReference;
                    }),
                "External Sketch dependency was not stored at the common Assembly owner");
        const auto dependency_revision =
            workspace.open_assembly(topassembly_id)->session.revision();
        workspace.add_external_sketch_dependency(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path),
            zima::assembly::InstancePath::decode(reference_part_path));
        require(workspace.open_assembly(topassembly_id)->session.revision() ==
                    dependency_revision,
                "Repeated external dependency created an intermediate transaction");
        bool reverse_external_cycle_rejected = false;
        try {
            workspace.add_external_sketch_dependency(
                topassembly_id,
                zima::assembly::InstancePath::decode(reference_part_path),
                zima::assembly::InstancePath::decode(expected_nested_path));
        } catch (const std::invalid_argument&) {
            reverse_external_cycle_rejected = true;
        }
        require(reverse_external_cycle_rejected,
                "External Sketch dependency accepted an indirect occurrence cycle");
        require(workspace.open_assembly(topassembly_id)->session.revision() ==
                    dependency_revision,
                "Rejected external dependency partially changed the Assembly");
        bool repeated_source_cycle_rejected = false;
        try {
            workspace.add_external_sketch_dependency(
                topassembly_id,
                zima::assembly::InstancePath::decode(direct_part_path),
                zima::assembly::InstancePath::decode(expected_nested_path));
        } catch (const std::invalid_argument&) {
            repeated_source_cycle_rejected = true;
        }
        require(repeated_source_cycle_rejected,
                "Repeated Part source accepted a contextual self-dependency");
        require(!rollback_scene.original_references.triangle_references.empty() &&
                    std::all_of(rollback_scene.original_references.triangle_references.begin(),
                        rollback_scene.original_references.triangle_references.end(),
                        [&](const auto& reference) {
                            return reference.instance_path == direct_part_path ||
                                reference.instance_path == reference_part_path;
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
        changed_again.history.front().box.width += 17.0;
        changed_again.history.front().box.length += 19.0;
        changed_again.history.front().box.height += 23.0;
        auto changed_again_calculation =
            kernel.evaluate_history(changed_again.kernel_operations());
        workspace.open_part(part_id)->session.commit(
            std::move(changed_again), changed_again_calculation);
        require(maximum_y(workspace.open_assembly(topassembly_id)
                    ->session.document().build_scene()) == old_nested_maximum_y,
                "Nested Part edit implicitly regenerated a top-level Assembly");
        auto live_boundary =
            workspace.open_part(part_id)->session.calculated_boundaries().back();
        for (auto& vertex : live_boundary.mesh.vertices) vertex.z += 7.0;
        for (auto& vertex : live_boundary.mesh.original_references.vertices) {
            vertex.z += 7.0;
        }
        const auto live_nested_scene = workspace.build_scene_with_part_override(
            topassembly_id,
            zima::assembly::InstancePath::decode(expected_nested_path),
            std::move(live_boundary));
        const auto vertices_for_path = [](const zima::kernel::ViewerMesh& scene,
                                           const std::string& instance_path) {
            std::set<std::tuple<double, double, double>> result;
            const auto& references = scene.original_references;
            for (std::size_t triangle = 0;
                 triangle < references.triangle_references.size(); ++triangle) {
                if (references.triangle_references[triangle].instance_path !=
                    instance_path) {
                    continue;
                }
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    const auto& vertex =
                        references.vertices[
                            references.triangles[triangle * 3 + corner]];
                    result.emplace(vertex.x, vertex.y, vertex.z);
                }
            }
            return result;
        };
        require(vertices_for_path(live_nested_scene, expected_nested_path) !=
                    vertices_for_path(nested_scene, expected_nested_path),
                "Active nested Part occurrence did not display its live calculated state");
        require(std::any_of(live_nested_scene.original_references
                    .triangle_references.begin(),
                    live_nested_scene.original_references.triangle_references.end(),
                    [&](const auto& reference) {
                        return reference.instance_path == expected_nested_path;
                    }) &&
                std::any_of(live_nested_scene.original_references
                    .triangle_references.begin(),
                    live_nested_scene.original_references.triangle_references.end(),
                    [&](const auto& reference) {
                        return reference.instance_path == direct_part_path;
                    }),
                "Live nested Part display lost target or passive sibling identity");
        require(maximum_y(workspace.open_assembly(topassembly_id)
                    ->session.document().build_scene()) == old_nested_maximum_y,
                "Live nested Part display mutated the parent Assembly snapshot");
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
                    regenerated_nested_scene.original_references.triangle_references.front().instance_path ==
                        expected_nested_path,
                "Top-level Regenerate did not pull nested geometry or preserve identity");
        require(workspace.open_assembly(topassembly_id)->session.document()
                    .find_occurrence(subassembly_occurrence)
                    ->nested_snapshot.front().name == "Přejmenovaný vnitřní díl",
                "Top-level Regenerate did not refresh nested structural snapshot");
        const auto nested_save_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-nested-snapshot-contract.asmz";
        workspace.open_assembly(topassembly_id)->session.document().save(
            nested_save_path);
        const auto loaded_nested = zima::assembly::AssemblyDocument::load(
            nested_save_path);
        std::filesystem::remove(nested_save_path);
        require(loaded_nested.find_occurrence(subassembly_occurrence)
                    ->nested_snapshot.front().name ==
                        "Přejmenovaný vnitřní díl" &&
                    std::abs(loaded_nested.find_occurrence(subassembly_occurrence)
                        ->nested_snapshot.front().placement.x - 100.0) < 1.0e-9,
                "Nested structural snapshot or placement did not survive save/load");
        auto missing_source_assembly = workspace.open_assembly(topassembly_id)
            ->session.document();
        std::erase_if(missing_source_assembly.dependencies,
            [&](const auto& dependency) {
                return dependency.dependent_occurrence_id == reference_occurrence ||
                    dependency.prerequisite_occurrence_id == reference_occurrence;
            });
        std::erase_if(missing_source_assembly.components,
            [&](const auto& occurrence) {
                return occurrence.occurrence_id == reference_occurrence;
            });
        workspace.open_assembly(topassembly_id)->session.commit(
            std::move(missing_source_assembly));
        const auto missing_external_geometry =
            workspace.authoritative_external_reference_geometry(
                topassembly_id,
                zima::assembly::InstancePath::decode(expected_nested_path),
                reference_part_id);
        require(missing_external_geometry.edges.empty() &&
                    missing_external_geometry.triangle_references.empty(),
                "Removed external source occurrence remained authoritative");
        auto missing_reference_sketch = zima::sketcher::Sketch::create_default();
        const auto cacheable_edge = std::find_if(
            external_geometry.edges.begin(), external_geometry.edges.end(),
            [&](const auto& edge) {
                if (edge.points.size() < 2) return false;
                const auto first = missing_reference_sketch.local_point(
                    edge.points.front());
                const auto last = missing_reference_sketch.local_point(
                    edge.points.back());
                return std::hypot(first[0] - last[0], first[1] - last[1]) > 1.0e-9;
            });
        require(cacheable_edge != external_geometry.edges.end(),
                "External geometry has no edge projectable into the test Sketch");
        auto missing_reference = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Edge);
        missing_reference.source_document_id = reference_part_id;
        missing_reference.source_owner_id =
            cacheable_edge->reference.owner_id;
        missing_reference.source_semantic_key =
            cacheable_edge->reference.semantic_key;
        missing_reference.source_instance_path = reference_part_path;
        missing_reference.context_assembly_document_id = topassembly_id;
        missing_reference.context_instance_path = expected_nested_path;
        for (const auto& point : cacheable_edge->points) {
            const auto local = missing_reference_sketch.local_point(point);
            if (missing_reference.cached_points.empty() ||
                std::hypot(local[0] - missing_reference.cached_points.back()[0],
                           local[1] - missing_reference.cached_points.back()[1]) >
                    1.0e-9) {
                missing_reference.cached_points.push_back(local);
            }
        }
        missing_reference_sketch.add_external_reference(missing_reference);
        const auto cached_missing_points = missing_reference.cached_points;
        auto contextual_document = zima::document::PartDocument::create_default();
        contextual_document.sketches.push_back(std::move(missing_reference_sketch));
        require(workspace.refresh_context_external_references(contextual_document) &&
                    contextual_document.sketches.front()
                        .external_references.front().broken &&
                    contextual_document.sketches.front()
                        .external_references.front().cached_points ==
                        cached_missing_points,
                "Missing contextual source did not preserve cache and mark reference broken");
        bool assembly_cycle_rejected = false;
        try {
            static_cast<void>(workspace.insert_open_assembly(
                subassembly_id, topassembly_id, "Zakázaný cyklus"));
        } catch (const std::invalid_argument&) {
            assembly_cycle_rejected = true;
        }
        require(assembly_cycle_rejected,
                "Workspace accepted an indirect nested Assembly cycle");
        workspace.display_top_level(topassembly_id);
        workspace.activate(part_id);
        require(workspace.remove(topassembly_id) &&
                    workspace.find(topassembly_id) == nullptr &&
                    workspace.active_document_id() == part_id &&
                    workspace.displayed_document_id() != topassembly_id,
                "Closing the displayed document lost independent edit activation");
        require(workspace.remove(part_id) &&
                    workspace.find(part_id) == nullptr &&
                    !workspace.active_document_id().empty() &&
                    workspace.find(workspace.active_document_id()) != nullptr,
                "Closing the active document did not choose a valid replacement");
        std::cout << "C++ Workspace contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

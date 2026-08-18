#include <zima/assembly/assembly_document.hpp>
#include <zima/assembly/assembly_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/viewer/picking.hpp>

#include <iostream>
#include <cmath>
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
        std::set<std::string> assembly_axis_paths;
        for (const auto& axis : scene.axes) {
            assembly_axis_paths.insert(axis.reference.instance_path);
        }
        require(scene.axes.size() == 6 && assembly_axis_paths == instance_paths,
                "Assembly did not transform and distinguish occurrence axes");
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
        auto mated_assembly = loaded;
        auto plane_mate = zima::assembly::AssemblyDocument::create_mate(
            "Plocha na plochu",
            zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "z_max"}, 2.5);
        const std::string plane_mate_id = plane_mate.mate_id;
        mated_assembly.add_mate(std::move(plane_mate));
        require(mated_assembly.mates.size() == 1 &&
                    mated_assembly.dependencies.size() == 1 &&
                    mated_assembly.dependencies.front().dependency_id == plane_mate_id &&
                    mated_assembly.dependencies.front().dependent_occurrence_id == second_id,
                "Assembly mate did not create its explicit dependency edge");
        const auto dependent_plane = mated_assembly.resolve_plane(
            mated_assembly.mates.front().dependent);
        const auto prerequisite_plane = mated_assembly.resolve_plane(
            mated_assembly.mates.front().prerequisite);
        require(dependent_plane.status == zima::assembly::MateStatus::Valid &&
                    prerequisite_plane.status == zima::assembly::MateStatus::Valid &&
                    std::abs(dependent_plane.plane.point.z - 30.0) < 1.0e-7 &&
                    std::abs(prerequisite_plane.plane.point.z - 15.0) < 1.0e-7,
                "Persisted viewer packet did not resolve the selected planes");
        auto missing_reference = mated_assembly.mates.front().dependent;
        missing_reference.semantic_key = "missing-face";
        require(mated_assembly.resolve_plane(missing_reference).status ==
                    zima::assembly::MateStatus::MissingReference,
                "Missing persisted face reference was not detected");
        mated_assembly.calculate_mates();
        const auto calculated_dependent = mated_assembly.resolve_plane(
            mated_assembly.mates.front().dependent);
        const auto calculated_prerequisite = mated_assembly.resolve_plane(
            mated_assembly.mates.front().prerequisite);
        const auto& calculated_normal = calculated_prerequisite.plane.normal;
        const double calculated_offset =
            (calculated_dependent.plane.point.x - calculated_prerequisite.plane.point.x) *
                calculated_normal.x +
            (calculated_dependent.plane.point.y - calculated_prerequisite.plane.point.y) *
                calculated_normal.y +
            (calculated_dependent.plane.point.z - calculated_prerequisite.plane.point.z) *
                calculated_normal.z;
        const auto calculated_placement = mated_assembly.components.back().placement;
        mated_assembly.calculate_mates();
        require(mated_assembly.mates.front().status ==
                    zima::assembly::MateStatus::Valid &&
                    std::abs(calculated_offset - 2.5) < 1.0e-7 &&
                    mated_assembly.components.back().placement.x == calculated_placement.x &&
                    mated_assembly.components.back().placement.y == calculated_placement.y &&
                    mated_assembly.components.back().placement.z == calculated_placement.z,
                "Plane mate did not calculate its offset idempotently");
        auto broken_mate_assembly = mated_assembly;
        broken_mate_assembly.mates.front().dependent.semantic_key = "missing-face";
        broken_mate_assembly.calculate_mates();
        require(broken_mate_assembly.mates.front().status ==
                    zima::assembly::MateStatus::MissingReference &&
                    broken_mate_assembly.effectively_suppressed_occurrences()
                        .contains(second_id),
                "Broken mate did not suppress its dependent component");
        broken_mate_assembly.mates.front().dependent.semantic_key = "z_min";
        broken_mate_assembly.calculate_mates();
        require(broken_mate_assembly.mates.front().status ==
                    zima::assembly::MateStatus::Valid &&
                    !broken_mate_assembly.effectively_suppressed_occurrences()
                        .contains(second_id),
                "Repaired mate remained trapped in its previous error suppression");
        const auto mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-mate-contract.zca.json";
        mated_assembly.save(mate_path);
        const auto loaded_mates = zima::assembly::AssemblyDocument::load(mate_path);
        std::filesystem::remove(mate_path);
        require(loaded_mates.mates == mated_assembly.mates &&
                    loaded_mates.dependencies == mated_assembly.dependencies,
                "Assembly mate reference or dependency did not survive save/load");
        auto lifecycle = loaded_mates;
        lifecycle.mates.front().suppressed = true;
        lifecycle.components.front().suppressed = true;
        lifecycle.calculate_mates();
        const auto suppressed_lifecycle =
            lifecycle.effectively_suppressed_occurrences();
        require(suppressed_lifecycle.contains(first_id) &&
                    !suppressed_lifecycle.contains(second_id),
                "Suppressed mate continued propagating through its dependency edge");
        lifecycle.components.front().suppressed = false;
        auto edited_mate = lifecycle.mates.front();
        edited_mate.suppressed = false;
        edited_mate.offset = 7.0;
        lifecycle.replace_mate(edited_mate);
        lifecycle.calculate_mates();
        require(lifecycle.mates.size() == 1 && lifecycle.dependencies.size() == 1 &&
                    lifecycle.mates.front().offset == 7.0 &&
                    lifecycle.mates.front().status == zima::assembly::MateStatus::Valid,
                "Replacing a mate lost its identity, dependency, or calculation");
        lifecycle.remove_mate(edited_mate.mate_id);
        require(lifecycle.mates.empty() && lifecycle.dependencies.empty(),
                "Removing a mate left its dependency edge behind");
        zima::assembly::AssemblySession mate_session(mated_assembly);
        auto suppressed_revision = mate_session.document();
        suppressed_revision.mates.front().suppressed = true;
        suppressed_revision.calculate_mates();
        mate_session.commit(std::move(suppressed_revision));
        require(mate_session.revision() == 1 &&
                    mate_session.document().mates.front().suppressed &&
                    mate_session.undo() &&
                    !mate_session.document().mates.front().suppressed &&
                    mate_session.redo() &&
                    mate_session.document().mates.front().suppressed,
                "Mate suppression did not behave as one Undo/Redo revision");
        auto removed_revision = mate_session.document();
        removed_revision.remove_mate(plane_mate_id);
        mate_session.commit(std::move(removed_revision));
        require(mate_session.document().mates.empty() &&
                    mate_session.document().dependencies.empty() &&
                    mate_session.undo() &&
                    mate_session.document().mates.size() == 1 &&
                    mate_session.document().dependencies.size() == 1,
                "Mate removal did not restore its dependency through Undo");
        auto rotated_axis_mate = loaded;
        rotated_axis_mate.components.back().placement.x = 25.0;
        rotated_axis_mate.components.back().placement.rotation_x = 90.0;
        rotated_axis_mate.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Natočená osa", zima::assembly::MateKind::AxisCoincident,
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "axis:z"},
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "axis:z"}));
        rotated_axis_mate.calculate_mates();
        const auto rotated_axis_dependent = rotated_axis_mate.resolve_axis(
            rotated_axis_mate.mates.front().dependent);
        const auto rotated_axis_prerequisite = rotated_axis_mate.resolve_axis(
            rotated_axis_mate.mates.front().prerequisite);
        const double rotated_axis_alignment =
            rotated_axis_dependent.axis.direction.x *
                rotated_axis_prerequisite.axis.direction.x +
            rotated_axis_dependent.axis.direction.y *
                rotated_axis_prerequisite.axis.direction.y +
            rotated_axis_dependent.axis.direction.z *
                rotated_axis_prerequisite.axis.direction.z;
        const auto rotated_axis_placement = rotated_axis_mate.components.back().placement;
        rotated_axis_mate.calculate_mates();
        require(rotated_axis_mate.mates.front().status ==
                    zima::assembly::MateStatus::Valid &&
                    std::abs(std::abs(rotated_axis_alignment) - 1.0) < 1.0e-7 &&
                    std::abs(rotated_axis_mate.components.back().placement.rotation_x -
                             rotated_axis_placement.rotation_x) < 1.0e-7 &&
                    std::abs(rotated_axis_mate.components.back().placement.rotation_y -
                             rotated_axis_placement.rotation_y) < 1.0e-7 &&
                    std::abs(rotated_axis_mate.components.back().placement.rotation_z -
                             rotated_axis_placement.rotation_z) < 1.0e-7,
                "Axis mate did not rotate a perpendicular axis idempotently");
        const auto rotated_mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-rotated-mate-contract.zca.json";
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
                    loaded_rotated_mate.mates.front().status ==
                        zima::assembly::MateStatus::Valid,
                "Calculated rotational mate placement did not survive save/load");
        auto rotated_plane_mate = loaded;
        rotated_plane_mate.components.back().placement.rotation_y = 90.0;
        rotated_plane_mate.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Natočená plocha", zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "z_max"}, 4.0));
        rotated_plane_mate.calculate_mates();
        const auto rotated_plane_dependent = rotated_plane_mate.resolve_plane(
            rotated_plane_mate.mates.front().dependent);
        const auto rotated_plane_prerequisite = rotated_plane_mate.resolve_plane(
            rotated_plane_mate.mates.front().prerequisite);
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
        require(rotated_plane_mate.mates.front().status ==
                    zima::assembly::MateStatus::Valid &&
                    std::abs(std::abs(rotated_plane_alignment) - 1.0) < 1.0e-7 &&
                    std::abs(rotated_plane_offset - 4.0) < 1.0e-7,
                "Plane mate did not rotate and offset a perpendicular plane");
        auto combined_mates = loaded;
        combined_mates.components.back().placement.x = 20.0;
        combined_mates.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Osa na osu", zima::assembly::MateKind::AxisCoincident,
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "axis:z"},
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "axis:z"}));
        combined_mates.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Doraz", zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "z_max"}));
        combined_mates.calculate_mates();
        const auto combined_axis_dependent = combined_mates.resolve_axis(
            combined_mates.mates.front().dependent);
        const auto combined_axis_prerequisite = combined_mates.resolve_axis(
            combined_mates.mates.front().prerequisite);
        require(combined_mates.mates[0].status == zima::assembly::MateStatus::Valid &&
                    combined_mates.mates[1].status == zima::assembly::MateStatus::Valid &&
                    std::abs(combined_axis_dependent.axis.point.x -
                             combined_axis_prerequisite.axis.point.x) < 1.0e-7 &&
                    std::abs(combined_axis_dependent.axis.point.y -
                             combined_axis_prerequisite.axis.point.y) < 1.0e-7,
                "Axis and plane mates did not preserve their independent constraints");
        auto conflicting_mates = loaded;
        conflicting_mates.components.back().placement.x = 17.0;
        const auto placement_before_conflict = conflicting_mates.components.back().placement;
        conflicting_mates.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Osa Z", zima::assembly::MateKind::AxisCoincident,
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "axis:z"},
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "axis:z"}));
        conflicting_mates.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Plocha Z na X", zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "x_max"}));
        conflicting_mates.calculate_mates();
        require(conflicting_mates.mates[0].status ==
                    zima::assembly::MateStatus::UnsupportedGeometry &&
                    conflicting_mates.mates[1].status ==
                    zima::assembly::MateStatus::UnsupportedGeometry &&
                    conflicting_mates.components.back().placement.x ==
                        placement_before_conflict.x &&
                    conflicting_mates.components.back().placement.y ==
                        placement_before_conflict.y &&
                    conflicting_mates.components.back().placement.z ==
                        placement_before_conflict.z &&
                    conflicting_mates.components.back().placement.rotation_x ==
                        placement_before_conflict.rotation_x &&
                    conflicting_mates.components.back().placement.rotation_y ==
                        placement_before_conflict.rotation_y &&
                    conflicting_mates.components.back().placement.rotation_z ==
                        placement_before_conflict.rotation_z,
                "Conflicting rotational mates damaged the previous placement");
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

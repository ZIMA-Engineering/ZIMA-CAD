#include <zima/assembly/assembly_document.hpp>
#include <zima/assembly/assembly_session.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/viewer/picking.hpp>

#include <iostream>
#include <cmath>
#include <filesystem>
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
        assembly.relations = {{"double_clearance", "clearance * 2"}};
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
        const auto loaded = zima::assembly::AssemblyDocument::load(assembly_path);
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
                    loaded.relations == assembly.relations &&
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
        auto point_mated_assembly = loaded;
        const auto dependent_point_source =
            point_for_path(zima::assembly::InstancePath{}.child(second_id).encoded());
        const auto prerequisite_point_source =
            point_for_path(zima::assembly::InstancePath{}.child(first_id).encoded());
        const auto ownership_before = point_mated_assembly;
        bool nested_mate_rejected = false;
        try {
            point_mated_assembly.add_mate(
                zima::assembly::AssemblyDocument::create_mate(
                    "Neplatná rodičovská vazba",
                    zima::assembly::MateKind::PointCoincident,
                    {zima::assembly::MateReferenceKind::Point,
                     zima::assembly::InstancePath{}.child(second_id).child("nested-part"),
                     dependent_point_source.reference.owner_id,
                     dependent_point_source.reference.semantic_key},
                    {zima::assembly::MateReferenceKind::Point,
                     zima::assembly::InstancePath{}.child(first_id),
                     prerequisite_point_source.reference.owner_id,
                     prerequisite_point_source.reference.semantic_key}));
        } catch (const std::invalid_argument&) {
            nested_mate_rejected = true;
        }
        require(nested_mate_rejected &&
                    point_mated_assembly.mates == ownership_before.mates &&
                    point_mated_assembly.dependencies ==
                        ownership_before.dependencies,
                "Parent Assembly accepted or partially stored a mate to nested internals");
        point_mated_assembly.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Bod na bod", zima::assembly::MateKind::PointCoincident,
            {zima::assembly::MateReferenceKind::Point,
             zima::assembly::InstancePath{}.child(second_id),
             dependent_point_source.reference.owner_id,
             dependent_point_source.reference.semantic_key},
            {zima::assembly::MateReferenceKind::Point,
             zima::assembly::InstancePath{}.child(first_id),
             prerequisite_point_source.reference.owner_id,
             prerequisite_point_source.reference.semantic_key}));
        point_mated_assembly.calculate_mates();
        const auto resolved_dependent_point = point_mated_assembly.resolve_point(
            point_mated_assembly.mates.front().dependent);
        const auto resolved_prerequisite_point = point_mated_assembly.resolve_point(
            point_mated_assembly.mates.front().prerequisite);
        require(point_mated_assembly.mates.front().status ==
                    zima::assembly::MateStatus::Valid &&
                    std::abs(resolved_dependent_point.point.x -
                             resolved_prerequisite_point.point.x) < 1.0e-7 &&
                    std::abs(resolved_dependent_point.point.y -
                             resolved_prerequisite_point.point.y) < 1.0e-7 &&
                    std::abs(resolved_dependent_point.point.z -
                             resolved_prerequisite_point.point.z) < 1.0e-7 &&
                    point_mated_assembly.remaining_degrees_of_freedom(second_id) == 3,
                "Point mate did not align persisted original-solid points");
        auto redundant_point_assembly = point_mated_assembly;
        redundant_point_assembly.add_mate(
            zima::assembly::AssemblyDocument::create_mate(
                "Redundantní bod", zima::assembly::MateKind::PointCoincident,
                point_mated_assembly.mates.front().dependent,
                point_mated_assembly.mates.front().prerequisite));
        redundant_point_assembly.calculate_mates();
        require(redundant_point_assembly.remaining_degrees_of_freedom(second_id) == 3,
                "Redundant Assembly mate was counted as three new constraints");
        const auto point_mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-point-mate-contract.asmz";
        point_mated_assembly.save(point_mate_path);
        const auto loaded_point_mate =
            zima::assembly::AssemblyDocument::load(point_mate_path);
        std::filesystem::remove(point_mate_path);
        require(loaded_point_mate.mates == point_mated_assembly.mates,
                "Point mate reference did not survive save/load");
        auto angled_assembly = loaded;
        auto angle_mate = zima::assembly::AssemblyDocument::create_mate(
            "Úhel os", zima::assembly::MateKind::AxisAngle,
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "axis:z"},
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "axis:z"});
        angle_mate.angle_degrees = 60.0;
        angle_mate.lower_limit = 30.0;
        angle_mate.upper_limit = 90.0;
        angled_assembly.add_mate(std::move(angle_mate));
        angled_assembly.calculate_mates();
        const auto angled_dependent = angled_assembly.resolve_axis(
            angled_assembly.mates.front().dependent);
        const auto angled_prerequisite = angled_assembly.resolve_axis(
            angled_assembly.mates.front().prerequisite);
        const double angle_alignment =
            angled_dependent.axis.direction.x * angled_prerequisite.axis.direction.x +
            angled_dependent.axis.direction.y * angled_prerequisite.axis.direction.y +
            angled_dependent.axis.direction.z * angled_prerequisite.axis.direction.z;
        require(angled_assembly.mates.front().status ==
                    zima::assembly::MateStatus::Valid &&
                    std::abs(angle_alignment - 0.5) < 1.0e-7 &&
                    angled_assembly.remaining_degrees_of_freedom(second_id) == 5,
                "Axis angle mate did not reach its requested angle");
        const auto angled_placement = angled_assembly.components.back().placement;
        angled_assembly.calculate_mates();
        require(angled_assembly.components.back().placement.rotation_x ==
                    angled_placement.rotation_x &&
                    angled_assembly.components.back().placement.rotation_y ==
                    angled_placement.rotation_y &&
                    angled_assembly.components.back().placement.rotation_z ==
                    angled_placement.rotation_z,
                "Axis angle mate was not idempotent");
        const auto angled_scene = angled_assembly.build_scene();
        require(angled_scene.dimensions.size() == 1 &&
                    angled_scene.dimensions.front().reference.owner_id ==
                        angled_assembly.document_id &&
                    angled_scene.dimensions.front().reference.semantic_key ==
                        "mate:" + angled_assembly.mates.front().mate_id &&
                    angled_scene.dimensions.front().unit_suffix == " °" &&
                    angled_scene.dimensions.front().value == 60.0,
                "Axis angle mate did not create its editable viewer dimension");
        const auto limited_mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-limited-mate-contract.asmz";
        angled_assembly.save(limited_mate_path);
        const auto loaded_limited_mate =
            zima::assembly::AssemblyDocument::load(limited_mate_path);
        std::filesystem::remove(limited_mate_path);
        require(loaded_limited_mate.mates.front().lower_limit == 30.0 &&
                    loaded_limited_mate.mates.front().upper_limit == 90.0,
                "Assembly mate absolute limits did not survive save/load");
        auto value_edit = loaded_limited_mate;
        const std::string limited_mate_id = value_edit.mates.front().mate_id;
        require(value_edit.set_mate_value(limited_mate_id, 75.0) &&
                    value_edit.mates.front().angle_degrees == 75.0 &&
                    value_edit.mates.front().status ==
                        zima::assembly::MateStatus::Valid,
                "Transactional Assembly mate value edit did not calculate");
        const auto valid_value_placement = value_edit.components.back().placement;
        require(!value_edit.set_mate_value(limited_mate_id, 100.0) &&
                    value_edit.mates.front().angle_degrees == 75.0 &&
                    value_edit.components.back().placement.rotation_x ==
                        valid_value_placement.rotation_x &&
                    value_edit.components.back().placement.rotation_y ==
                        valid_value_placement.rotation_y &&
                    value_edit.components.back().placement.rotation_z ==
                        valid_value_placement.rotation_z,
                "Rejected Assembly mate value damaged the previous valid state");
        require(!value_edit.set_mate_value(limited_mate_id,
                    std::numeric_limits<double>::infinity()),
                "Assembly accepted a non-finite mate value");
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
        auto invalid_limited_angle = zima::assembly::AssemblyDocument::create_mate(
            "Neplatný úhel", zima::assembly::MateKind::AxisAngle,
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "axis:z"},
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "axis:z"});
        invalid_limited_angle.angle_degrees = 20.0;
        invalid_limited_angle.lower_limit = 30.0;
        bool rejected_outside_limit = false;
        auto invalid_limited_assembly = loaded;
        try { invalid_limited_assembly.add_mate(std::move(invalid_limited_angle)); }
        catch (const std::invalid_argument&) { rejected_outside_limit = true; }
        require(rejected_outside_limit,
                "Assembly accepted a mate value outside its absolute limits");
        auto plane_angled_assembly = loaded;
        auto plane_angle_mate = zima::assembly::AssemblyDocument::create_mate(
            "Úhel ploch", zima::assembly::MateKind::PlaneAngle,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "z_max"});
        plane_angle_mate.angle_degrees = 45.0;
        plane_angled_assembly.add_mate(std::move(plane_angle_mate));
        plane_angled_assembly.calculate_mates();
        const auto plane_angled_dependent = plane_angled_assembly.resolve_plane(
            plane_angled_assembly.mates.front().dependent);
        const auto plane_angled_prerequisite = plane_angled_assembly.resolve_plane(
            plane_angled_assembly.mates.front().prerequisite);
        const double plane_angle_alignment =
            plane_angled_dependent.plane.normal.x *
                plane_angled_prerequisite.plane.normal.x +
            plane_angled_dependent.plane.normal.y *
                plane_angled_prerequisite.plane.normal.y +
            plane_angled_dependent.plane.normal.z *
                plane_angled_prerequisite.plane.normal.z;
        require(plane_angled_assembly.mates.front().status ==
                    zima::assembly::MateStatus::Valid &&
                    std::abs(plane_angle_alignment - std::sqrt(0.5)) < 1.0e-7 &&
                    plane_angled_assembly.remaining_degrees_of_freedom(second_id) == 5,
                "Plane angle mate did not reach its requested angle");
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
        const auto mated_scene = mated_assembly.build_scene();
        require(mated_scene.dimensions.size() == 1 &&
                    mated_scene.dimensions.front().reference.owner_id ==
                        mated_assembly.document_id &&
                    mated_scene.dimensions.front().reference.semantic_key ==
                        "mate:" + mated_assembly.mates.front().mate_id &&
                    mated_scene.dimensions.front().unit_suffix == " mm" &&
                    mated_scene.dimensions.front().value == 2.5,
                "Plane mate did not create its editable viewer dimension");
        auto flipped_plane_assembly = loaded;
        auto flipped_plane = zima::assembly::AssemblyDocument::create_mate(
            "Obrácená plocha",
            zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "z_max"});
        flipped_plane.flipped = true;
        flipped_plane_assembly.add_mate(std::move(flipped_plane));
        flipped_plane_assembly.calculate_mates();
        const auto flipped_dependent = flipped_plane_assembly.resolve_plane(
            flipped_plane_assembly.mates.front().dependent);
        const auto flipped_prerequisite = flipped_plane_assembly.resolve_plane(
            flipped_plane_assembly.mates.front().prerequisite);
        const double flipped_alignment =
            flipped_dependent.plane.normal.x * flipped_prerequisite.plane.normal.x +
            flipped_dependent.plane.normal.y * flipped_prerequisite.plane.normal.y +
            flipped_dependent.plane.normal.z * flipped_prerequisite.plane.normal.z;
        require(flipped_plane_assembly.mates.front().status ==
                    zima::assembly::MateStatus::Valid &&
                    std::abs(flipped_alignment + 1.0) < 1.0e-7,
                "Flipped Plane mate did not preserve opposite face orientation");
        const auto flipped_mate_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-flipped-mate-contract.asmz";
        flipped_plane_assembly.save(flipped_mate_path);
        const auto loaded_flipped_mates =
            zima::assembly::AssemblyDocument::load(flipped_mate_path);
        std::filesystem::remove(flipped_mate_path);
        require(loaded_flipped_mates.mates.front().flipped,
                "Assembly mate Flip did not survive save/load");
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
            "zima-cad-cpp-mate-contract.asmz";
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
        auto two_plane_mates = loaded;
        two_plane_mates.components.back().placement = {23.0, 9.0, 14.0, 0.0, 0.0, 0.0};
        two_plane_mates.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Doraz Z", zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "z_max"}));
        two_plane_mates.add_mate(zima::assembly::AssemblyDocument::create_mate(
            "Doraz X", zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(second_id),
             "same-source-container", "x_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child(first_id),
             "same-source-container", "x_max"}));
        two_plane_mates.calculate_mates();
        require(two_plane_mates.mates[0].status == zima::assembly::MateStatus::Valid &&
                    two_plane_mates.mates[1].status == zima::assembly::MateStatus::Valid,
                "Two independent Plane mates on one component were not preserved");
        auto two_axis_mates = loaded;
        two_axis_mates.components.back().placement = {23.0, 9.0, 14.0, 0.0, 0.0, 0.0};
        for (const auto* axis : {"axis:z", "axis:x"}) {
            two_axis_mates.add_mate(zima::assembly::AssemblyDocument::create_mate(
                std::string("Souosost ") + axis,
                zima::assembly::MateKind::AxisCoincident,
                {zima::assembly::MateReferenceKind::Axis,
                 zima::assembly::InstancePath{}.child(second_id),
                 "same-source-container", axis},
                {zima::assembly::MateReferenceKind::Axis,
                 zima::assembly::InstancePath{}.child(first_id),
                 "same-source-container", axis}));
        }
        two_axis_mates.calculate_mates();
        require(two_axis_mates.mates[0].status == zima::assembly::MateStatus::Valid &&
                    two_axis_mates.mates[1].status == zima::assembly::MateStatus::Valid,
                "Two independent Axis mates on one component were not preserved");
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
        state_document.components.front().grounded = true;
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
        auto datum_point = zima::assembly::AssemblyDocument::create_construction(
            zima::document::ConstructionKind::Point);
        auto datum_axis = zima::assembly::AssemblyDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        const auto datum_point_id = datum_point.id;
        const auto datum_axis_id = datum_axis.id;
        datum_assembly.constructions = {datum_point, datum_axis, datum_plane};
        const auto datum_scene = datum_assembly.build_scene();
        require(datum_assembly.find_construction(datum_id) != nullptr &&
                    std::any_of(datum_scene.original_references.triangle_references.begin(),
                        datum_scene.original_references.triangle_references.end(),
                        [&](const auto& reference) {
                            return reference.instance_path.empty() &&
                                reference.owner_id == datum_id &&
                                reference.semantic_key == "plane";
                        }) &&
                    std::any_of(datum_scene.original_references.points.begin(),
                        datum_scene.original_references.points.end(),
                        [&](const auto& point) {
                            return point.reference.instance_path.empty() &&
                                point.reference.owner_id == datum_point_id;
                        }) &&
                    std::any_of(datum_scene.original_references.axes.begin(),
                        datum_scene.original_references.axes.end(),
                        [&](const auto& axis) {
                            return axis.reference.instance_path.empty() &&
                                axis.reference.owner_id == datum_axis_id;
                        }),
                "Assembly-owned point, axis or plane lost viewer identity");
        const auto dependent_face = *std::find_if(
            datum_scene.original_references.triangle_references.begin(),
            datum_scene.original_references.triangle_references.end(),
            [&](const auto& reference) {
                return reference.instance_path ==
                    zima::assembly::InstancePath{}.child(first_id).encoded();
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
        referenced_plane.offset = 7.0;
        associative_assembly.constructions.push_back(referenced_plane);
        auto second_referenced_plane = referenced_plane;
        second_referenced_plane.id += "-second-occurrence";
        second_referenced_plane.references.front().instance_path =
            zima::assembly::InstancePath{{second_id}}.encoded();
        associative_assembly.constructions.push_back(second_referenced_plane);
        associative_assembly.resolve_constructions();
        require(associative_assembly.constructions.front().reference_valid &&
                    associative_assembly.constructions.back().reference_valid &&
                    associative_assembly.constructions.front().origin.z !=
                        associative_assembly.constructions.back().origin.z &&
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
        auto datum_mate = zima::assembly::AssemblyDocument::create_mate(
            "Komponenta na montážní rovinu",
            zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{{first_id}}, dependent_face.owner_id,
             dependent_face.semantic_key},
            {zima::assembly::MateReferenceKind::Face, {}, datum_id, "plane"});
        datum_assembly.add_mate(std::move(datum_mate));
        datum_assembly.calculate_mates();
        require(datum_assembly.mates.back().status ==
                    zima::assembly::MateStatus::Valid &&
                    datum_assembly.dependencies.empty(),
                "Component could not mate to its owning Assembly datum plane");
        const auto datum_path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-assembly-datum-contract.asmz";
        datum_assembly.save(datum_path);
        const auto loaded_datum =
            zima::assembly::AssemblyDocument::load(datum_path);
        std::filesystem::remove(datum_path);
        require(loaded_datum.constructions.size() == 3 &&
                    loaded_datum.constructions.back().id == datum_id &&
                    loaded_datum.constructions.back().name == "Montážní rovina" &&
                    loaded_datum.mates.back().prerequisite.instance_path
                        .occurrence_ids.empty(),
                "Assembly datum identity or mate reference changed on save/load");
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

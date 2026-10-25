#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace zima::document {

enum class ExtrusionDirection { Forward, Reverse, Symmetric };
enum class ExtrusionExtent { Blind, UpToPlane, UpToSurface, ThroughAll };
enum class ProfileSource { Internal, External };
enum class ProfileResultType { Solid, Thin, Surface };
enum class ProfileExtentMode { OneSide, TwoSides, Symmetric };
enum class ThinMode { OneSide, OtherSide, Symmetric };
enum class EndCondition { Length, UpTo, ThroughAll };
enum class EndTargetKind { Point, Plane, Face };

// An explicit, read-only source dependency. The small calculated face cache is
// self-contained native data, not a copied Part/history or a required sidecar.
// Regeneration refreshes it from the authoritative source document.
struct ExternalProfileTarget {
    std::string document_id;
    std::filesystem::path source_file;
    std::string context_assembly_id;
    std::string dependent_instance_path;
    std::string source_instance_path;
    kernel::BodySnapshot source_face;
    std::vector<kernel::KernelRigidPlacement> source_to_part;
    std::vector<kernel::KernelRigidPlacement> source_to_body;
    bool operator==(const ExternalProfileTarget&) const;
};
[[nodiscard]] nlohmann::json serialize_external_profile_target(const ExternalProfileTarget&);
[[nodiscard]] ExternalProfileTarget load_external_profile_target(const nlohmann::json&);
struct ExtrusionParameters {
    bool origin_centerline{};
    bool centroid_centerline{};
    bool sheet_cut{};
    bool sheet_cut_clearance{};
    std::string sketch_id;
    double profile_plane_offset{};
    ProfileSource profile_source{ProfileSource::Internal};
    ProfileResultType result_type{ProfileResultType::Solid};
    double thin_thickness{1.0};
    ThinMode thin_mode{ThinMode::OneSide};
    ProfileExtentMode extent_mode{ProfileExtentMode::OneSide};
    double length_forward{10.0};
    double length_reverse{60.0};
    EndCondition end_condition_forward{EndCondition::Length};
    EndCondition end_condition_reverse{EndCondition::Length};
    struct EndTarget {
        EndTargetKind kind{EndTargetKind::Face};
        zima::kernel::FaceReference reference;
        std::string label;
        zima::kernel::Vec3 fallback_origin;
        zima::kernel::Vec3 fallback_normal{0.0, 0.0, 1.0};
        std::vector<zima::kernel::Vec3> fallback_triangles;
        std::optional<ExternalProfileTarget> external_source;
        bool operator==(const EndTarget&) const = default;
    };
    std::vector<EndTarget> end_targets_forward;
    std::vector<EndTarget> end_targets_reverse;
    double height{10.0};
    ExtrusionDirection direction{ExtrusionDirection::Forward};
    ExtrusionExtent extent{ExtrusionExtent::Blind};
    zima::kernel::FaceReference target_face;
    zima::kernel::Vec3 target_plane_origin;
    zima::kernel::Vec3 target_plane_normal{0.0, 0.0, 1.0};
    std::vector<zima::kernel::Vec3> target_surface_triangles;
    bool operator==(const ExtrusionParameters&) const = default;
};

struct RevolutionParameters {
    bool origin_centerline{};
    bool centroid_centerline{};
    bool sheet_metal{};
    bool sheet_attachment{};
    bool thickness_override{};
    std::string sketch_id;
    // Stable finite construction segment whose supporting line is the
    // revolution axis. It belongs to the owned Sketch.
    std::string axis_segment_id;
    double profile_plane_offset{};
    ProfileSource profile_source{ProfileSource::Internal};
    ProfileResultType result_type{ProfileResultType::Solid};
    double thin_thickness{1.0};
    ThinMode thin_mode{ThinMode::OneSide};
    ProfileExtentMode extent_mode{ProfileExtentMode::OneSide};
    ExtrusionDirection direction{ExtrusionDirection::Forward};
    double angle_reverse{360.0};
    double angle_degrees{360.0};
    bool operator==(const RevolutionParameters&) const = default;
};

} // namespace zima::document

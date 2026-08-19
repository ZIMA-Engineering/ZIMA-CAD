#pragma once

#include <cstdint>
#include <bit>
#include <algorithm>
#include <string>
#include <vector>
#include <variant>
#include <type_traits>

namespace zima::kernel {

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

struct FaceReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;

    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const FaceReference&) const = default;
};

struct EdgeReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const EdgeReference&) const = default;
};

struct VertexReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const VertexReference&) const = default;
};

struct AxisReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const AxisReference&) const = default;
};

struct ViewerEdge {
    std::vector<Vec3> points;
    EdgeReference reference;
    bool construction{};
    // Screen-space helpers (Sketcher and construction planes) are painted as
    // UI overlays. Calculated body topology stays in the depth-tested GL pass.
    bool overlay{};
};

struct ViewerPoint {
    Vec3 position;
    VertexReference reference;
};

struct ViewerAxis {
    Vec3 point;
    Vec3 direction{0.0, 0.0, 1.0};
    double display_length{100.0};
    AxisReference reference;
};

struct ViewerDimension {
    Vec3 witness_first;
    Vec3 witness_second;
    Vec3 line_first;
    Vec3 line_second;
    double value{};
    EdgeReference reference;
    std::string label_prefix;
    std::string unit_suffix{" mm"};
};

// Hidden persisted geometry owned by original ZIMA entities. It participates
// in picking and exact highlight only; the calculated OCCT result remains the
// sole shaded/display body.
struct ViewerReferenceGeometry {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> triangles;
    std::vector<FaceReference> triangle_references;
    std::vector<ViewerEdge> edges;
    std::vector<ViewerPoint> points;
    std::vector<ViewerAxis> axes;
};

struct ViewerMesh {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> triangles;
    // One entry per triangle. Invalid entries are displayed but never offered
    // as persistent modeling references.
    std::vector<FaceReference> triangle_references;
    std::vector<ViewerEdge> edges;
    std::vector<ViewerPoint> points;
    std::vector<ViewerAxis> axes;
    std::vector<ViewerDimension> dimensions;
    ViewerReferenceGeometry original_references;
};

struct BoxRequest {
    BoxRequest() = default;
    BoxRequest(double box_length, double box_width, double box_height)
        : length(box_length), width(box_width), height(box_height) {}

    double length{100.0};
    double width{80.0};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct CylinderRequest {
    double radius{40.0};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct SphereRequest {
    double radius{40.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct ConeRequest {
    double bottom_radius{20.0};
    double top_radius{};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct PyramidRequest {
    double length{40.0};
    double width{40.0};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct WedgeRequest {
    double length{60.0};
    double width{40.0};
    double height{40.0};
    double top_offset{30.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct ExtrusionRequest {
    enum class Extent { Blind, UpToPlane, UpToSurface, ThroughAll };
    struct PolygonProfile {
        std::vector<Vec3> vertices;
    };
    struct CircleProfile {
        Vec3 center;
        double radius{};
    };
    struct EllipseProfile {
        Vec3 center;
        Vec3 major_axis_direction{1.0, 0.0, 0.0};
        double major_radius{};
        double minor_radius{};
    };
    struct LineCurve {
        Vec3 start;
        Vec3 end;
    };
    struct ArcCurve {
        Vec3 start;
        Vec3 middle;
        Vec3 end;
    };
    struct EllipticalArcCurve {
        Vec3 start;
        Vec3 end;
        Vec3 center;
        Vec3 major_axis_direction{1.0, 0.0, 0.0};
        double major_radius{};
        double minor_radius{};
        double start_parameter{};
        double end_parameter{};
        bool reversed{};
    };
    struct BSplineCurve {
        Vec3 start;
        Vec3 end;
        std::vector<Vec3> control_points;
        unsigned degree{3};
        bool periodic{};
    };
    struct CurvedProfile {
        std::vector<std::variant<
            LineCurve, ArcCurve, EllipticalArcCurve, BSplineCurve>> curves;
    };
    using ProfileLoop = std::variant<
        PolygonProfile, CircleProfile, EllipseProfile, CurvedProfile>;
    struct ProfileRegion {
        std::string region_id;
        std::string outer_boundary_id;
        std::vector<std::string> inner_boundary_ids;
        ProfileLoop outer_profile{PolygonProfile{}};
        std::vector<ProfileLoop> inner_profiles;
    };
    std::string profile_region_id;
    std::string outer_boundary_id;
    std::vector<std::string> inner_boundary_ids;
    ProfileLoop outer_profile{PolygonProfile{}};
    std::vector<ProfileLoop> inner_profiles;
    std::vector<ProfileRegion> additional_profile_regions;
    Vec3 direction{0.0, 0.0, 10.0};
    Extent extent{Extent::Blind};
    FaceReference target_face;
    bool target_is_datum{};
    Vec3 target_plane_origin;
    Vec3 target_plane_normal{0.0, 0.0, 1.0};
    std::vector<Vec3> target_surface_triangles;
};

struct RevolutionRequest {
    ExtrusionRequest::ProfileLoop outer_profile{
        ExtrusionRequest::PolygonProfile{}};
    std::string profile_region_id;
    std::string outer_boundary_id;
    std::vector<std::string> inner_boundary_ids;
    std::vector<ExtrusionRequest::ProfileLoop> inner_profiles;
    std::vector<ExtrusionRequest::ProfileRegion> additional_profile_regions;
    Vec3 profile_normal{0.0, 0.0, 1.0};
    Vec3 axis_point;
    Vec3 axis_direction{1.0, 0.0, 0.0};
    double angle_degrees{360.0};
};

struct StepRequest {
    std::string source_path;
    std::string component_path;
};

enum class EdgeSelectionOrigin { OriginalEntity, OperationalBody };

struct FilletRequest {
    std::vector<EdgeReference> edges;
    EdgeSelectionOrigin origin{EdgeSelectionOrigin::OriginalEntity};
    double radius{1.0};
};

struct ChamferRequest {
    std::vector<EdgeReference> edges;
    EdgeSelectionOrigin origin{EdgeSelectionOrigin::OriginalEntity};
    double distance{1.0};
};

enum class BooleanOperation { Add, Subtract };

struct BoxOperation {
    std::string owner_id;
    BoxRequest box;
    BooleanOperation operation{BooleanOperation::Add};
};

using PrimitiveRequest = std::variant<
    BoxRequest, CylinderRequest, SphereRequest, ConeRequest, PyramidRequest, WedgeRequest,
    ExtrusionRequest, RevolutionRequest,
    StepRequest, FilletRequest, ChamferRequest>;

struct HistoryOperation {
    std::string owner_id;
    PrimitiveRequest primitive;
    BooleanOperation operation{BooleanOperation::Add};
};

struct BodyResult {
    ViewerMesh mesh;
    double volume{};
    double surface_area{};
    std::string source_fingerprint;
};

[[nodiscard]] inline std::string box_history_fingerprint(
    const std::vector<BoxOperation>& operations,
    std::size_t operation_count) {
    operation_count = std::min(operation_count, operations.size());
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append_byte = [&](std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    const auto append_u64 = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            append_byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    };
    append_u64(operation_count);
    for (std::size_t index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        append_u64(operation.owner_id.size());
        for (const unsigned char value : operation.owner_id) append_byte(value);
        append_byte(static_cast<std::uint8_t>(operation.operation));
        for (const double value : {
                operation.box.length, operation.box.width, operation.box.height,
                operation.box.translation.x, operation.box.translation.y,
                operation.box.translation.z, operation.box.rotation_degrees.x,
                operation.box.rotation_degrees.y,
                operation.box.rotation_degrees.z}) {
            append_u64(std::bit_cast<std::uint64_t>(value));
        }
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

[[nodiscard]] inline std::string history_fingerprint(
    const std::vector<HistoryOperation>& operations,
    std::size_t operation_count) {
    // A dedicated byte stream keeps primitive kind part of the identity while
    // preserving the established Box fingerprint for existing tests.
    std::uint64_t hash = 1469598103934665603ULL;
    const auto byte = [&](std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    const auto u64 = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    };
    operation_count = std::min(operation_count, operations.size());
    u64(operation_count);
    for (std::size_t index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        u64(operation.owner_id.size());
        for (const unsigned char value : operation.owner_id) byte(value);
        byte(static_cast<std::uint8_t>(operation.operation));
        byte(static_cast<std::uint8_t>(operation.primitive.index()));
        std::visit([&](const auto& primitive) {
            using Request = std::decay_t<decltype(primitive)>;
            if constexpr (std::is_same_v<Request, BoxRequest>) {
                for (const double value : {
                        primitive.length, primitive.width, primitive.height,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, CylinderRequest>) {
                for (const double value : {
                        primitive.radius, primitive.height,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, SphereRequest>) {
                for (const double value : {
                        primitive.radius, primitive.translation.x,
                        primitive.translation.y, primitive.translation.z,
                        primitive.rotation_degrees.x, primitive.rotation_degrees.y,
                        primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, ConeRequest>) {
                for (const double value : {primitive.bottom_radius,
                        primitive.top_radius, primitive.height,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, PyramidRequest>) {
                for (const double value : {primitive.length, primitive.width,
                        primitive.height, primitive.translation.x,
                        primitive.translation.y, primitive.translation.z,
                        primitive.rotation_degrees.x, primitive.rotation_degrees.y,
                        primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, WedgeRequest>) {
                for (const double value : {primitive.length, primitive.width,
                        primitive.height, primitive.top_offset,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, ExtrusionRequest>) {
                const auto append_profile = [&](const auto& profile_variant) {
                    byte(static_cast<std::uint8_t>(profile_variant.index()));
                    std::visit([&](const auto& profile) {
                        using Profile = std::decay_t<decltype(profile)>;
                        if constexpr (std::is_same_v<Profile,
                                          ExtrusionRequest::PolygonProfile>) {
                            u64(profile.vertices.size());
                            for (const auto& point : profile.vertices) {
                                for (const double value : {point.x, point.y, point.z}) {
                                    u64(std::bit_cast<std::uint64_t>(value));
                                }
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::CircleProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::EllipseProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.major_axis_direction.x,
                                    profile.major_axis_direction.y,
                                    profile.major_axis_direction.z,
                                    profile.major_radius, profile.minor_radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else {
                            u64(profile.curves.size());
                            for (const auto& curve : profile.curves) {
                                byte(static_cast<std::uint8_t>(curve.index()));
                                std::visit([&](const auto& exact_curve) {
                                    const auto append_point = [&](const Vec3& point) {
                                        for (const double value : {
                                                point.x, point.y, point.z}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                    };
                                    append_point(exact_curve.start);
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::ArcCurve>) {
                                        append_point(exact_curve.middle);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::EllipticalArcCurve>) {
                                        append_point(exact_curve.center);
                                        append_point(exact_curve.major_axis_direction);
                                        for (const double value : {
                                                exact_curve.major_radius,
                                                exact_curve.minor_radius,
                                                exact_curve.start_parameter,
                                                exact_curve.end_parameter}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                        byte(exact_curve.reversed);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::BSplineCurve>) {
                                        u64(exact_curve.degree);
                                        byte(exact_curve.periodic);
                                        u64(exact_curve.control_points.size());
                                        for (const auto& point : exact_curve.control_points) {
                                            append_point(point);
                                        }
                                    }
                                    append_point(exact_curve.end);
                                }, curve);
                            }
                        }
                    }, profile_variant);
                };
                append_profile(primitive.outer_profile);
                u64(primitive.profile_region_id.size());
                for (const unsigned char value : primitive.profile_region_id) byte(value);
                u64(primitive.outer_boundary_id.size());
                for (const unsigned char value : primitive.outer_boundary_id) byte(value);
                u64(primitive.inner_boundary_ids.size());
                for (const auto& id : primitive.inner_boundary_ids) {
                    u64(id.size()); for (const unsigned char value : id) byte(value);
                }
                u64(primitive.inner_profiles.size());
                for (const auto& profile : primitive.inner_profiles) {
                    append_profile(profile);
                }
                u64(primitive.additional_profile_regions.size());
                for (const auto& region : primitive.additional_profile_regions) {
                    u64(region.region_id.size());
                    for (const unsigned char value : region.region_id) byte(value);
                    u64(region.outer_boundary_id.size());
                    for (const unsigned char value : region.outer_boundary_id) byte(value);
                    u64(region.inner_boundary_ids.size());
                    for (const auto& id : region.inner_boundary_ids) {
                        u64(id.size()); for (const unsigned char value : id) byte(value);
                    }
                    append_profile(region.outer_profile);
                    u64(region.inner_profiles.size());
                    for (const auto& profile : region.inner_profiles) append_profile(profile);
                }
                for (const double value : {
                        primitive.direction.x, primitive.direction.y,
                        primitive.direction.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                byte(static_cast<std::uint8_t>(primitive.extent));
                u64(primitive.target_face.owner_id.size());
                for (const unsigned char value : primitive.target_face.owner_id) byte(value);
                u64(primitive.target_face.semantic_key.size());
                for (const unsigned char value : primitive.target_face.semantic_key) byte(value);
                byte(primitive.target_is_datum);
                for (const double value : {primitive.target_plane_origin.x,
                        primitive.target_plane_origin.y, primitive.target_plane_origin.z,
                        primitive.target_plane_normal.x, primitive.target_plane_normal.y,
                        primitive.target_plane_normal.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                u64(primitive.target_surface_triangles.size());
                for (const auto& point : primitive.target_surface_triangles) {
                    for (const double value : {point.x, point.y, point.z}) {
                        u64(std::bit_cast<std::uint64_t>(value));
                    }
                }
            } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
                const auto append_profile = [&](const auto& profile_variant) {
                    byte(static_cast<std::uint8_t>(profile_variant.index()));
                    std::visit([&](const auto& profile) {
                        using Profile = std::decay_t<decltype(profile)>;
                        if constexpr (std::is_same_v<Profile,
                                          ExtrusionRequest::PolygonProfile>) {
                            u64(profile.vertices.size());
                            for (const auto& point : profile.vertices) {
                                for (const double value : {point.x, point.y, point.z}) {
                                    u64(std::bit_cast<std::uint64_t>(value));
                                }
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::CircleProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::EllipseProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.major_axis_direction.x,
                                    profile.major_axis_direction.y,
                                    profile.major_axis_direction.z,
                                    profile.major_radius, profile.minor_radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else {
                            u64(profile.curves.size());
                            for (const auto& curve : profile.curves) {
                                byte(static_cast<std::uint8_t>(curve.index()));
                                std::visit([&](const auto& exact_curve) {
                                    const auto point = [&](const Vec3& value) {
                                        for (const double coordinate : {
                                                value.x, value.y, value.z}) {
                                            u64(std::bit_cast<std::uint64_t>(coordinate));
                                        }
                                    };
                                    point(exact_curve.start);
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::ArcCurve>) {
                                        point(exact_curve.middle);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::EllipticalArcCurve>) {
                                        point(exact_curve.center);
                                        point(exact_curve.major_axis_direction);
                                        for (const double value : {
                                                exact_curve.major_radius,
                                                exact_curve.minor_radius,
                                                exact_curve.start_parameter,
                                                exact_curve.end_parameter}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                        byte(exact_curve.reversed);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::BSplineCurve>) {
                                        u64(exact_curve.degree);
                                        byte(exact_curve.periodic);
                                        u64(exact_curve.control_points.size());
                                        for (const auto& control : exact_curve.control_points) {
                                            point(control);
                                        }
                                    }
                                    point(exact_curve.end);
                                }, curve);
                            }
                        }
                    }, profile_variant);
                };
                append_profile(primitive.outer_profile);
                u64(primitive.profile_region_id.size());
                for (const unsigned char value : primitive.profile_region_id) byte(value);
                u64(primitive.outer_boundary_id.size());
                for (const unsigned char value : primitive.outer_boundary_id) byte(value);
                u64(primitive.inner_boundary_ids.size());
                for (const auto& id : primitive.inner_boundary_ids) {
                    u64(id.size()); for (const unsigned char value : id) byte(value);
                }
                u64(primitive.inner_profiles.size());
                for (const auto& profile : primitive.inner_profiles) {
                    append_profile(profile);
                }
                u64(primitive.additional_profile_regions.size());
                for (const auto& region : primitive.additional_profile_regions) {
                    u64(region.region_id.size());
                    for (const unsigned char value : region.region_id) byte(value);
                    u64(region.outer_boundary_id.size());
                    for (const unsigned char value : region.outer_boundary_id) byte(value);
                    u64(region.inner_boundary_ids.size());
                    for (const auto& id : region.inner_boundary_ids) {
                        u64(id.size()); for (const unsigned char value : id) byte(value);
                    }
                    append_profile(region.outer_profile);
                    u64(region.inner_profiles.size());
                    for (const auto& profile : region.inner_profiles) append_profile(profile);
                }
                for (const double value : {
                        primitive.profile_normal.x, primitive.profile_normal.y,
                        primitive.profile_normal.z,
                        primitive.axis_point.x, primitive.axis_point.y,
                        primitive.axis_point.z, primitive.axis_direction.x,
                        primitive.axis_direction.y, primitive.axis_direction.z,
                        primitive.angle_degrees}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, StepRequest>) {
                u64(primitive.source_path.size());
                for (const unsigned char value : primitive.source_path) byte(value);
                u64(primitive.component_path.size());
                for (const unsigned char value : primitive.component_path) byte(value);
            } else {
                u64(primitive.edges.size());
                for (const auto& edge : primitive.edges) {
                    u64(edge.owner_id.size());
                    for (const unsigned char value : edge.owner_id) byte(value);
                    u64(edge.semantic_key.size());
                    for (const unsigned char value : edge.semantic_key) byte(value);
                }
                byte(static_cast<std::uint8_t>(primitive.origin));
                if constexpr (std::is_same_v<Request, FilletRequest>) {
                    u64(std::bit_cast<std::uint64_t>(primitive.radius));
                } else {
                    u64(std::bit_cast<std::uint64_t>(primitive.distance));
                }
            }
        }, operation.primitive);
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

class GeometryKernel {
public:
    virtual ~GeometryKernel() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual BodyResult make_box(const BoxRequest& request) const = 0;
    [[nodiscard]] virtual BodyResult evaluate_boxes(
        const std::vector<BoxOperation>& operations) const = 0;
    [[nodiscard]] virtual std::vector<BodyResult> evaluate_box_boundaries(
        const std::vector<BoxOperation>& operations) const = 0;
    [[nodiscard]] virtual std::vector<BodyResult> evaluate_history(
        const std::vector<HistoryOperation>& operations) const = 0;
};

}  // namespace zima::kernel

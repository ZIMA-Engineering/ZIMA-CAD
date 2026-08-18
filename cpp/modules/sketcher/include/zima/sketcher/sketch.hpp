#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <filesystem>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace zima::sketcher {

enum class SketchPlane { XY, XZ, YZ };
enum class ConstraintKind { Horizontal, Vertical, Coincident };
enum class DimensionKind { Distance, DistanceX, DistanceY };
enum class SolveStatus { Solved, UnderConstrained, Conflicting, Invalid };

struct SketchPoint {
    std::string id;
    double x{};
    double y{};
    bool fixed{};
    bool construction{};
    bool operator==(const SketchPoint&) const = default;
};

struct SketchSegment {
    std::string id;
    std::string first_point_id;
    std::string second_point_id;
    bool construction{};
    bool operator==(const SketchSegment&) const = default;
};

struct SketchConstraint {
    std::string id;
    ConstraintKind kind{ConstraintKind::Coincident};
    std::string first_point_id;
    std::string second_point_id;
    bool suppressed{};
    bool operator==(const SketchConstraint&) const = default;
};

struct SketchDimension {
    std::string id;
    DimensionKind kind{DimensionKind::Distance};
    std::string first_point_id;
    std::string second_point_id;
    double value{};
    bool driving{true};
    bool suppressed{};
    std::optional<double> lower_limit;
    std::optional<double> upper_limit;
    bool operator==(const SketchDimension&) const = default;
};

struct SolveResult {
    SolveStatus status{SolveStatus::Invalid};
    std::size_t remaining_degrees_of_freedom{};
    double maximum_residual{};
};

class Sketch {
public:
    std::string id;
    std::string name{"Skica"};
    SketchPlane plane{SketchPlane::XY};
    double plane_offset{};
    std::vector<SketchPoint> points;
    std::vector<SketchSegment> segments;
    std::vector<SketchConstraint> constraints;
    std::vector<SketchDimension> dimensions;

    [[nodiscard]] static Sketch create_default();
    [[nodiscard]] static SketchPoint create_point(double x, double y);
    [[nodiscard]] static SketchSegment create_segment(
        std::string first_point_id, std::string second_point_id,
        bool construction = false);
    [[nodiscard]] SketchPoint* find_point(const std::string& point_id);
    [[nodiscard]] const SketchPoint* find_point(const std::string& point_id) const;
    void validate() const;
    [[nodiscard]] SolveResult solve(std::size_t maximum_iterations = 100);
    [[nodiscard]] bool set_dimension_value(
        const std::string& dimension_id, double value);
    [[nodiscard]] std::string add_segment(
        double first_x, double first_y, double second_x, double second_y,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] std::string add_segment_constraint(
        const std::string& segment_id, ConstraintKind kind);
    [[nodiscard]] SketchDimension create_segment_dimension(
        const std::string& segment_id, DimensionKind kind = DimensionKind::Distance) const;
    void apply_dimension(SketchDimension dimension);
    [[nodiscard]] zima::kernel::ViewerMesh viewer_mesh() const;
    [[nodiscard]] zima::kernel::Vec3 world_point(double x, double y) const;
    [[nodiscard]] std::optional<std::array<double, 2>> intersect_ray(
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction) const;
    [[nodiscard]] std::string serialized() const;
    [[nodiscard]] static Sketch from_serialized(const std::string& value);
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static Sketch load(const std::filesystem::path& path);
};

}  // namespace zima::sketcher

#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <array>
#include <optional>

namespace zima::kernel {
enum class MeasurementKind { Point, Curve, Face, Object, Axis, Plane };
struct MeasurementReference {
    MeasurementKind kind{MeasurementKind::Point};
    std::string owner_id, semantic_key, instance_path;
    bool operator==(const MeasurementReference&) const = default;
};
struct MeasurementValue {
    double value{};
    bool approximate{};
    bool operator==(const MeasurementValue&) const = default;
};
struct MeasurementValues {
    std::optional<Vec3> position;
    std::optional<MeasurementValue> length, area, volume, mass;
    bool operator==(const MeasurementValues&) const = default;
};
struct MeasurementDistance {
    MeasurementValue distance;
    Vec3 first, second;
    bool operator==(const MeasurementDistance&) const = default;
};
struct SavedMeasurement {
    std::string id, name;
    // Informational history row; never a solid-kernel operation.
    std::string body_id, after_object_id;
    std::vector<MeasurementReference> references;
    std::vector<MeasurementValues> values;
    std::optional<MeasurementDistance> distance;
    bool operator==(const SavedMeasurement&) const = default;
};
} // namespace zima::kernel
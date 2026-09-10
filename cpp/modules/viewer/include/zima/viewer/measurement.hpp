#pragma once
#include <zima/kernel/measurement.hpp>
#include <zima/viewer/picking.hpp>

namespace zima::viewer {
struct MeasurementGeometry {
    zima::kernel::MeasurementReference reference;
    zima::kernel::MeasurementValues values;
    std::vector<zima::kernel::Vec3> points;
    std::vector<std::array<zima::kernel::Vec3,2>> segments;
    std::vector<std::array<zima::kernel::Vec3,3>> triangles;
    std::optional<std::pair<zima::kernel::Vec3,zima::kernel::Vec3>> axis, plane;
    bool approximate{}, solid{};
};
[[nodiscard]] std::optional<zima::kernel::MeasurementReference> measurement_reference(const ViewerCandidate&);
[[nodiscard]] std::optional<MeasurementGeometry> measure_entity(
    const zima::kernel::ViewerMesh&, const zima::kernel::MeasurementReference&);
[[nodiscard]] std::optional<zima::kernel::MeasurementDistance> measure_distance(
    const MeasurementGeometry&, const MeasurementGeometry&);
} // namespace zima::viewer
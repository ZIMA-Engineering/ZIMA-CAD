#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/measurement/measurement.hpp>
namespace zima::workspace {
// Current calculated data only: no regeneration, mutation, or kernel invocation.
[[nodiscard]] kernel::ViewerMesh measurement_scene(const Workspace&,const std::string& document);
[[nodiscard]] std::optional<measurement::MeasurementGeometry> resolve_measurement(
    const Workspace&,const std::string& document,const kernel::MeasurementReference&,const kernel::ViewerMesh&);
}

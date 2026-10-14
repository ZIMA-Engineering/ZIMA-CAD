#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/measurement/measurement.hpp>
namespace zima::workspace {
// Current calculated geometry only; no regeneration or document mutation.
[[nodiscard]] kernel::ViewerMesh measurement_scene(const Workspace&,const std::string& document);
[[nodiscard]] std::optional<measurement::MeasurementGeometry> resolve_measurement(
    const Workspace&,const std::string& document,const kernel::MeasurementReference&,const kernel::ViewerMesh&);
// Called only by an explicit measurement/analysis consumer. Complete the exact
// areas of the requested identities from native snapshots, without remeshing.
[[nodiscard]] std::vector<std::optional<double>> measurement_face_areas(
    const Workspace&,const std::string& document,std::span<const kernel::FaceReference>);
}

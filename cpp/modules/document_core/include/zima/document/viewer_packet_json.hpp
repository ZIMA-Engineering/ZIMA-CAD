#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <nlohmann/json_fwd.hpp>

namespace zima::document {
[[nodiscard]] nlohmann::json serialize_surface_geometry(const zima::kernel::SurfaceGeometry& value);
[[nodiscard]] zima::kernel::SurfaceGeometry load_surface_geometry(const nlohmann::json& value);


[[nodiscard]] nlohmann::json serialize_viewer_reference_geometry(
    const zima::kernel::ViewerReferenceGeometry& geometry);
[[nodiscard]] zima::kernel::ViewerReferenceGeometry
load_viewer_reference_geometry(const nlohmann::json& source);

[[nodiscard]] nlohmann::json serialize_body_result(
    const zima::kernel::BodyResult& result);
[[nodiscard]] zima::kernel::BodyResult load_body_result(
    const nlohmann::json& source);

}  // namespace zima::document

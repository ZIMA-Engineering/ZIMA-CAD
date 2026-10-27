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
    const zima::kernel::BodyResult& result, bool include_histories = true,
    bool include_compound_operands = true,
    const zima::kernel::ViewerReferenceGeometry* reference_geometry = nullptr,
    bool include_measurement_sources = true);
// Assemblies already store operands in their native, shared geometry table.
// Resolve those entries before loading/validating the compound recipe.
[[nodiscard]] zima::kernel::BodyResult load_body_result(
    const nlohmann::json& source,
    std::map<std::string,zima::kernel::BodySnapshot> compound_operands = {});

}  // namespace zima::document

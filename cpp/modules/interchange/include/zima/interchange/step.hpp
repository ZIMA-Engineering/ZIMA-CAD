#pragma once

#include <zima/interchange/planar_face.hpp>

#include <filesystem>
#include <string>
#include <optional>
#include <vector>

namespace zima::kernel { class StepSourceDocument; }

namespace zima::interchange {

// Keep legitimate large product structures available to both preview and
// import. Geometry is frozen once per distinct STEP definition by the
// assembly import path; this bound protects only the lightweight occurrence
// hierarchy from malformed or unreasonably large input.
inline constexpr std::size_t maximum_step_product_nodes = 100000;

struct StepPlanarFace {
    std::string face_key;
    PlanarFaceProfile profile;
};

struct StepPart {
    std::string component_path;
    std::string parent_path;
    std::string definition_id;
    std::string name;
    bool assembly{};
    double x{};
    double y{};
    double z{};
    double rotation_x{};
    double rotation_y{};
    double rotation_z{};
    double global_x{};
    double global_y{};
    double global_z{};
    double global_rotation_x{};
    double global_rotation_y{};
    double global_rotation_z{};
    std::string definition_name;
};

[[nodiscard]] std::vector<StepPart> inspect_step_parts(
    const std::filesystem::path& path,
    std::size_t maximum_parts = maximum_step_product_nodes);
[[nodiscard]] std::vector<StepPart> inspect_step_parts(
    const kernel::StepSourceDocument& source,
    std::size_t maximum_parts = maximum_step_product_nodes);

[[nodiscard]] std::vector<StepPlanarFace> extract_step_planar_faces(
    const std::filesystem::path& path, std::size_t maximum_faces = 1000);
[[nodiscard]] std::optional<StepPlanarFace> extract_step_planar_face(
    const std::filesystem::path& path, const std::string& face_key);

}  // namespace zima::interchange

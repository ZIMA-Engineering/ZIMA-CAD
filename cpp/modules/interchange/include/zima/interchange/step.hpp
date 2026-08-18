#pragma once

#include <zima/interchange/planar_face.hpp>

#include <filesystem>
#include <string>
#include <optional>
#include <vector>

namespace zima::interchange {

struct StepPlanarFace {
    std::string face_key;
    PlanarFaceProfile profile;
};

[[nodiscard]] std::vector<StepPlanarFace> extract_step_planar_faces(
    const std::filesystem::path& path, std::size_t maximum_faces = 1000);
[[nodiscard]] std::optional<StepPlanarFace> extract_step_planar_face(
    const std::filesystem::path& path, const std::string& face_key);

}  // namespace zima::interchange

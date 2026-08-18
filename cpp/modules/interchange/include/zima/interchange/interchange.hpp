#pragma once

#include <filesystem>
#include <string>

namespace zima::interchange {

enum class Format { Unknown, Dxf, Step, Stl, Png, Jpeg };
enum class Direction { Import, Export };
enum class Context { Sketch, Part, Assembly };

[[nodiscard]] Format format_from_path(const std::filesystem::path& path);
[[nodiscard]] bool supports(Format format, Direction direction, Context context);
[[nodiscard]] std::string unsupported_reason(
    Format format, Direction direction, Context context);

}  // namespace zima::interchange

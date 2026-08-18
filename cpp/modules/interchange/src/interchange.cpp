#include <zima/interchange/interchange.hpp>

#include <algorithm>
#include <cctype>

namespace zima::interchange {

Format format_from_path(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".dxf") return Format::Dxf;
    if (extension == ".step" || extension == ".stp") return Format::Step;
    if (extension == ".stl") return Format::Stl;
    if (extension == ".png") return Format::Png;
    if (extension == ".jpg" || extension == ".jpeg") return Format::Jpeg;
    return Format::Unknown;
}

bool supports(Format format, Direction direction, Context context) {
    if (direction == Direction::Import) {
        if (format == Format::Dxf) return true;
        if (format == Format::Step) return context != Context::Sketch;
        return false;
    }
    if (format == Format::Dxf) return context == Context::Sketch;
    if (format == Format::Step || format == Format::Stl) {
        return context != Context::Sketch;
    }
    return format == Format::Png || format == Format::Jpeg;
}

std::string unsupported_reason(Format format, Direction direction, Context context) {
    if (supports(format, direction, context)) return {};
    if (format == Format::Unknown) return "Neznámý formát souboru";
    if (direction == Direction::Import &&
        (format == Format::Stl || format == Format::Png || format == Format::Jpeg)) {
        return "Tento formát je určen pouze pro export";
    }
    if (context == Context::Sketch) return "3D formát nelze použít uvnitř skici";
    if (format == Format::Dxf) return "DXF lze exportovat pouze z aktivní skici";
    return "Formát není v tomto kontextu podporován";
}

}  // namespace zima::interchange

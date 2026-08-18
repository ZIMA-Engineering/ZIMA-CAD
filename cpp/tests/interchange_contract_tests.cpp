#include <zima/interchange/interchange.hpp>

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace zima::interchange;
    try {
        require(format_from_path("profile.DXF") == Format::Dxf &&
                    format_from_path("model.stp") == Format::Step &&
                    format_from_path("view.JPEG") == Format::Jpeg,
                "Interchange extension dispatch is not case insensitive");
        require(supports(Format::Dxf, Direction::Import, Context::Sketch) &&
                    supports(Format::Dxf, Direction::Import, Context::Part) &&
                    !supports(Format::Step, Direction::Import, Context::Sketch) &&
                    supports(Format::Step, Direction::Import, Context::Assembly),
                "Import context contract is invalid");
        require(supports(Format::Dxf, Direction::Export, Context::Sketch) &&
                    !supports(Format::Dxf, Direction::Export, Context::Part) &&
                    supports(Format::Stl, Direction::Export, Context::Part) &&
                    supports(Format::Png, Direction::Export, Context::Assembly),
                "Export context contract is invalid");
        std::cout << "C++ interchange contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

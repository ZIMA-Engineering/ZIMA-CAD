#include <zima/interchange/interchange.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/interchange/planar_face.hpp>

#include <filesystem>
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
        auto source = zima::sketcher::Sketch::create_default();
        const auto segment = source.add_segment(0.0, 0.0, 20.0, 0.0);
        source.segments.back().construction = true;
        static_cast<void>(source.add_circle(5.0, 8.0, 3.0));
        static_cast<void>(source.add_arc(0.0, 0.0, 5.0, 0.0, 0.0, 5.0));
        const auto path = std::filesystem::temp_directory_path() /
            "zima-cad-dxf-roundtrip.dxf";
        export_dxf(path, source);
        auto imported = zima::sketcher::Sketch::create_default();
        const auto result = import_dxf(path, imported);
        std::filesystem::remove(path);
        require(result.source_entities == 3 && result.imported_entities == 3 &&
                    !result.import_block_id.empty() &&
                    imported.import_blocks.size() == 1 &&
                    imported.segments.size() == 1 && imported.circles.size() == 1 &&
                    imported.arcs.size() == 1 && imported.segments.front().construction,
                "DXF geometry did not import as one editable ZIMA block");
        const auto before = imported.points.front();
        imported.transform_import_block(result.import_block_id, 10.0, -2.0, 0.0);
        require(imported.points.front().x == before.x + 10.0 &&
                    imported.points.front().y == before.y - 2.0,
                "DXF block translation did not preserve editable entities");
        const auto restored = zima::sketcher::Sketch::from_serialized(
            imported.serialized());
        require(restored.import_blocks == imported.import_blocks &&
                    restored.segments.front().id == imported.segments.front().id &&
                    segment != restored.segments.front().id,
                "DXF block or stable imported identities did not survive save/load");
        auto dense = zima::sketcher::Sketch::create_default();
        for (int index = 0; index < 101; ++index) {
            static_cast<void>(dense.add_segment(
                static_cast<double>(index), 0.0,
                static_cast<double>(index), 1.0));
        }
        export_dxf(path, dense);
        auto rejected_target = zima::sketcher::Sketch::create_default();
        bool limit_rejected = false;
        try {
            static_cast<void>(import_dxf(path, rejected_target));
        } catch (const std::runtime_error&) {
            limit_rejected = true;
        }
        std::filesystem::remove(path);
        require(limit_rejected && rejected_target.points.empty() &&
                    rejected_target.segments.empty() &&
                    rejected_target.import_blocks.empty(),
                "Oversized DXF was not rejected before mutating the Sketch");
        auto face_sketch = zima::sketcher::Sketch::create_default();
        const auto face_block = append_planar_face_as_sketch_block({
            "STEP profil", "face:front", {
                PlanarLine{{0.0, 0.0}, {20.0, 0.0}},
                PlanarArc{{20.0, 5.0}, {20.0, 0.0}, {25.0, 5.0}},
                PlanarCircle{{10.0, 10.0}, 2.0},
            }}, face_sketch);
        require(!face_block.empty() && face_sketch.import_blocks.size() == 1 &&
                    face_sketch.segments.size() == 1 && face_sketch.arcs.size() == 1 &&
                    face_sketch.circles.size() == 1 &&
                    face_sketch.import_blocks.front().source_path == "face:front",
                "Analytic STEP face profile did not become one normal Sketch block");
        std::cout << "C++ interchange contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

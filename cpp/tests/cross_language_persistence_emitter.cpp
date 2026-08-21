#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/drawing/drawing_document.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: cross_language_persistence_emitter OUTPUT_DIR");
        const std::filesystem::path output = argv[1];
        std::filesystem::create_directories(output);

        auto part = zima::document::PartDocument::create_default();
        part.document_id = "cpp-part-persistence-001";
        part.name = "C++ Persistence Part";
        part.user_parameters = {{"material", "Steel"}};
        part.user_parameter_order = {"material"};
        part.user_parameter_labels["material"] = {{"en", "Material"}};
        part.user_parameter_values["material"] = {{"", "Steel"}};
        part.physical_parameters["MASS_DENSITY"] = "7.85e-6";
        auto sketch = zima::sketcher::Sketch::create_default();
        sketch.id = "cpp-sketch-001";
        sketch.name = "C++ Profile";
        sketch.plane = zima::sketcher::SketchPlane::XZ;
        static_cast<void>(sketch.add_rectangle(0.0, 0.0, 20.0, 10.0));
        static_cast<void>(sketch.add_circle(5.0, 5.0, 2.0));
        part.sketches.push_back(sketch);
        auto feature = zima::document::PartDocument::create_box_container();
        feature.id = "cpp-feature-001";
        feature.feature_id = "cpp-feature-001:feature";
        feature.feature_parent_id = feature.id;
        feature.container_origin =
            zima::document::create_container_origin(feature.id);
        feature.name = "C++ Box";
        feature.box = {20.0, 10.0, 5.0};
        part.history.push_back(feature);
        part.save(output / "cpp_part.prtz");

        auto nested = zima::assembly::AssemblyDocument::create_default();
        nested.document_id = "cpp-nested-assembly-001";
        nested.name = "C++ Nested Assembly";
        zima::assembly::PartOccurrence child;
        child.occurrence_id = "cpp-part-occurrence-001";
        child.name = "Nested C++ Part";
        child.source_document_id = part.document_id;
        child.source_path = "cpp_part.prtz";
        child.source_kind = zima::assembly::ComponentSourceKind::Part;
        child.placement.x = 2.0;
        nested.components.push_back(child);
        nested.save(output / "cpp_nested.asmz");

        auto assembly = zima::assembly::AssemblyDocument::create_default();
        assembly.document_id = "cpp-assembly-001";
        assembly.name = "C++ Nested Assembly Root";
        zima::assembly::PartOccurrence occurrence;
        occurrence.occurrence_id = "cpp-nested-occurrence-001";
        occurrence.name = "Nested Assembly";
        occurrence.source_document_id = nested.document_id;
        occurrence.source_path = "cpp_nested.asmz";
        occurrence.source_kind = zima::assembly::ComponentSourceKind::Assembly;
        zima::assembly::OccurrenceSnapshot nested_part;
        nested_part.occurrence_id = child.occurrence_id;
        nested_part.name = child.name;
        nested_part.source_document_id = child.source_document_id;
        nested_part.source_kind = child.source_kind;
        nested_part.placement.x = child.placement.x;
        occurrence.nested_snapshot.push_back(nested_part);
        assembly.components.push_back(occurrence);
        assembly.save(output / "cpp_assembly.asmz");

        auto drawing = zima::drawing::DrawingDocument::create_default();
        drawing.document_id = "cpp-drawing-001";
        drawing.name = "C++ Drawing";
        require(!drawing.sheets.empty(), "default drawing has no sheet");
        auto& sheet = drawing.sheets.front();
        sheet.id = "cpp-sheet-001";
        sheet.name = "C++ Sheet";
        auto view = zima::drawing::DrawingDocument::create_view(
            part.document_id, "cpp_part.prtz", {}, 
            zima::drawing::ViewOrientation::Front);
        view.id = "cpp-view-001";
        view.name = "C++ Front View";
        sheet.views.push_back(std::move(view));
        drawing.save(output / "cpp_drawing.drwz");

        std::cout << "C++ persistence fixtures emitted to " << output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

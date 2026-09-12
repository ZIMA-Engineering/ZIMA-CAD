#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <functional>
#include <stdexcept>

namespace zima::workspace {
class SketchOperationError : public std::runtime_error {
public:
    SketchOperationError(const char* code,const char* message) : std::runtime_error(message),code(code) {}
    const char* code;
};
using SketchMutation=std::function<void(sketcher::Sketch&)>;
// The caller owns a transient draft. Geometry dependencies and validation are
// shared by GUI drafts and document/CLI edits; this never calculates a body.
void apply_sketch_geometry(sketcher::Sketch&,const SketchMutation&);
// Synchronous borrowed references; embedded profiles live only during callback.
// Return false to stop without deserializing unrelated profiles.
void visit_document_sketches(const Workspace&,const std::string& document,
    const std::function<bool(const sketcher::Sketch&)>&);
[[nodiscard]] sketcher::Sketch document_sketch(const Workspace&,const std::string& document,const std::string& sketch);
// Optional document-level label layouts commit atomically with the Sketch edit.
[[nodiscard]] bool mutate_document_sketch(Workspace&,const std::string& document,const std::string& sketch,const SketchMutation&,
    const std::vector<kernel::DimensionLayoutEntry>& document_layouts={});
// Insert already validated Sketch definitions. No placement values are changed
// here; callers use their existing factory or property dialog values.
void insert_new_sketch(document::PartDocument&,sketcher::Sketch,document::HistoryContainer);
void insert_new_sketch(assembly::AssemblyDocument&,sketcher::Sketch);
[[nodiscard]] std::string create_document_sketch(Workspace&,const kernel::OcctKernel&,const std::string& document,
    std::string name,sketcher::SketchPlane plane);
}

#include <zima/document/body_origin_attachment.hpp>
#include <zima/interchange/model_import.hpp>
#include <zima/document/precision.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <algorithm>
#include <stdexcept>
namespace zima::interchange {
DxfPartImport import_dxf_part(document::PartDocument doc,
        const std::vector<kernel::BodyResult>& previous, const std::filesystem::path& source,
        const std::string& active_sketch_id) {
    auto sketch = sketcher::Sketch::create_default();
    if (!active_sketch_id.empty()) {
        const auto found = std::ranges::find(doc.sketches, active_sketch_id, &sketcher::Sketch::id);
        if (found == doc.sketches.end()) throw std::runtime_error("Aktivní skica pro DXF nebyla nalezena");
        sketch = *found;
    } else sketch.name = source.stem().string();
    auto report = import_dxf(source, sketch);
    if (report.imported_entities == 0) throw std::runtime_error("DXF neobsahuje podporovanou 2D geometrii");
    const auto sketch_id = sketch.id;
    if (active_sketch_id.empty()) {
        auto container = document::PartDocument::create_sketch_container();
        container.name = sketch.name; sketch.owner_container_id = container.id;
        auto graph = doc.body_history;
        if (graph.active_body_id().empty()) static_cast<void>(document::create_origin_bound_body(graph, doc.document_id, source.stem().string()));
        graph.insert({document::PartHistoryKind::Feature, container.id});
        doc.history.push_back(std::move(container)); doc.sketches.push_back(std::move(sketch));
        doc.set_body_history(std::move(graph));
    } else *std::ranges::find(doc.sketches, sketch_id, &sketcher::Sketch::id) = std::move(sketch);
    doc.resolve_constructions();
    // A Sketch has no solid operation. Preserve the calculated history; opening
    // or importing sketch geometry must not recalculate unrelated solids.
    return {{std::move(doc), previous, {}}, std::move(report), sketch_id};
}
StepImportedPart import_iges_part(document::PartDocument doc,
        const std::vector<kernel::BodyResult>& previous, const std::filesystem::path& source, std::optional<double> mesh_deflection) {
    if (mesh_deflection && (!std::isfinite(*mesh_deflection) || *mesh_deflection<=0))
        throw std::invalid_argument("Import mesh deflection must be positive");
    const auto absolute = std::filesystem::absolute(source);
    auto container = document::PartDocument::create_imported_step_container(absolute, {}, source.stem().string());
    container.imported_step.mesh_deflection=mesh_deflection;
    kernel::OcctKernel kernel;
    auto frozen = kernel.import_iges(absolute.string(), container.id,
        mesh_deflection.value_or(document::precision_value(doc.document_precision, "mesh_deflection", 0.1)));
    container.imported_step.frozen_brep = std::make_shared<const std::string>(std::move(frozen.kernel_shape));
    container.imported_step.topology = std::move(frozen.imported_step_topology);
    auto graph = doc.body_history;
    static_cast<void>(document::create_origin_bound_body(graph, doc.document_id, source.stem().string()));
    graph.insert({document::PartHistoryKind::Feature, container.id});
    doc.history.push_back(std::move(container)); doc.set_body_history(std::move(graph));
    auto calculated = kernel.evaluate_history_incremental(doc.kernel_operations(), previous);
    return {std::move(doc), std::move(calculated), {}};
}
}

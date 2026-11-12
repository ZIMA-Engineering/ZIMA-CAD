#include <zima/document/file_path.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <zima/interchange/model_import.hpp>
#include <zima/document/precision.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <algorithm>
#include <stdexcept>
namespace zima::interchange {
namespace {
void apply_imported_appearance(document::PartDocument& document,
        const std::string& body_id,kernel::Appearance appearance) {
    if(!appearance.body.color.empty())
        document.appearance.bodies[body_id]=appearance.body;
    for(auto& group:appearance.groups) {
        group.body_id=body_id;
        document.appearance.groups.push_back(std::move(group));
    }
}
}
DxfPartImport import_dxf_part(document::PartDocument doc,
        const std::vector<kernel::BodyResult>& previous, const std::filesystem::path& source,
        const std::string& active_sketch_id, double ambiguous_unit_scale_to_mm,
        std::size_t maximum_entities) {
    auto sketch = sketcher::Sketch::create_default();
    if (!active_sketch_id.empty()) {
        const auto found = std::ranges::find(doc.sketches, active_sketch_id, &sketcher::Sketch::id);
        if (found == doc.sketches.end()) throw std::runtime_error("Aktivní skica pro DXF nebyla nalezena");
        sketch = *found;
    } else sketch.name = document::path_to_utf8(source.stem());
    auto report = import_dxf(source, sketch, ambiguous_unit_scale_to_mm, maximum_entities);
    if (report.imported_entities == 0) throw std::runtime_error("DXF neobsahuje podporovanou 2D geometrii");
    const auto sketch_id = sketch.id;
    if (active_sketch_id.empty()) {
        auto container = document::PartDocument::create_sketch_container();
        container.name = sketch.name; sketch.owner_container_id = container.id;
        auto graph = doc.body_history;
        if (graph.active_body_id().empty()) static_cast<void>(document::create_origin_bound_body(graph, doc.document_id, document::path_to_utf8(source.stem())));
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
    auto container = document::PartDocument::create_imported_step_container(absolute, {}, document::path_to_utf8(source.stem()));
    container.imported_step.mesh_deflection=mesh_deflection;
    kernel::OcctKernel kernel;
    auto imported = kernel.import_iges_model(document::path_to_utf8(absolute), container.id,
        mesh_deflection.value_or(document::precision_value(doc.document_precision, "mesh_deflection", 0.1)));
    container.imported_step.frozen_brep = std::make_shared<const std::string>(
        std::move(imported.body.kernel_shape));
    container.imported_step.topology =
        std::move(imported.body.imported_step_topology);
    auto graph = doc.body_history;
    const auto body_id=document::create_origin_bound_body(graph, doc.document_id, document::path_to_utf8(source.stem()));
    graph.insert({document::PartHistoryKind::Feature, container.id});
    graph.move_body(body_id,0);
    doc.history.push_back(std::move(container)); doc.set_body_history(std::move(graph));
    apply_imported_appearance(doc,body_id,std::move(imported.appearance));
    auto calculated = kernel.evaluate_history_incremental(doc.kernel_operations(), previous);
    return {std::move(doc), std::move(calculated), {}};
}

StepImportedPart import_stl_part(document::PartDocument doc,
        const std::vector<kernel::BodyResult>& previous,
        const std::filesystem::path& source,
        std::optional<double> mesh_deflection) {
    if(mesh_deflection&&(!std::isfinite(*mesh_deflection)||
            *mesh_deflection<=0))
        throw std::invalid_argument(
            "Import mesh deflection must be positive");
    const auto absolute=std::filesystem::absolute(source);
    auto container=document::PartDocument::create_imported_step_container(
        absolute,{},document::path_to_utf8(source.stem()));
    container.imported_step.mesh_deflection=mesh_deflection;
    kernel::OcctKernel kernel;
    auto imported=kernel.import_stl_model(document::path_to_utf8(absolute),
        container.id,mesh_deflection.value_or(document::precision_value(
            doc.document_precision,"mesh_deflection",0.1)));
    container.imported_step.frozen_brep=std::make_shared<const std::string>(
        std::move(imported.body.kernel_shape));
    container.imported_step.topology=
        std::move(imported.body.imported_step_topology);
    auto graph=doc.body_history;
    const auto body_id=document::create_origin_bound_body(graph,
        doc.document_id,document::path_to_utf8(source.stem()));
    graph.insert({document::PartHistoryKind::Feature,container.id});
    graph.move_body(body_id,0);
    doc.history.push_back(std::move(container));
    doc.set_body_history(std::move(graph));
    apply_imported_appearance(doc,body_id,std::move(imported.appearance));
    auto calculated=kernel.evaluate_history_incremental(
        doc.kernel_operations(),previous);
    return {std::move(doc),std::move(calculated),{}};
}
}

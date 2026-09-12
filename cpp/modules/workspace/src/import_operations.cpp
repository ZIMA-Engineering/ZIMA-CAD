#include <zima/workspace/import_operations.hpp>
#include <zima/interchange/interchange.hpp>
#include <algorithm>
#include <set>

namespace zima::workspace {
PartImportReport import_part(Workspace& live, const std::string& document_id,
    const std::filesystem::path& source, const PartImportOptions& options,
    const std::function<void(std::function<void()>)>& runner) {
    const auto* part = live.open_part(document_id);
    if (!part) throw ImportOperationError("unsupported_document", "This import requires an open Part.");
    const auto format = interchange::format_from_path(source);
    if (format != interchange::Format::Dxf && format != interchange::Format::Step && format != interchange::Format::Iges)
        throw ImportOperationError("unsupported_format", "Part import supports STEP, IGES and DXF.");
    if (!std::filesystem::is_regular_file(source))
        throw ImportOperationError("file_not_found", "The import source file does not exist.");
    if (!options.sketch_id.empty() && format != interchange::Format::Dxf)
        throw ImportOperationError("invalid_arguments", "Only DXF can be imported into a Sketch.");
    const auto& before = part->session.document();
    if (format == interchange::Format::Dxf) {
        const auto* body = before.body_history.find(before.body_history.active_body_id());
        if (!options.sketch_id.empty()) {
            const auto sketch = std::ranges::find(before.sketches, options.sketch_id, &sketcher::Sketch::id);
            if (sketch == before.sketches.end())
                throw ImportOperationError("sketch_not_found", "The requested Sketch does not exist.");
            body = before.body_owner_for_object(sketch->owner_container_id);
            if (body && body->scope.id != before.body_history.active_body_id())
                throw ImportOperationError("inactive_body", "Activate the owning Body before editing its history.");
        }
        if (body && body->derived_copy)
            throw ImportOperationError("read_only_body", "A derived Body cannot be edited directly.");
    }
    const auto generation = part->session.data_generation();
    const auto revision = part->session.revision();
    const auto identity = part->runtime_identity;
    const auto active_body = before.body_history.active_body_id();
    std::set<std::string> old_bodies, old_containers;
    for (const auto& body : before.body_history.bodies()) old_bodies.insert(body.scope.id);
    for (const auto& container : before.history) old_containers.insert(container.id);
    std::optional<interchange::StepImportedPart> imported;
    PartImportReport report;
    std::function<void()> task = [document=before, previous=part->session.calculated_boundaries(),
                       source=std::filesystem::absolute(source), options, format, &imported, &report]() mutable {
        if (format == interchange::Format::Dxf) {
            auto result = interchange::import_dxf_part(std::move(document), previous, source,
                options.sketch_id, options.unitless_scale_mm, options.maximum_entities);
            report.dxf = std::move(result.report); report.sketch_id = std::move(result.sketch_id);
            imported = std::move(result.part);
        } else if (format == interchange::Format::Step) {
            imported = interchange::import_step_part(std::move(document), previous, source, options.mesh_deflection);
            report.body_calculated = true;
        } else {
            imported = interchange::import_iges_part(std::move(document), previous, source, options.mesh_deflection);
            report.body_calculated = true;
        }
    };
    if (runner) runner(std::move(task)); else task();
    if (!imported) throw ImportOperationError("incomplete_import", "The import runner did not complete the calculation.");
    for (const auto& body : imported->document.body_history.bodies())
        if (!old_bodies.contains(body.scope.id)) report.body_ids.push_back(body.scope.id);
    for (const auto& container : imported->document.history)
        if (!old_containers.contains(container.id)) report.container_ids.push_back(container.id);
    auto* target = live.open_part(document_id);
    if (!target || target->runtime_identity != identity || target->session.revision() != revision ||
        target->session.data_generation() != generation || target->session.document().body_history.active_body_id() != active_body)
        throw ImportOperationError("document_changed", "The target Part changed while importing; its current data was preserved.");
    if (imported->document.document_id != document_id)
        throw ImportOperationError("identity_changed", "Import cannot replace the target document identity.");
    target->session.commit(std::move(imported->document), std::move(imported->calculated));
    return report;
}
}

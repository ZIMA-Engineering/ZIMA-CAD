#include <zima/workspace/import_operations.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <algorithm>
#include <set>

namespace zima::workspace {
PreparedSketchImport prepare_sketch_dxf(const sketcher::Sketch& before,const std::filesystem::path& source,double scale,std::size_t maximum) {
    if(interchange::format_from_path(source)!=interchange::Format::Dxf)throw ImportOperationError("unsupported_format","Only DXF can be imported into a Sketch.");
    if(!std::filesystem::is_regular_file(source))throw ImportOperationError("file_not_found","The import source file does not exist.");
    PreparedSketchImport result;result.sketch=before;
    apply_sketch_geometry(result.sketch,[&](auto& draft){result.report=interchange::import_dxf(source,draft,scale,maximum);});
    if(!result.report.imported_entities)throw std::runtime_error("DXF neobsahuje podporovanou 2D geometrii");
    return result;
}
PartImportReport import_sketch(Workspace& live,const std::string& id,const std::filesystem::path& source,
    const PartImportOptions& options,const std::function<void(std::function<void()>)>& runner) {
    const auto* part=live.open_part(id);const auto* assembly=live.open_assembly(id);
    if(!part&&!assembly)throw SketchOperationError("unsupported_document","Sketch operations require an open Part or Assembly.");
    const auto input=document_sketch(live,id,options.sketch_id);
    const auto identity=part?part->runtime_identity:assembly->runtime_identity;
    const auto revision=part?part->session.revision():assembly->session.revision();
    const auto generation=part?part->session.data_generation():assembly->session.data_generation();
    const auto active_body=part?part->session.document().body_history.active_body_id():std::string{};
    std::optional<PreparedSketchImport> result;
    const auto task=[input,source=std::filesystem::absolute(source),options,&result]{result=prepare_sketch_dxf(input,source,options.unitless_scale_mm,options.maximum_entities);};
    if(runner)runner(task);else task();
    if(!result)throw ImportOperationError("incomplete_import","The import runner did not complete the calculation.");
    const auto* current_part=live.open_part(id);const auto* current_assembly=live.open_assembly(id);
    const bool unchanged=part?(current_part&&current_part->runtime_identity==identity&&current_part->session.revision()==revision&&current_part->session.data_generation()==generation&&current_part->session.document().body_history.active_body_id()==active_body)
        :(current_assembly&&current_assembly->runtime_identity==identity&&current_assembly->session.revision()==revision&&current_assembly->session.data_generation()==generation);
    if(!unchanged)throw ImportOperationError("document_changed","The target document changed while importing the Sketch; its current data was preserved.");
    static_cast<void>(mutate_document_sketch(live,id,options.sketch_id,[&](auto& draft){draft=std::move(result->sketch);}));
    PartImportReport report;report.sketch_id=options.sketch_id;report.dxf=std::move(result->report);return report;
}
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
    if(format==interchange::Format::Dxf&&!options.sketch_id.empty())return import_sketch(live,document_id,source,options,runner);
    const auto& before = part->session.document();
    if (format == interchange::Format::Dxf) {
        const auto* body = before.body_history.find(before.body_history.active_body_id());
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

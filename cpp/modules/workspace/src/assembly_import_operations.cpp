#include <zima/workspace/assembly_import_operations.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/document/file_path.hpp>
#include <zima/document/precision.hpp>
#include <set>
#include <type_traits>

namespace zima::workspace {
namespace {
struct ImportDirectory {
    std::filesystem::path path;
    std::vector<std::filesystem::path> files;
    bool keep{};
    bool created{};
    ImportDirectory(const std::filesystem::path& requested,const std::filesystem::path& base) {
        if(!requested.empty()) {
            path=std::filesystem::absolute(requested).lexically_normal();
            if(!std::filesystem::is_directory(path.parent_path()))
                throw ImportOperationError("invalid_directory","The import destination parent directory does not exist.");
            if(!std::filesystem::create_directory(path))
                throw ImportOperationError("file_exists","Assembly import requires a new destination directory.");
            created=true;
        } else {
            if(!std::filesystem::is_directory(base))
                throw ImportOperationError("invalid_directory","The import destination parent directory does not exist.");
            path=std::filesystem::absolute(base).lexically_normal();
        }
    }
    ImportDirectory(const ImportDirectory&)=delete;
    ~ImportDirectory() {
        if(keep)return;
        std::error_code error;
        for(const auto& file:files)std::filesystem::remove(file,error);
        if(created)std::filesystem::remove(path,error); // Never remove a pre-existing directory.
    }
    void track(const std::filesystem::path& file) {
        if(file.lexically_normal().parent_path()!=path)
            throw ImportOperationError("invalid_destination","An imported native file escaped its destination directory.");
        files.push_back(file);
        auto temporary=file;temporary += ".tmp";files.push_back(std::move(temporary));
    }
};
}
AssemblyImportReport import_assembly(Workspace& live,const std::string& owner,
    const std::filesystem::path& source,const AssemblyImportOptions& options,
    const std::function<void(std::function<void()>)>& runner) {
    const auto* target=live.open_assembly(owner);
    if(!target)throw ImportOperationError("unsupported_document","This import requires an open Assembly.");
    const auto format=interchange::format_from_path(source);
    if(format!=interchange::Format::Step && format!=interchange::Format::Iges && format!=interchange::Format::Dxf)
        throw ImportOperationError("unsupported_format","Assembly import supports STEP, IGES and DXF.");
    if(!std::filesystem::is_regular_file(source))
        throw ImportOperationError("file_not_found","The import source file does not exist.");
    if(!options.geometry.sketch_id.empty())
        throw ImportOperationError("invalid_arguments","Assembly file import creates a new component; it cannot append to an existing Sketch.");
    const auto before=target->session.document();
    const auto identity=target->runtime_identity;
    const auto revision=target->session.revision(),generation=target->session.data_generation();
    const auto unchanged=[&]() {
        const auto* current=live.open_assembly(owner);
        if(!current || current->runtime_identity!=identity || current->session.revision()!=revision ||
           current->session.data_generation()!=generation)
            throw ImportOperationError("document_changed","The target Assembly changed while importing; its current data was preserved.");
    };
    const auto base=options.working_directory;
    ImportDirectory directory(options.output_directory,base.empty()?std::filesystem::current_path():base);
    AssemblyImportReport report;report.directory=directory.path;
    std::optional<interchange::StepAssemblyImport> imported;
    std::function<void()> calculate=[&,source=std::filesystem::absolute(source),precision=before.document_precision,
                                     units=before.document_units,geometry=options.geometry,templates=options.templates] {
        const double cut_tolerance=templates?templates->sheet_cut_tolerance:.05;
        document::validate_sheet_cut_tolerance(cut_tolerance);
        char tolerance[64];const auto encoded=std::to_chars(tolerance,tolerance+sizeof(tolerance),cut_tolerance);
        if(encoded.ec!=std::errc{})throw std::invalid_argument("Cannot encode Sheet Cut tolerance.");
        const std::string cut_tolerance_text(tolerance,encoded.ptr);
        if(format==interchange::Format::Step) {
            imported=interchange::import_step_assembly(source,directory.path,precision,geometry.mesh_deflection);
            for(auto& part:imported->parts)part.document.document_units=units;
            for(auto& assembly:imported->assemblies)assembly.document.document_units=units;
        } else {
            auto doc=templates?part_from_template(*templates):document::PartDocument::create_default();
            doc.name=document::path_to_utf8(source.stem());doc.document_precision=precision;doc.document_units=units;
            interchange::StepImportedPart part;
            if(format==interchange::Format::Iges)part=interchange::import_iges_part(std::move(doc),{},source,geometry.mesh_deflection);
            else {
                auto result=interchange::import_dxf_part(std::move(doc),{},source,{},geometry.unitless_scale_mm,geometry.maximum_entities);
                part=std::move(result.part);report.dxf=std::move(result.report);report.sketch_id=std::move(result.sketch_id);
            }
            part.path=directory.path/"part-1.prtz";
            imported.emplace();imported->parts.push_back(std::move(part));
        }
        // Imported definitions are new Parts; retain inherited precision while
        // snapshotting the configured Part-only calculation default.
        for(auto& part:imported->parts)
            part.document.document_precision["sheet_cut_tolerance"]=cut_tolerance_text;
    };
    if(runner)runner(std::move(calculate));else calculate();
    if(!imported)throw ImportOperationError("incomplete_import","The import runner did not complete the calculation.");
    unchanged();
    // Remap every definition before insertion so all dependency links point to
    // collision-free native files in the selected working directory.
    if(options.output_directory.empty()) {
        std::map<std::string,std::filesystem::path> paths;
        std::set<std::filesystem::path> reserved;
        const auto allocate=[&](const std::string& id,const std::filesystem::path& original) {
            const auto stem=document::path_to_utf8(source.stem())+"_"+document::path_to_utf8(original.stem());
            for(std::size_t suffix=0;;++suffix) {
                auto candidate=directory.path/std::filesystem::u8path(stem+(suffix?"_"+std::to_string(suffix):"")+document::path_to_utf8(original.extension()));
                auto temporary=candidate;temporary += ".tmp";
                if(std::filesystem::exists(candidate)||std::filesystem::exists(temporary)||live.document_id_for_path(candidate)||reserved.contains(candidate))continue;
                reserved.insert(candidate);paths.emplace(id,candidate);return candidate;
            }
        };
        for(auto& part:imported->parts)part.path=allocate(part.document.document_id,part.path);
        for(auto& assembly:imported->assemblies)assembly.path=allocate(assembly.document.document_id,assembly.path);
        for(auto& assembly:imported->assemblies)for(auto& component:assembly.document.components)
            component.source_path=paths.at(component.source_document_id);
        if(format==interchange::Format::Step)
            imported->root_occurrence.source_path=paths.at(imported->root_occurrence.source_document_id);
    }
    // Build the complete insertion privately through existing model contracts.
    Workspace prepared;prepared.add_assembly(before);
    std::set<std::string> ids{owner};
    const auto check_source=[&](const std::string& id,const std::filesystem::path& path) {
        if(id.empty() || !ids.insert(id).second || live.find(id) || live.document_id_for_path(path))
            throw ImportOperationError("identity_conflict","An imported source conflicts with an open document.");
        directory.track(path);report.files.push_back(path);
    };
    for(auto& part:imported->parts) {
        check_source(part.document.document_id,part.path);report.part_ids.push_back(part.document.document_id);
        prepared.add_part(std::move(part.document),std::move(part.calculated),part.path);
        prepared.open_part(report.part_ids.back())->background_import_source=true;
    }
    for(auto& assembly:imported->assemblies) {
        check_source(assembly.document.document_id,assembly.path);report.assembly_ids.push_back(assembly.document.document_id);
        prepared.add_assembly(std::move(assembly.document),assembly.path);
        prepared.open_assembly(report.assembly_ids.back())->background_import_source=true;
    }
    if(format==interchange::Format::Step) {
        auto next=before;report.occurrence_id=imported->root_occurrence.occurrence_id;
        report.source_document_id=imported->root_occurrence.source_document_id;
        next.components.push_back(std::move(imported->root_occurrence));
        static_cast<void>(next.build_scene());prepared.open_assembly(owner)->session.commit(std::move(next));
    } else {
        report.source_document_id=report.part_ids.at(0);
        report.occurrence_id=prepared.insert_open_part(owner,report.source_document_id,prepared.open_part(report.source_document_id)->session.document().name);
    }
    std::function<void()> save=[&] {
        for(const auto& id:report.part_ids) {
            const auto& part=*prepared.open_part(id);part.session.document().save(part.path,part.session.calculated_boundaries());
        }
        for(const auto& id:report.assembly_ids) {
            const auto& assembly=*prepared.open_assembly(id);assembly.session.document().save(assembly.path);
        }
    };
    bool saved=false;std::function<void()> write=[&]{save();saved=true;};
    if(runner)runner(std::move(write));else write();
    if(!saved)throw ImportOperationError("incomplete_import","The import runner did not finish writing the native documents.");
    unchanged();
    // Prepare the owner's Undo step before touching the live document list.
    auto committed=live.open_assembly(owner)->session;
    committed.commit(prepared.open_assembly(owner)->session.document());
    for(const auto& path:report.files)if(live.document_id_for_path(path))
        throw ImportOperationError("identity_conflict","An imported source conflicts with an open document.");
    for(const auto& id:ids)if(id!=owner && live.find(id))
        throw ImportOperationError("identity_conflict","An imported source conflicts with an open document.");
    auto& documents=live.documents();documents.reserve(documents.size()+prepared.size()-1);
    const auto previous_size=documents.size();
    static_assert(std::is_nothrow_move_assignable_v<assembly::AssemblySession>);
    try { for(auto& state:prepared.documents()) {
        const auto& id=std::visit([](const auto& value)->const std::string& {
            if constexpr(std::is_same_v<std::decay_t<decltype(value)>,DrawingState>)return value.document().document_id;
            else return value.session.document().document_id;
        },state);
        if(id!=owner)documents.push_back(std::move(state));
    }} catch(...) {
        while(documents.size()>previous_size)documents.pop_back();
        throw;
    }
    live.open_assembly(owner)->session=std::move(committed);
    directory.keep=true;
    return report;
}
}

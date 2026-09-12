#include <zima/workspace/export_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/interchange/step_model.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/document/file_path.hpp>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace zima::workspace {
namespace {
bool path_exists(const std::filesystem::path& path) {
    return std::filesystem::exists(std::filesystem::symlink_status(path));
}
bool require_stl_snapshot(const kernel::BodySnapshot& source,
        const std::vector<assembly::OccurrenceSnapshot>& children,std::size_t depth=0) {
    if(depth>256)throw ExportOperationError("dependency_limit","The component dependency graph is too large or too deep.");
    // A calculated Assembly cut or derived copy already owns its final body.
    // Its uncut child snapshots must not replace that result during export.
    if(!source->kernel_shape.empty())return true;
    if(children.empty())throw ExportOperationError("calculation_required","An exported component has no calculated body; invoke Regenerate first.");
    bool visible=false;
    for(const auto& child:children) {
        if(!child.visible||child.manually_suppressed||child.dependency_suppressed)continue;
        const auto found=source->body_outputs.find(child.occurrence_id);
        if(found==source->body_outputs.end())throw ExportOperationError("calculation_required","An exported component has no calculated body; invoke Regenerate first.");
        visible=require_stl_snapshot(found->second,child.children,depth+1)||visible;
    }
    return visible;
}
struct StagedFile {
    std::filesystem::path directory, file;
    explicit StagedFile(const std::filesystem::path& target) {
        directory=target.parent_path()/(".zima-export-"+document::PartDocument::create_default().document_id);
        if(!std::filesystem::create_directory(directory))throw ExportOperationError("staging_failed", "Cannot create the export staging directory.");
        file=directory/("export"+target.extension().string());
    }
    ~StagedFile(){std::error_code error;std::filesystem::remove(file,error);std::filesystem::remove(directory,error);}
};
void publish(const std::filesystem::path& source,const std::filesystem::path& target,bool overwrite) {
#ifdef _WIN32
    if(!MoveFileExW(source.c_str(),target.c_str(),overwrite?MOVEFILE_REPLACE_EXISTING:0))
        throw std::filesystem::filesystem_error("Cannot publish export",source,target,
            std::error_code(static_cast<int>(GetLastError()),std::system_category()));
#else
    if(overwrite)std::filesystem::rename(source,target);
    else {
        // Same-filesystem hard-link creation publishes the completed bytes and
        // atomically refuses an existing name, including a concurrent writer.
        if(::link(source.c_str(),target.c_str())!=0)
            throw std::filesystem::filesystem_error("Cannot publish export",source,target,std::error_code(errno,std::generic_category()));
        std::error_code ignored;std::filesystem::remove(source,ignored);
    }
#endif
}
}
std::uint64_t write_export_file(const std::filesystem::path& destination,bool overwrite,const std::function<void(const std::filesystem::path&)>& writer) {
    const auto target=std::filesystem::absolute(destination).lexically_normal();
    if(!std::filesystem::is_directory(target.parent_path()))throw ExportOperationError("invalid_directory","The export destination directory does not exist.");
    if(path_exists(target)&&(!overwrite||!std::filesystem::is_regular_file(target)))throw ExportOperationError("file_exists","The export destination already exists; request overwrite explicitly.");
    StagedFile stage(target);writer(stage.file);
    const auto bytes=std::filesystem::file_size(stage.file);
    if(!bytes)throw ExportOperationError("empty_export","The exporter produced an empty file.");
    publish(stage.file,target,overwrite);return bytes;
}
ExportReport export_file(const Workspace& live,const std::string& document_id,
    const std::filesystem::path& destination,const ExportOptions& options,
    const std::function<void(std::function<void()>)>& runner) {
    const auto target=std::filesystem::absolute(destination).lexically_normal();
    const auto format=interchange::format_from_path(target);
    if(format!=interchange::Format::Step && format!=interchange::Format::Stl && format!=interchange::Format::Dxf)
        throw ExportOperationError("unsupported_format", "Model export supports STEP, STL and DXF.");
    if(!std::filesystem::is_directory(target.parent_path()))
        throw ExportOperationError("invalid_directory", "The export destination directory does not exist.");
    if(path_exists(target) && (!options.overwrite || !std::filesystem::is_regular_file(target)))
        throw ExportOperationError("file_exists", "The export destination already exists; request overwrite explicitly.");
    if(format!=interchange::Format::Dxf && !options.sketch_id.empty())
        throw ExportOperationError("invalid_arguments", "A Sketch target is supported only for DXF export.");
    const auto* part=live.open_part(document_id);const auto* assembly=live.open_assembly(document_id);
    if(!part && !assembly)throw ExportOperationError("unsupported_document", "Model export requires an open Part or Assembly.");
    ExportReport report{document_id,part?part->session.revision():assembly->session.revision(),0,target};
    using Data=std::variant<sketcher::Sketch,kernel::StepProduct,std::vector<kernel::PlacedBody>,std::vector<assembly::PartOccurrence>>;
    Data data;
    if(format==interchange::Format::Dxf) {
        if(options.sketch_id.empty())throw ExportOperationError("sketch_required", "Specify the Sketch to export as DXF.");
        auto sketch=document_sketch(live,document_id,options.sketch_id);
        try{interchange::validate_dxf_export(sketch);}
        catch(const interchange::DxfExportError& error){throw ExportOperationError("unsupported_geometry",error.what());}
        data=std::move(sketch);
    } else if(format==interchange::Format::Step) {
        data=part?interchange::step_product(part->session.document(),part->session.calculated_boundaries())
                 :interchange::step_product(assembly->session.document());
    } else {
        if(part) {
            if(part->session.calculated_boundaries().empty())throw ExportOperationError("calculation_required", "The Part has no calculated body; invoke Regenerate first.");
            data=std::vector<kernel::PlacedBody>{{part->session.calculated_boundaries().back(),{}, {}}};
        } else {
            std::vector<assembly::PartOccurrence> components;
            const auto& doc=assembly->session.document();const auto suppressed=doc.effectively_suppressed_occurrences();
            for(const auto& component:doc.components) {
                if(!component.visible || suppressed.contains(component.occurrence_id))continue;
                if(require_stl_snapshot(component.calculated_source,component.nested_snapshot))components.push_back(component);
            }
            if(components.empty())throw ExportOperationError("empty_geometry", "There are no visible calculated bodies to export.");
            data=std::move(components);
        }
    }
    bool completed=false;
    std::function<void()> task=[data=std::move(data),target,overwrite=options.overwrite,&report,&completed] {
        report.bytes=write_export_file(target,overwrite,[&](const auto& file) {
        if(const auto* sketch=std::get_if<sketcher::Sketch>(&data))interchange::export_dxf(file,*sketch);
        else {
            kernel::OcctKernel kernel;
            if(const auto* product=std::get_if<kernel::StepProduct>(&data))kernel.export_step(*product,document::path_to_utf8(file));
            else if(const auto* part_bodies=std::get_if<std::vector<kernel::PlacedBody>>(&data))kernel.export_stl(*part_bodies,document::path_to_utf8(file));
            else {
                std::vector<kernel::PlacedBody> bodies;
                for(const auto& component:std::get<std::vector<assembly::PartOccurrence>>(data)) {
                    // Materialize only the captured, already calculated hierarchy,
                    // on the export worker. This never reads or regenerates sources.
                    const auto& p=component.placement;
                    bodies.push_back({assembly::calculate_component_body(component,kernel),
                        {p.x,p.y,p.z},{p.rotation_x,p.rotation_y,p.rotation_z}});
                }
                kernel.export_stl(bodies,document::path_to_utf8(file));
            }
        }
        });completed=true;
    };
    if(runner)runner(std::move(task));else task();
    if(!completed)throw ExportOperationError("incomplete_export", "The export runner did not finish writing the file.");
    return report;
}
}

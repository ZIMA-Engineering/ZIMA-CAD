#include "sketch_command_support.hpp"
#include <zima/workspace/import_operations.hpp>
#include <zima/workspace/assembly_import_operations.hpp>
#include <zima/document/file_path.hpp>
#include <zima/interchange/interchange.hpp>

namespace zima::command_host {
void Host::register_import_commands() {
    using Type=commands::ArgumentType;
    for (const auto format : {interchange::Format::Step, interchange::Format::Iges, interchange::Format::Dxf}) {
        const bool dxf=format==interchange::Format::Dxf;
        std::vector<commands::Argument> args={{"path",true}};
        if (dxf) {
            args.push_back({"sketch",false});args.push_back({"unitless_scale_mm",false,Type::Number});
            args.push_back({"maximum_entities",false,Type::Integer});
        } else args.push_back({"mesh_deflection_mm",false,Type::Number});
        args.push_back({"output_directory",false});args.push_back({"document",false});
        dispatcher_.add({dxf?"import.dxf":format==interchange::Format::Step?"import.step":"import.iges",
            tr("Import a source file into the active Part or Assembly through the shared model transaction."),args,true},
            [this,format,dxf](const Json& a) {
                const auto check=target(a);if(!check.ok)return check;
                if(interaction().template_document)return Result::failure("unsupported_document",tr("This import requires an ordinary Part or Assembly."));
                try {
                    auto source=std::filesystem::u8path(a["path"].get<std::string>());
                    if(source.is_relative())source=directory_/source;source=std::filesystem::absolute(source).lexically_normal();
                    if(interchange::format_from_path(source)!=format)
                        return Result::failure("unsupported_format",tr("The source extension does not match the requested import format."));
                    workspace::PartImportOptions options;
                    if(a.contains("mesh_deflection_mm"))options.mesh_deflection=sketch_commands::number(a["mesh_deflection_mm"]);
                    if(dxf) {
                        options.sketch_id=a.value("sketch",std::string{});
                        options.unitless_scale_mm=a.value("unitless_scale_mm",1.0);
                        options.maximum_entities=sketch_commands::integer(a,"maximum_entities",100000,1,1000000);
                    }
                    const auto doc=workspace_.active_document_id();
                    if(options_.progress)options_.progress(Activity::Read,source);
                    if(workspace_.open_assembly(doc)) {
                        workspace::AssemblyImportOptions settings;settings.geometry=options;settings.working_directory=directory_;
                        if(options_.settings)settings.templates=options_.settings().templates;
                        if(a.contains("output_directory")) {
                            settings.output_directory=std::filesystem::u8path(a["output_directory"].get<std::string>());
                            if(settings.output_directory.empty())return Result::failure("invalid_arguments",tr("Assembly import requires a new destination directory."));
                            if(settings.output_directory.is_relative())settings.output_directory=directory_/settings.output_directory;
                        }
                        const auto imported=workspace::import_assembly(workspace_,doc,source,settings,[this](auto task){io(std::move(task));});
                        change_=Change{ChangeKind::Model,doc,true};
                        Json files=Json::array();for(const auto& file:imported.files)files.push_back(document::path_to_utf8(file));
                        return Result::success({{"document",doc},{"source",document::path_to_utf8(source)},
                            {"directory",document::path_to_utf8(imported.directory)},{"files",std::move(files)},
                            {"occurrence",imported.occurrence_id},{"source_document",imported.source_document_id},
                            {"parts",imported.part_ids},{"assemblies",imported.assembly_ids},{"sketch",imported.sketch_id},
                            {"source_entities",imported.dxf.source_entities},{"imported_entities",imported.dxf.imported_entities},
                            {"warnings",imported.dxf.warnings},{"changed",true},{"revision",workspace_.open_assembly(doc)->session.revision()}});
                    }
                    if(a.contains("output_directory"))return Result::failure("invalid_arguments",tr("A destination directory is supported only when importing into an Assembly."));
                    const auto imported=workspace::import_part(workspace_,doc,source,options,[this](auto task){io(std::move(task));});
                    change_=Change{ChangeKind::Model,doc,true};
                    const auto path=source.generic_u8string();
                    return Result::success({{"document",doc},{"source",std::string(path.begin(),path.end())},
                        {"bodies",imported.body_ids},{"containers",imported.container_ids},{"sketch",imported.sketch_id},
                        {"source_entities",imported.dxf.source_entities},{"imported_entities",imported.dxf.imported_entities},
                        {"import_block",imported.dxf.import_block_id},{"warnings",imported.dxf.warnings},
                        {"body_calculated",imported.body_calculated},{"changed",true},
                        {"revision",workspace_.open_part(doc)->session.revision()}});
                } catch(const workspace::ImportOperationError& e){return Result::failure(e.code,tr(e.what()));}
                  catch(const workspace::SketchOperationError& e){return Result::failure(e.code,tr(e.what()));}
                  catch(const std::exception& e){return Result::failure("import_failed",tr(e.what()));}
            });
    }
}
}

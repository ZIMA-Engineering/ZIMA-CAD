#include <zima/command_host/host.hpp>
#include <zima/drawing_render/pdf_export.hpp>
#include <zima/drawing_render/dxf_export.hpp>
#include <zima/workspace/export_operations.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/document/file_path.hpp>

namespace zima::command_host {
void Host::register_export_commands() {
    dispatcher_.add({"export.pdf",tr("Export every Drawing sheet to PDF without regenerating geometry."),{{"path",true},{"overwrite",false,commands::ArgumentType::Boolean},{"document",false}},true},[this](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        const auto id=workspace_.active_document_id();const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("PDF export requires an open Drawing."));
        try {
            auto path=std::filesystem::u8path(args["path"].get<std::string>());if(path.is_relative())path=directory_/path;path=std::filesystem::absolute(path).lexically_normal();
            const auto revision=state->revision();const auto doc=state->document();const auto source=state->path;
            if(options_.progress)options_.progress(Activity::Export,path);
            const auto bytes=drawing_render::export_pdf(doc,path,source,&workspace_,args.value("overwrite",false));
            return Result::success({{"document",id},{"path",document::path_to_utf8(path)},{"source_revision",revision},{"bytes",bytes},{"pages",doc.sheets.size()},{"model_changed",false}});
        }catch(const workspace::ExportOperationError& e){return Result::failure(e.code,tr(e.what()));}
         catch(const std::exception& e){return Result::failure("export_failed",tr(e.what()));}
    });

    for(const auto format:{interchange::Format::Step,interchange::Format::Stl,interchange::Format::Dxf}) {
        const bool dxf=format==interchange::Format::Dxf;
        std::vector<commands::Argument> args={{"path",true}};
        if(dxf)args.push_back({"sketch",false});
        args.push_back({"overwrite",false,commands::ArgumentType::Boolean});args.push_back({"document",false});
        if(dxf)args.push_back({"sheet",false});
        dispatcher_.add({dxf?"export.dxf":format==interchange::Format::Step?"export.step":"export.stl",
            tr("Export the persisted model snapshot without regenerating or modifying the document."),args,true},
            [this,format](const Json& args) {
                const auto check=target(args);if(!check.ok)return check;
                try {
                    auto path=std::filesystem::u8path(args["path"].get<std::string>());
                    if(path.is_relative())path=directory_/path;path=std::filesystem::absolute(path).lexically_normal();
                    if(interchange::format_from_path(path)!=format)
                        return Result::failure("unsupported_format",tr("The destination extension does not match the requested export format."));
                    const auto doc=workspace_.active_document_id();
                    if(format==interchange::Format::Dxf) {
                        if(const auto* drawing=workspace_.open_drawing(doc)) {
                            if(!args.contains("sheet")||args.contains("sketch"))return Result::failure("invalid_arguments",tr("Drawing DXF export requires a sheet ID and does not accept a sketch."));
                            const auto revision=drawing->revision();const auto snapshot=drawing->document();const auto source=drawing->path;const auto sheet=args["sheet"].get<std::string>();
                            if(options_.progress)options_.progress(Activity::Export,path);
                            const auto bytes=drawing_render::export_dxf(snapshot,sheet,path,source,&workspace_,args.value("overwrite",false));
                            return Result::success({{"document",doc},{"path",document::path_to_utf8(path)},{"source_revision",revision},{"bytes",bytes},{"sheet",sheet},{"model_changed",false}});
                        }
                        if(!args.contains("sketch")||args.contains("sheet"))return Result::failure("invalid_arguments",tr("Sketch DXF export requires a sketch ID and does not accept a drawing sheet."));
                    }
                    if(options_.progress)options_.progress(Activity::Export,path);
                    const auto report=workspace::export_file(workspace_,doc,path,
                        {args.value("sketch",std::string{}),args.value("overwrite",false)},[this](auto task){io(std::move(task));});
                    return Result::success({{"document",report.document_id},{"path",document::path_to_utf8(report.path)},
                        {"source_revision",report.revision},{"bytes",report.bytes},{"model_changed",false}});
                } catch(const workspace::ExportOperationError& e){return Result::failure(e.code,tr(e.what()));}
                  catch(const std::exception& e){return Result::failure("export_failed",tr(e.what()));}
            });
    }
}
}

#include <zima/command_host/host.hpp>
#include <zima/workspace/export_operations.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/document/file_path.hpp>

namespace zima::command_host {
void Host::register_export_commands() {
    for(const auto format:{interchange::Format::Step,interchange::Format::Stl,interchange::Format::Dxf}) {
        const bool dxf=format==interchange::Format::Dxf;
        std::vector<commands::Argument> args={{"path",true}};
        if(dxf)args.push_back({"sketch",true});
        args.push_back({"overwrite",false,commands::ArgumentType::Boolean});args.push_back({"document",false});
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

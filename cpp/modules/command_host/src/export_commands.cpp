#include <zima/command_host/host.hpp>
#include <zima/drawing_render/pdf_export.hpp>
#include <zima/drawing_render/dxf_export.hpp>
#include <zima/drawing_render/image_export.hpp>
#include <zima/workspace/export_operations.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/document/file_path.hpp>
#include <algorithm>

namespace zima::command_host {
void Host::register_export_commands() {
    dispatcher_.add({"export.view",tr("Save the current interactive 3D View to PNG or JPEG without regeneration."),
        {{"path",true},{"quality",false,commands::ArgumentType::Integer},{"overwrite",false,commands::ArgumentType::Boolean},{"document",false}},true},[this](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        const auto id=workspace_.active_document_id(),displayed=workspace_.displayed_document_id();
        if(interaction().template_document||(!workspace_.open_part(displayed)&&!workspace_.open_assembly(displayed)))
            return Result::failure("unsupported_document",tr("View image export requires a displayed Part or Assembly."));
        if(!options_.capture_view)return Result::failure("view_unavailable",tr("This host has no model View."));
        try {
            const auto quality=args.value("quality",95.0);
            if(quality<0||quality>100)return Result::failure("invalid_arguments",tr("Invalid image export settings."));
            auto path=std::filesystem::u8path(args.at("path").get<std::string>());
            if(path.is_relative())path=directory_/path;path=std::filesystem::absolute(path).lexically_normal();
            const auto format=interchange::format_from_path(path);
            if(format!=interchange::Format::Png&&format!=interchange::Format::Jpeg)
                return Result::failure("unsupported_format",tr("Image export requires a PNG or JPEG destination."));
            const auto camera=interaction().camera;
            const auto image=options_.capture_view();
            if(image.isNull())return Result::failure("view_unavailable",tr("The current model View could not be captured."));
            if(options_.progress)options_.progress(Activity::Export,path);
            std::uint64_t bytes{};
            io([&,image]{bytes=drawing_render::write_image(image,path,args.value("overwrite",false),static_cast<int>(quality));});
            return Result::success({{"document",id},{"displayed_document",displayed},{"path",document::path_to_utf8(path)},
                {"bytes",bytes},{"width_px",image.width()},{"height_px",image.height()},{"camera",camera},{"model_changed",false}});
        }catch(const workspace::ExportOperationError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("export_failed",tr(error.what()));}
    });

    dispatcher_.add({"export.image",tr("Export a Drawing sheet or paper crop to PNG or JPEG without regeneration."),
        {{"path",true},{"sheet",true},{"dpi",false,commands::ArgumentType::Number},{"crop_mm",false,commands::ArgumentType::Array},
         {"quality",false,commands::ArgumentType::Integer},{"overwrite",false,commands::ArgumentType::Boolean},{"document",false}},true},[this](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        const auto id=workspace_.active_document_id();const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Sheet image export requires an open Drawing."));
        try {
            drawing_render::ImageExportSettings settings;settings.dpi=args.value("dpi",150.0);
            if(args.contains("quality")){const auto quality=args["quality"].get<double>();if(quality<0||quality>100)return Result::failure("invalid_arguments",tr("Invalid image export settings."));settings.quality=static_cast<int>(quality);}
            if(args.contains("crop_mm")){
                const auto& crop=args["crop_mm"];
                if(crop.size()!=4||!std::ranges::all_of(crop,[](const auto& value){return value.is_number();}))return Result::failure("invalid_arguments",tr("Invalid image export settings."));
                settings.crop_mm=QRectF(crop[0].get<double>(),crop[1].get<double>(),crop[2].get<double>(),crop[3].get<double>());
            }
            auto path=std::filesystem::u8path(args["path"].get<std::string>());if(path.is_relative())path=directory_/path;path=std::filesystem::absolute(path).lexically_normal();
            const auto revision=state->revision();const auto doc=state->document();const auto source=state->path;const auto sheet=args["sheet"].get<std::string>();
            if(options_.progress)options_.progress(Activity::Export,path);
            const auto report=drawing_render::export_image(doc,sheet,path,source,&workspace_,settings,args.value("overwrite",false));
            return Result::success({{"document",id},{"sheet",sheet},{"path",document::path_to_utf8(path)},{"source_revision",revision},{"bytes",report.bytes},
                {"width_px",report.width_px},{"height_px",report.height_px},{"dpi",report.dpi},{"model_changed",false}});
        }catch(const workspace::ExportOperationError& e){return Result::failure(e.code,tr(e.what()));}
         catch(const std::exception& e){return Result::failure("export_failed",tr(e.what()));}
    });

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

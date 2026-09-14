#include <zima/workspace/imported_feature_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/placement_json.hpp>
#include <cmath>
#include "sketch_command_support.hpp"
#include <zima/workspace/import_operations.hpp>
#include <zima/workspace/assembly_import_operations.hpp>
#include <zima/document/file_path.hpp>
#include <zima/interchange/interchange.hpp>

namespace zima::command_host {
namespace {
Json imported_details(const workspace::Workspace& live, const std::string& id, const std::string& container) {
    const auto& value = workspace::imported_feature(live, id, container);
    const auto* state = live.open_part(id);
    const auto frame = workspace::read_placement(live, id, container);
    const auto& source = value.imported_step;
    return {{"document", id}, {"container", value.id}, {"feature", value.feature_id},
        {"name", value.name}, {"body", frame.body}, {"combine", value.combine_mode == document::CombineMode::Add ? "add" : "subtract"},
        {"source", source.source_path}, {"component_path", source.component_path},
        {"mesh_deflection_mm", source.mesh_deflection ? Json(*source.mesh_deflection) : Json(nullptr)},
        {"stored_brep_bytes", source.frozen_brep ? source.frozen_brep->size() : 0},
        {"topology_count", source.topology.size()}, {"placement", value.placement},
        {"coordinate_system", frame.coordinate_system}, {"coordinate_owner", frame.coordinate_owner},
        {"length_unit", "mm"}, {"angle_unit", "degrees"}, {"reference_valid", value.placement.reference_valid},
        {"revision", state->session.revision()}};
}
}
void Host::register_import_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"import.get", tr("Read stored imported-feature properties without calculation."),
        {{"container", true}, {"document", false}}, false}, [this](const Json& args) {
        try {
            return Result::success(imported_details(workspace_, args.value("document", workspace_.active_document_id()),
                args.at("container").get<std::string>()));
        } catch (const workspace::ImportOperationError& error) { return Result::failure(error.code, tr(error.what())); }
    });
    dispatcher_.add({"import.set", tr("Edit an imported feature through its shared Properties transaction."),
        {{"container", true}, {"name", false}, {"combine", false}, {"placement", false, Type::Object}, {"document", false}}, true},
        [this](const Json& args) {
        const auto checked = target(args); if (!checked.ok) return checked;
        try {
            if (interaction().template_document)
                throw workspace::ImportOperationError("unsupported_document", "Imported feature properties require an open Part.");
            const auto id = workspace_.active_document_id(), container = args.at("container").get<std::string>();
            auto value = workspace::imported_feature(workspace_, id, container);
            if (!args.contains("name") && !args.contains("combine") && !args.contains("placement"))
                throw workspace::ImportOperationError("invalid_arguments", "Specify at least one property to change.");
            if (args.contains("name")) value.name = args.at("name").get<std::string>();
            if (args.contains("combine")) {
                const auto combine = args.at("combine").get<std::string>();
                if (combine != "add" && combine != "subtract")
                    throw workspace::ImportOperationError("invalid_arguments", "Choose add or subtract.");
                value.combine_mode = combine == "add" ? document::CombineMode::Add : document::CombineMode::Subtract;
            }
            if (args.contains("placement")) {
                const auto& patch = args.at("placement");
                if (patch.empty()) throw workspace::ImportOperationError("invalid_arguments", "Specify at least one placement parameter.");
                const auto geometry = workspace::placement_edit_geometry(workspace_, id, container);
                for (const auto& [key, number] : patch.items()) {
                    if (!number.is_number() || !std::isfinite(number.get<double>()))
                        throw workspace::ImportOperationError("invalid_arguments", "Placement value must be finite.");
                    if (!workspace::assign_placement_dimension(value.placement, geometry, key, number.get<double>()))
                        throw workspace::ImportOperationError("parameter_not_editable", "The placement parameter is unknown, constrained or locked.");
                }
            }
            const bool changed = workspace::commit_imported_feature(workspace_, kernel_, id, std::move(value));
            auto result = imported_details(workspace_, id, container); result["changed"] = changed;
            if (changed) change_ = Change{ChangeKind::Model, id};
            return Result::success(std::move(result));
        } catch (const workspace::ImportOperationError& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const std::exception& error) { return Result::failure("import_rejected", tr(error.what())); }
    });
    dispatcher_.add({"import.reference.set", tr("Assign an original reference through the supported shared placement and feature transactions."),
        {{"container", true}, {"index", true, Type::Integer}, {"reference", true, Type::Object},
         {"offset_mm", false, Type::Number}, {"flip", false, Type::Boolean}, {"derive_orientation", false, Type::Boolean}, {"document", false}}, true},
        [this](const Json& args) {
        const auto checked = target(args); if (!checked.ok) return checked;
        try {
            if (interaction().template_document)
                throw workspace::ImportOperationError("unsupported_document", "Imported feature properties require an open Part.");
            const auto& ref = args.at("reference");
            const auto invalid = [] { throw workspace::ImportOperationError("invalid_arguments", "Specify owner, key and an optional instance_path for the placement reference."); };
            for (const auto& [key, value] : ref.items())
                if ((key != "owner" && key != "key" && key != "instance_path") || !value.is_string()) invalid();
            if (!ref.contains("owner") || !ref.contains("key") || args.at("index") < 0 || args.at("index") > 4) invalid();
            const auto id = workspace_.active_document_id(), container = args.at("container").get<std::string>();
            document::ConstructionReference source;
            source.owner_id = ref.at("owner"); source.semantic_key = ref.at("key");
            source.instance_path = ref.value("instance_path", std::string{});
            source.offset = args.value("offset_mm", 0.0); source.flip = args.value("flip", false);
            const bool changed = workspace::set_imported_feature_reference(workspace_, kernel_, id, container,
                args.at("index").get<std::size_t>(), std::move(source), args.value("derive_orientation", true));
            auto result = imported_details(workspace_, id, container); result["changed"] = changed;
            if (changed) change_ = Change{ChangeKind::Model, id};
            return Result::success(std::move(result));
        } catch (const workspace::ImportOperationError& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const std::exception& error) { return Result::failure("import_rejected", tr(error.what())); }
    });
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
                    if(dxf&&!options.sketch_id.empty()) {
                        if(a.contains("output_directory"))return Result::failure("invalid_arguments",tr("A Sketch import does not create an output directory."));
                        const auto imported=workspace::import_sketch(workspace_,doc,source,options,[this](auto task){io(std::move(task));});
                        change_=Change{ChangeKind::Model,doc,true};
                        const auto* part=workspace_.open_part(doc);
                        return Result::success({{"document",doc},{"source",document::path_to_utf8(source)},{"sketch",imported.sketch_id},
                            {"bodies",Json::array()},{"containers",Json::array()},{"source_entities",imported.dxf.source_entities},{"imported_entities",imported.dxf.imported_entities},
                            {"import_block",imported.dxf.import_block_id},{"warnings",imported.dxf.warnings},{"body_calculated",false},{"changed",true},
                            {"revision",part?part->session.revision():workspace_.open_assembly(doc)->session.revision()}});
                    }
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

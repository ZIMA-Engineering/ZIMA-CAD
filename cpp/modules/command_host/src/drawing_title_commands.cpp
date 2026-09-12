#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_title_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/document/file_path.hpp>
namespace zima::command_host {
void Host::register_drawing_title_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"drawing.bom.list",tr("List stored BOM rows and their exact source documents without loading sources."),{{"sheet",false},{"limit",false,Type::Integer},{"document",false}},false},[this](const Json& args){
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        const auto wanted=args.value("sheet",std::string{});const auto limit=args.value("limit",2000.0);
        if(limit<1||limit>10000)return Result::failure("invalid_arguments",tr("Drawing query limit must be from 1 to 10000."));
        if(!wanted.empty()&&!state->document().find_sheet(wanted))return Result::failure("sheet_not_found",tr("The drawing sheet does not exist."));
        Json rows=Json::array();std::size_t total=0;
        for(const auto& sheet:state->document().sheets)if(wanted.empty()||wanted==sheet.id)for(const auto& row:sheet.bom_rows) {
            ++total;if(rows.size()>=static_cast<std::size_t>(limit))continue;
            rows.push_back({{"sheet",sheet.id},{"row",row.designation},{"item_number",row.item_number},{"quantity",row.quantity},{"component_name",row.name},
                {"source_document",row.source_document_id},{"source_path",document::path_to_utf8(row.source_path)},
                {"file_stem",row.file_stem},{"parameters",row.parameters},{"parameter_values",row.parameter_values},{"parameter_aliases",row.parameter_aliases},{"mass_unit",row.mass_unit}});
        }
        return Result::success({{"document",id},{"revision",state->revision()},{"items",std::move(rows)},{"total",total}});
    });
    for(bool write:{false,true}) {
        std::vector<commands::Argument> arguments={{"sheet",true},{"bom_row",false}};
        if(write){arguments.push_back({"values",true,Type::Object});arguments.push_back({"expected_values",false,Type::Object});}
        arguments.push_back({"document",false});
        dispatcher_.add({write?"drawing.title.set":"drawing.title.get",tr(write?"Edit title block or BOM source parameters with shared GUI validation.":"Read title block fields and writable parameters from the authoritative source."),arguments,write},[this,write](const Json& args){
            if(write){const auto checked=target(args);if(!checked.ok)return checked;}
            const auto id=args.value("document",workspace_.active_document_id());auto* state=workspace_.open_drawing(id);
            if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
            try {
                auto edit=workspace::prepare_drawing_title_edit(state->document(),args["sheet"].get<std::string>(),&workspace_,state->path,args.value("bom_row",std::string{}));
                if(!write) {
                    Json fields=Json::array();
                    for(const auto& field:edit.fields)fields.push_back({{"field",field.id},{"expression",field.expression},{"value",edit.initial_values.at(field.id)},
                        {"writable",workspace::drawing_title_field_writable(field,edit)},{"write_back",field.write_back}});
                    return Result::success({{"document",id},{"revision",state->revision()},{"sheet",edit.sheet},{"bom_row",edit.bom_row},
                        {"source_document",edit.source_document},{"source_path",document::path_to_utf8(edit.source_path)},
                        {"locale",state->document().find_sheet(edit.sheet)->title_block_locale},{"fields",std::move(fields)}});
                }
                std::map<std::string,std::string> changes;
                if(args["values"].size()>4096)throw workspace::DrawingOperationError("invalid_arguments","Invalid title block field values.");
                for(const auto& [field,value]:args["values"].items()) {
                    if(field.empty()||!value.is_string())throw workspace::DrawingOperationError("invalid_arguments","Invalid title block field values.");
                    changes[field]=value.get<std::string>();
                }
                if(args.contains("expected_values"))for(const auto& [field,value]:args["expected_values"].items()) {
                    if(!changes.contains(field)||!value.is_string())throw workspace::DrawingOperationError("invalid_arguments","Invalid title block field values.");
                    edit.initial_values[field]=value.get<std::string>();
                }
                auto next=state->document();const auto result=workspace::edit_drawing_title(next,&workspace_,edit,changes);
                // Opening a closed source can relocate workspace document states.
                state=workspace_.open_drawing(id);
                if(result.changed){state->commit(std::move(next));change_=Change{ChangeKind::Model,id,true};}
                return Result::success({{"document",id},{"revision",state->revision()},{"changed",result.changed},{"source_changed",result.source_changed},{"source_document",result.source_document}});
            }catch(const workspace::DrawingOperationError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("drawing_failed",tr(error.what()));}
        });
    }
}
}

#include <zima/command_host/host.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include "metadata_json.hpp"
#include <zima/document/material_library.hpp>
#include <zima/document/file_path.hpp>
namespace zima::command_host {
namespace {
using metadata_json::fields;
using metadata_json::strings;
Json relation_data(const workspace::RelationData& data) {
    Json rows=Json::array();for(const auto& row:data.relations)rows.push_back({{"target",row.target},{"expression",row.expression}});
    return {{"relations",std::move(rows)},{"parameters",data.parameters},{"model_values",data.model_values},{"decimal_places",data.decimal_places}};
}
Json material_data(const document::MaterialData& data) {
    Json rows=Json::array(),choices=Json::object();
    for(const auto& [key,value]:data.properties) {
        const auto unit=data.units.find(key);const auto description=data.descriptions.find(key);
        rows.push_back({{"key",key},{"value",value},{"unit",unit==data.units.end()?"":unit->second},
            {"descriptions",description==data.descriptions.end()?Json::object():Json(description->second)}});
        choices[key]=document::material_unit_choices(key);
    }
    return {{"properties",std::move(rows)},{"unit_choices",std::move(choices)}};
}
}
void Host::register_engineering_metadata_commands() {
    using Type=commands::ArgumentType;
    const auto add=[this](commands::Command command,std::function<Json(const std::string&,const Json&)> action) {
        const auto changes=command.changes_state;
        dispatcher_.add(std::move(command),[this,changes,action](const Json& args) {
            if(changes){const auto check=target(args);if(!check.ok)return check;}
            const auto id=args.value("document",workspace_.active_document_id());
            if(!workspace_.open_part(id) && !workspace_.open_assembly(id))return Result::failure("unsupported_document",tr("Document metadata requires an open Part or Assembly."));
            try {
                auto result=action(id,args);result["document"]=id;
                result["revision"]=workspace_.open_part(id)?workspace_.open_part(id)->session.revision():workspace_.open_assembly(id)->session.revision();
                if(changes && result.value("changed",false))change_=Change{ChangeKind::Metadata,id,false};
                return Result::success(std::move(result));
            }catch(const std::invalid_argument& e){return Result::failure("invalid_arguments",tr(e.what()));}
             catch(const Json::exception& e){return Result::failure("invalid_arguments",tr("Invalid native family table structure."));}
             catch(const std::exception& e){return Result::failure("metadata_failed",tr(e.what()));}
        });
    };
    add({"document.relations.get",tr("Read ordered parameter relations and cached physical values."),{{"document",false}},false},[this](const auto& id,const Json&){return relation_data(workspace::model_relations(workspace_,id));});
    add({"document.relations.set",tr("Replace and evaluate parameter relations without calculating geometry."),{{"relations",true,Type::Array},{"document",false}},true},[this](const auto& id,const Json& args){
        std::vector<document::ModelRelation> rows;
        for(const auto& row:args["relations"]) {
            fields(row,{"target","expression"});
            if(!row.contains("target") || !row["target"].is_string() || !row.contains("expression") || !row["expression"].is_string())throw std::invalid_argument("Every relation needs a string target and expression.");
            rows.push_back({row["target"].get<std::string>(),row["expression"].get<std::string>()});
        }
        const auto changed=workspace::set_model_relations(workspace_,id,std::move(rows));auto result=relation_data(workspace::model_relations(workspace_,id));result["changed"]=changed;return result;
    });
    add({"document.material.get",tr("Read document material properties, units and localized descriptions."),{{"document",false}},false},[this](const auto& id,const Json&){return material_data(workspace::material_data(workspace_,id));});
    add({"document.material.load",tr("Load a native material library into the document without retaining a file dependency."),{{"path",true},{"document",false}},true},[this](const auto& id,const Json& args){
        auto source=std::filesystem::u8path(args["path"].get<std::string>());
        if(source.is_relative())source=directory_/source;source=std::filesystem::absolute(source).lexically_normal();
        const auto changed=workspace::set_material_data(workspace_,id,document::load_material_library(source));
        auto result=material_data(workspace::material_data(workspace_,id));result["changed"]=changed;result["source"]=document::path_to_utf8(source);return result;
    });
    add({"document.material.set",tr("Replace material data and update cached physical relations."),{{"properties",true,Type::Array},{"document",false}},true},[this](const auto& id,const Json& args){
        document::MaterialData data;
        for(const auto& row:args["properties"]) {
            fields(row,{"key","value","unit","descriptions"});
            if(!row.contains("key") || !row["key"].is_string() || !row.contains("value") || !row["value"].is_string() || (row.contains("unit") && !row["unit"].is_string()))throw std::invalid_argument("Every material property needs a string key, value and optional unit.");
            const auto key=row["key"].get<std::string>();
            if(!data.properties.emplace(key,row["value"].get<std::string>()).second)throw std::invalid_argument("Material property names must be unique.");
            if(row.contains("unit"))data.units[key]=row["unit"].get<std::string>();
            if(row.contains("descriptions"))data.descriptions[key]=strings(row["descriptions"]);
        }
        const auto changed=workspace::set_material_data(workspace_,id,std::move(data));auto result=material_data(workspace::material_data(workspace_,id));result["changed"]=changed;return result;
    });
    add({"document.family.get",tr("Read the stored family table without generating instances."),{{"document",false}},false},[this](const auto& id,const Json&){return Json{{"table",Json::parse(document::serialize_family_table(workspace::family_table(workspace_,id)))}};});
    add({"document.family.set",tr("Replace the stored family table without generating geometry."),{{"table",true,Type::Object},{"document",false}},true},[this](const auto& id,const Json& args){
        const auto changed=workspace::set_family_table(workspace_,id,document::parse_family_table(args["table"].dump()));
        return Json{{"table",Json::parse(document::serialize_family_table(workspace::family_table(workspace_,id)))},{"changed",changed}};
    });
}
}

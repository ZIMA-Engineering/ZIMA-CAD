#include <zima/command_host/host.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include <zima/document/precision.hpp>
#include <charconv>
#include <algorithm>
#include <set>
#include "metadata_json.hpp"
namespace zima::command_host {
namespace {
using metadata_json::fields;
using metadata_json::strings;
Json parameter_data(const document::UserParameterData& data) {
    Json result=Json::array();for(const auto& key:data.order) {
        const auto labels=data.labels.find(key),values=data.values.find(key);
        result.push_back({{"key",key},{"labels",labels==data.labels.end()?Json::object():Json(labels->second)},
            {"values",values==data.values.end()?Json::object():Json(values->second)}});
    }return result;
}
Json settings_data(const document::FileSettingsData& data) {
    document::validate_file_settings(data);
    Json precision=Json::object();for(const auto& [key,value]:data.precision) {
        if(key=="decimal_places")precision[key]=static_cast<int>(document::precision_value(data.precision,key,3));
        else precision[key]=document::precision_value(data.precision,key,0);
    }
    return {{"units",data.units},{"precision",std::move(precision)},{"unit_choices",document::file_unit_choices()}};
}
}
void Host::register_metadata_commands() {
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
                if(changes && result.value("changed",false))change_=Change{result.value("calculated",false)?ChangeKind::Model:ChangeKind::Metadata,id,false};
                return Result::success(std::move(result));
            }catch(const std::invalid_argument& e){return Result::failure("invalid_arguments",tr(e.what()));}
             catch(const std::exception& e){return Result::failure("metadata_failed",tr(e.what()));}
        });
    };
    add({"document.parameters.get",tr("Read ordered user parameters, shared values and language variants."),{{"document",false}},false},[this](const auto& id,const Json&){
        return Json{{"parameters",parameter_data(workspace::user_parameters(workspace_,id))}};
    });
    add({"document.parameters.set",tr("Replace the complete ordered parameter table without calculating geometry."),{{"parameters",true,Type::Array},{"document",false}},true},[this](const auto& id,const Json& args){
        if(args["parameters"].size()>4096)throw std::invalid_argument("A document supports at most 4096 user parameters.");
        document::UserParameterData data;
        for(const auto& row:args["parameters"]) {
            fields(row,{"key","labels","values"});
            if(!row.contains("key") || !row["key"].is_string())throw std::invalid_argument("Every parameter needs a string key.");
            const auto key=row["key"].get<std::string>();data.order.push_back(key);
            if(row.contains("labels"))data.labels[key]=strings(row["labels"]);
            if(row.contains("values"))data.values[key]=strings(row["values"]);
        }
        const auto changed=workspace::set_user_parameters(workspace_,id,std::move(data));
        return Json{{"parameters",parameter_data(workspace::user_parameters(workspace_,id))},{"changed",changed}};
    });
    add({"document.settings.get",tr("Read document units, tolerances and display precision."),{{"document",false}},false},[this](const auto& id,const Json&){return settings_data(workspace::file_settings(workspace_,id));});
    add({"document.settings.set",tr("Update document units and precision, calculating affected local geometry when required."),{{"units",false,Type::Object},{"precision",false,Type::Object},{"document",false}},true},[this](const auto& id,const Json& args){
        if(!args.contains("units") && !args.contains("precision"))throw std::invalid_argument("Specify document units or precision to update.");
        auto data=workspace::file_settings(workspace_,id);
        if(args.contains("units")) {
            fields(args["units"],{"Length","Angle","Mass","Time","Temperature","Stress"});
            for(const auto& [key,value]:strings(args["units"]))data.units[key]=value;
        }
        if(args.contains("precision")) {
            fields(args["precision"],{"linear_tolerance","angular_tolerance","mesh_deflection","decimal_places"});
            for(auto it=args["precision"].begin();it!=args["precision"].end();++it) {
                if(!it.value().is_number() || !std::isfinite(it.value().get<double>()) ||
                   (it.key()=="decimal_places" && !it.value().is_number_integer()))
                    throw std::invalid_argument("Precision values must be finite numbers; decimal places must be an integer.");
                char buffer[64];const auto encoded=std::to_chars(buffer,buffer+sizeof(buffer),it.value().get<double>());
                if(encoded.ec!=std::errc{})throw std::invalid_argument("Cannot encode document precision.");
                data.precision[it.key()]={buffer,encoded.ptr};
            }
        }
        const auto change=workspace::set_file_settings(workspace_,kernel_,id,std::move(data));auto result=settings_data(workspace::file_settings(workspace_,id));result["changed"]=change.changed;result["calculated"]=change.calculated;return result;
    });
}
}

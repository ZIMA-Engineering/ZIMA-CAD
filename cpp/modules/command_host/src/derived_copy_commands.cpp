#include "derived_copy_command_support.hpp"
#include <zima/workspace/derived_copy_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <algorithm>
#include <cmath>
namespace zima::command_host {
namespace {
using Error=workspace::DerivedCopyError;
void keys(const Json& value,std::initializer_list<const char*> allowed) {
    if(!value.is_object())throw Error("invalid_arguments","Expected an object of copy parameters.");
    for(const auto& [key,field]:value.items())if(std::ranges::none_of(allowed,[&](const char* name){return key==name;}))
        throw Error("invalid_arguments","Unknown copy parameter field.");
}
double number(const Json& value) {
    if(!value.is_number()||!std::isfinite(value.get<double>()))throw Error("invalid_arguments","Copy dimensions must be finite JSON numbers.");
    return value.get<double>();
}
unsigned count(const Json& value,unsigned minimum,unsigned maximum) {
    if(!value.is_number_integer()||number(value)<minimum||number(value)>maximum)
        throw Error("invalid_arguments","Pattern counts must be integers within the supported range.");
    return value.get<unsigned>();
}
int axis(const Json& value) {
    if(!value.is_string())throw Error("invalid_arguments","Select local axis x, y or z.");
    const auto text=value.get<std::string>();
    if(text!="x"&&text!="y"&&text!="z")throw Error("invalid_arguments","Select local axis x, y or z.");
    return static_cast<int>(std::string("xyz").find(text));
}
document::ConstructionReference reference(const Json& value) {
    keys(value,{"owner","key","instance_path","offset_mm"});
    if(!value.contains("owner")||!value.contains("key"))throw Error("invalid_arguments","A target reference requires owner and key.");
    for(const auto* key:{"owner","key","instance_path"})if(value.contains(key)&&!value.at(key).is_string())
        throw Error("invalid_arguments","Reference fields must be supported text fields.");
    document::ConstructionReference result{value.value("instance_path",std::string{}),value.at("owner"),value.at("key")};
    if(result.owner_id.empty()||result.semantic_key.empty())throw Error("invalid_reference","A target reference requires owner and key.");
    static_cast<void>(assembly::InstancePath::decode(result.instance_path));
    if(value.contains("offset_mm"))result.offset=number(value.at("offset_mm"));
    return result;
}
void parameters(workspace::DerivedCopyDefinition& value,const workspace::DerivedCopyEdit& edit,const Json& args,bool pattern) {
    auto& params=value.parameters;
    if(args.contains("name"))value.name=args.at("name").get<std::string>();
    if(args.contains("source"))params.source_id=args.at("source").get<std::string>();
    if(args.contains("placement")) {
        const auto& values=args.at("placement");
        if(values.empty())throw Error("invalid_arguments","Specify at least one placement parameter.");
        for(const auto& [key,v]:values.items())
            if(!workspace::assign_placement_dimension(value.placement,edit.references,key,number(v)))
                throw Error("parameter_not_editable","This placement parameter is unavailable or locked.");
    }
    const auto local=pattern?"local_axis":"local_plane";
    if(args.contains("reference")&&args.contains(local))throw Error("invalid_arguments","Choose a local Origin reference or an original reference, not both.");
    if(pattern) {
        auto& p=*params.pattern;
        if(args.contains("mode")) {
            const auto mode=args.at("mode").get<std::string>();
            if(mode!="linear"&&mode!="circular")throw Error("invalid_arguments","Pattern mode must be linear or circular.");
            p.circular=mode=="circular";
        }
        if(!p.circular&&(args.contains("count")||args.contains("full_circle")||args.contains("angle_degrees")||args.contains("local_axis")||args.contains("reference")))
            throw Error("invalid_arguments","Circular settings require circular Pattern mode; use linear direction counts for a linear Pattern.");
        if(p.circular&&args.contains("linear"))throw Error("invalid_arguments","Linear directions require linear Pattern mode.");
        if(args.contains("count"))p.count=count(args.at("count"),2,1000);
        if(args.contains("full_circle"))p.full_circle=args.at("full_circle").get<bool>();
        if(args.contains("angle_degrees"))p.angle_degrees=number(args.at("angle_degrees"));
        if(args.contains("linear")) {
            const auto& directions=args.at("linear");
            if(directions.empty()||directions.size()>3)throw Error("invalid_arguments","Specify one to three local Pattern directions.");
            for(std::size_t i=0;i<p.linear.size();++i) {
                auto& d=p.linear[i];
                if(i>=directions.size()){d.local_axis=-1;continue;}
                const auto& row=directions[i];keys(row,{"axis","spacing_mm","count","reverse_count","distribution"});
                if(!row.contains("axis"))throw Error("invalid_arguments","Each Pattern direction requires an axis or null.");
                d.local_axis=row.at("axis").is_null()?-1:axis(row.at("axis"));
                if(row.contains("spacing_mm"))d.spacing=number(row.at("spacing_mm"));
                if(row.contains("count"))d.count=count(row.at("count"),2,1000);
                if(row.contains("reverse_count"))d.reverse_count=count(row.at("reverse_count"),1,999);
                if(row.contains("distribution")) {
                    if(!row.at("distribution").is_string())throw Error("invalid_arguments","Unknown Pattern distribution.");
                    const auto mode=row.at("distribution").get<std::string>();
                    if(mode=="forward")d.distribution=kernel::PatternDistribution::Forward;
                    else if(mode=="reverse")d.distribution=kernel::PatternDistribution::Reverse;
                    else if(mode=="both")d.distribution=kernel::PatternDistribution::Both;
                    else if(mode=="symmetric")d.distribution=kernel::PatternDistribution::Symmetric;
                    else throw Error("invalid_arguments","Unknown Pattern distribution.");
                }
            }
        }
    }
    if(args.contains("reference")) {
        params.reference=reference(args.at("reference"));
    }
    if(args.contains(local)) {
        std::string key;
        if(pattern)key="origin:axis:"+std::string(1,"xyz"[axis(args.at(local))]);
        else {
            const auto plane=args.at(local).get<std::string>();
            if(plane!="xy"&&plane!="xz"&&plane!="yz")throw Error("invalid_arguments","Select local plane xy, xz or yz.");
            key="origin:plane:"+plane;
        }
        params.reference={{},value.id+":origin",std::move(key)};
    }
    if((!pattern||params.pattern->circular||!params.reference.owner_id.empty())&&
        !document::is_derived_copy_origin_reference(params.reference,value.id,pattern))
        throw Error("invalid_reference",pattern?"Select an axis of the Pattern's own Origin.":"Select a plane of the Mirror's own Origin.");
}
}
void Host::register_derived_copy_commands() {
    using Type=commands::ArgumentType;
    for(const bool pattern:{false,true})for(const bool create:{true,false}) {
        std::vector<commands::Argument> fields;
        if(!create)fields.push_back({"object",true});
        fields.insert(fields.end(),{{"source",create},{"name",false},{"placement",false,Type::Object},{"reference",false,Type::Object}});
        if(pattern)fields.insert(fields.end(),{{"mode",false},{"linear",false,Type::Array},{"count",false,Type::Integer},
            {"angle_degrees",false,Type::Number},{"full_circle",false,Type::Boolean},{"local_axis",false}});
        else fields.push_back({"local_plane",false});
        fields.push_back({"document",false});
        const std::string prefix=pattern?"pattern":"mirror";
        const auto description=pattern?(create?tr("Create a Pattern from an owned Body or component."):tr("Change Pattern source, directions and properties.")):
            (create?tr("Create a Mirror from an owned Body or component."):tr("Change Mirror source, plane and properties."));
        dispatcher_.add({prefix+(create?".create":".set"),description,std::move(fields),true},[this,pattern,create](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;
            if(interaction().template_document)return Result::failure("unsupported_document",tr("Derived copies require an open Part or Assembly."));
            try {
                const auto id=workspace_.active_document_id();
                if(!create&&args.size()==1+(args.contains("document")?1:0))throw Error("invalid_arguments","Specify at least one copy parameter.");
                const auto edit=workspace::prepare_derived_copy_edit(workspace_,id,create?std::string{}:args.at("object").get<std::string>(),pattern);
                if(edit.initial.parameters.pattern.has_value()!=pattern)throw Error("wrong_feature","The requested copy type does not match this object.");
                auto value=edit.initial;parameters(value,edit,args,pattern);
                const bool changed=workspace::commit_derived_copy(workspace_,kernel_,edit,std::move(value));
                auto data=derived_copy_details(workspace_,id,edit.initial.id,pattern);data["changed"]=changed;
                if(changed)change_=Change{ChangeKind::Model,id};
                if(const auto* part=workspace_.open_part(id)) {
                    const auto& calculated=part->session.calculated_boundaries();
                    data["calculation_errors"]=calculated.empty()?Json::object():Json(calculated.back().calculation_errors);
                    if(changed&&!data.at("calculation_errors").empty())
                        return Result{false,"calculation_errors",tr("History changed; some dependent features could not be calculated."),std::move(data)};
                }
                return Result::success(std::move(data));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("derived_copy_rejected",tr(error.what()));}
        });
    }
}
} // namespace zima::command_host

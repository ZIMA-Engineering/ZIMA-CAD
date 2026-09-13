#include <zima/command_host/host.hpp>
#include <zima/workspace/measurement_operations.hpp>
#include <zima/document/measurement_record.hpp>
#include <array>
namespace zima::command_host {
namespace {
struct Error : std::runtime_error {
    const char* code;
    Error(const char* code,const char* text):std::runtime_error(text),code(code){}
};
struct Source {
    std::string id;
    std::uint64_t revision;
    const std::vector<kernel::SavedMeasurement>* records;
};
Source source(const workspace::Workspace& live,const Json& args) {
    const auto id=args.value("document",live.active_document_id());
    if(const auto* part=live.open_part(id))return {id,part->session.revision(),&part->session.document().measurements};
    if(const auto* assembly=live.open_assembly(id))return {id,assembly->session.revision(),&assembly->session.document().measurements};
    throw Error("unsupported_document","Measurement commands require an open Part or Assembly.");
}
std::vector<kernel::MeasurementReference> references(const Json& input) {
    if(input.empty()||input.size()>2)throw Error("invalid_arguments","Measurement requires one or two original references.");
    constexpr std::array kinds{"point","curve","face","object","axis","plane"};
    std::vector<kernel::MeasurementReference> result;
    for(const auto& item:input) {
        if(!item.is_object()||!item.contains("kind"))throw Error("invalid_arguments","A measurement reference requires its kind and original identity.");
        for(const auto& [key,value]:item.items())
            if((key!="kind"&&key!="owner"&&key!="key"&&key!="instance_path")||!value.is_string())
                throw Error("invalid_arguments","Measurement references accept only kind, owner, key and instance_path strings.");
        const auto kind=item.at("kind").get<std::string>();const auto found=std::ranges::find(kinds,kind);
        if(found==kinds.end())throw Error("invalid_arguments","Unknown measurement reference kind.");
        kernel::MeasurementReference ref{static_cast<kernel::MeasurementKind>(found-kinds.begin()),item.value("owner",std::string{}),
            item.value("key",std::string{}),item.value("instance_path",std::string{})};
        if(ref.kind==kernel::MeasurementKind::Object?
            ((ref.owner_id.empty()&&ref.instance_path.empty())||!ref.semantic_key.empty()):
            (ref.owner_id.empty()||ref.semantic_key.empty()||ref.semantic_key=="container:display"))
            throw Error("invalid_reference","A measurement requires an original object or exact occurrence identity.");
        if(!ref.instance_path.empty())try {
            if(assembly::InstancePath::decode(ref.instance_path).encoded()!=ref.instance_path)throw std::invalid_argument("path");
        }catch(const std::exception&) {throw Error("invalid_reference","The measurement occurrence path is invalid.");}
        result.push_back(std::move(ref));
    }
    return result;
}
void units(Json& value) {
    value["units"]={{"position","mm"},{"length","mm"},{"area","mm2"},{"volume","mm3"},{"mass","kg"},{"distance","mm"}};
}
Json details(const kernel::SavedMeasurement& record) {
    auto result=Json::parse(document::serialize_measurements({record})).at(0);
    result["object"]=record.id;units(result);return result;
}
}
void Host::register_measurement_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"measurement.list",tr("List saved measurements without recalculating their values."),
        {{"offset",false,Type::Integer},{"limit",false,Type::Integer},{"document",false}},false},[this](const Json& args) {
        try {
            const auto data=source(workspace_,args);const auto offset=args.value("offset",0LL),limit=args.value("limit",2000LL);
            if(offset<0||offset>100000000||limit<1||limit>10000)throw Error("invalid_arguments","Measurement query offset or limit is outside the supported range.");
            auto items=Json::array();
            for(std::size_t i=static_cast<std::size_t>(offset);i<data.records->size()&&items.size()<static_cast<std::size_t>(limit);++i) {
                const auto& record=(*data.records)[i];items.push_back({{"object",record.id},{"name",record.name},
                    {"body",record.body_id},{"after_object",record.after_object_id},{"reference_count",record.references.size()}});
            }
            return Result::success({{"document",data.id},{"revision",data.revision},{"items",std::move(items)},{"total",data.records->size()}});
        }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("measurement_query_failed",tr(error.what()));}
    });
    dispatcher_.add({"measurement.get",tr("Read the last saved values and original references of a measurement."),
        {{"object",true},{"document",false}},false},[this](const Json& args) {
        try {
            const auto data=source(workspace_,args);const auto id=args.at("object").get<std::string>();
            const auto found=std::ranges::find(*data.records,id,&kernel::SavedMeasurement::id);
            if(found==data.records->end())throw Error("measurement_not_found","The requested measurement does not exist in this document.");
            auto result=details(*found);result["document"]=data.id;result["revision"]=data.revision;result["saved_values"]=true;
            return Result::success(std::move(result));
        }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("measurement_query_failed",tr(error.what()));}
    });
    dispatcher_.add({"measurement.evaluate",tr("Measure one or two original references using already calculated geometry."),
        {{"references",true,Type::Array},{"document",false}},false},[this](const Json& args) {
        try {
            const auto data=source(workspace_,args);kernel::SavedMeasurement record;record.references=references(args.at("references"));
            const auto scene=workspace::measurement_scene(workspace_,data.id);
            std::vector<measurement::MeasurementGeometry> geometry;
            for(std::size_t i=0;i<record.references.size();++i) {
                auto value=workspace::resolve_measurement(workspace_,data.id,record.references[i],scene);
                if(!value) {
                    auto result=Result::failure("missing_reference",tr("The original measurement reference is unavailable."));
                    result.data={{"reference_index",i}};return result;
                }
                record.values.push_back(value->values);geometry.push_back(std::move(*value));
            }
            if(geometry.size()==2)record.distance=measurement::measure_distance(geometry[0],geometry[1]);
            auto result=details(record);
            for(const auto* field:{"id","object","name","body_id","after_object_id"})result.erase(field);
            result["document"]=data.id;result["revision"]=data.revision;result["body_calculated"]=false;result["saved_values"]=false;
            return Result::success(std::move(result));
        }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("measurement_query_failed",tr(error.what()));}
    });
}
}

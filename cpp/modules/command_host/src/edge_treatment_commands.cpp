#include <zima/command_host/host.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cctype>
#include <limits>
#include <zima/workspace/history_operations.hpp>
namespace zima::command_host {
namespace {
using Error=workspace::EdgeTreatmentOperationError;
using Parameters=document::EdgeTreatmentParameters;
const document::HistoryContainer& treatment(const workspace::PartState* state,const std::string& id,bool fillet) {
    if(!state)throw Error("unsupported_document","Edge treatment operations require an open Part.");
    const auto* feature=state->session.document().find_container(id);
    if(!feature)throw Error("container_not_found","The requested container does not exist.");
    if(feature->feature_kind!=(fillet?document::FeatureKind::Fillet:document::FeatureKind::Chamfer))
        throw Error("wrong_feature","The requested edge treatment type does not match the container.");
    return *feature;
}
template<class Ref> Json reference(const Ref& ref) {
    return {{"owner",ref.owner_id},{"key",ref.semantic_key},{"instance_path",ref.instance_path}};
}
template<class Ref> Ref parse_reference(const Json& value) {
    if(!value.is_object()||!value.contains("owner")||!value.contains("key"))throw Error("invalid_arguments","A target reference requires owner and key.");
    for(const auto& [key,field]:value.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!field.is_string())
        throw Error("invalid_arguments","Reference fields must be supported text fields.");
    Ref ref{value.at("owner"),value.at("key"),value.value("instance_path",std::string{})};
    if(!ref.valid()||!ref.instance_path.empty())throw Error("invalid_reference","Select a local persisted input reference.");
    return ref;
}
Parameters parse_routes(const Json& values) {
    Parameters result;std::size_t total{};
    if(values.empty()||values.size()>10000)throw Error("invalid_arguments","Specify between 1 and 10000 nonempty edge routes.");
    for(const auto& value:values) {
        if(!value.is_object()||!value.contains("edges")||!value.at("edges").is_array()||value.at("edges").empty())
            throw Error("invalid_arguments","Each route requires a nonempty edges array and an optional start reference.");
        for(const auto& [key,field]:value.items())if(key!="edges"&&key!="start")
            throw Error("invalid_arguments","Each route requires a nonempty edges array and an optional start reference.");
        total+=value.at("edges").size();if(total>10000)throw Error("invalid_arguments","Too many selected input edges.");
        std::vector<kernel::EdgeReference> edges;for(const auto& edge:value.at("edges"))edges.push_back(parse_reference<kernel::EdgeReference>(edge));
        result.routes.push_back(std::move(edges));
        result.route_start_vertices.push_back(value.contains("start")&&!value.at("start").is_null()?parse_reference<kernel::VertexReference>(value.at("start")):kernel::VertexReference{});
    }
    return result;
}
Json details(const workspace::PartState& state,const document::HistoryContainer& feature) {
    const auto& doc=state.session.document();const auto* body=doc.body_owner_for_object(feature.id);
    const auto& params=feature.edge_treatment;const bool fillet=feature.feature_kind==document::FeatureKind::Fillet;
    auto routes=Json::array();for(std::size_t i=0;i<params.routes.size();++i) {
        auto edges=Json::array();for(const auto& edge:params.routes[i])edges.push_back(reference(edge));
        const auto start=i<params.route_start_vertices.size()?params.route_start_vertices[i]:kernel::VertexReference{};
        routes.push_back({{"edges",std::move(edges)},{"start",start.valid()?reference(start):Json(nullptr)}});
    }
    Json data={{"document",doc.document_id},{"container",feature.id},{"feature",feature.feature_id},{"name",feature.name},
        {"body",body?body->scope.id:std::string{}},{"routes",std::move(routes)},{"value_locks",feature.value_locks},{"revision",state.session.revision()}};
    if(fillet) {
        data["mode"]=params.fillet_mode==Parameters::FilletMode::Linear?"linear":"constant";
        data["radius_mm"]=params.primary_size;data["radius_end_mm"]=params.secondary_size;data["reverse"]=params.reverse;
    }else {
        data["mode"]=params.chamfer_mode==Parameters::ChamferMode::TwoDistances?"two_distances":params.chamfer_mode==Parameters::ChamferMode::DistanceAngle?"distance_angle":"equal_distance";
        data["distance_a_mm"]=params.primary_size;data["distance_b_mm"]=params.secondary_size;
        data["angle_degrees"]=params.angle_degrees;data["flip"]=params.flip;
    }
    return data;
}
}
void Host::register_edge_treatment_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"edge_treatment.remove",tr("Remove an input edge or route; the last route removes its treatment."),
        {{"container",true},{"route",true,Type::Integer},{"edge",false,Type::Object},{"document",false}},true},[this](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;const auto id=workspace_.active_document_id();
            try {
                auto* state=workspace_.open_part(id);
                if(!state||interaction().template_document)throw Error("unsupported_document","Edge treatment operations require an open Part.");
                if(args.at("route")<0||args.at("route").get<std::uint64_t>()>std::numeric_limits<std::size_t>::max())
                    throw Error("invalid_route","The selected edge route does not exist.");
                const auto container=args.at("container").get<std::string>();
                const auto edge=args.contains("edge")?std::optional{parse_reference<kernel::EdgeReference>(args.at("edge"))}:std::nullopt;
                const bool removed=workspace::remove_edge_treatment_selection(workspace_,kernel_,id,container,args.at("route").get<std::size_t>(),edge);
                Json data={{"document",id},{"container",container},{"revision",state->session.revision()}};
                if(!removed)data=details(*state,*state->session.document().find_container(container));
                data["changed"]=true;data["removed"]=removed;
                const auto& boundaries=state->session.calculated_boundaries();
                data["calculation_errors"]=boundaries.empty()?Json::object():Json(boundaries.back().calculation_errors);
                change_=Change{ChangeKind::Model,id,true};
                if(!data.at("calculation_errors").empty())
                    return Result{false,"calculation_errors",tr("History changed; some dependent features could not be calculated."),std::move(data)};
                return Result::success(std::move(data));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const workspace::HistoryOperationError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("edge_treatment_rejected",tr(error.what()));}
        });
    for(const bool fillet:{true,false}) {
        const std::string prefix=fillet?"fillet":"chamfer";
        dispatcher_.add({prefix+".get",fillet?tr("Read Fillet radii, mode and original input routes."):tr("Read Chamfer distances, angle and original input routes."),
            {{"container",true},{"document",false}},false},[this,fillet](const Json& args) {
                try {const auto* state=workspace_.open_part(args.value("document",workspace_.active_document_id()));
                    const auto& feature=treatment(state,args.at("container"),fillet);return Result::success(details(*state,feature));
                }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
            });
        for(const bool create:{true,false}) {
            std::vector<commands::Argument> args;if(!create)args.push_back({"container",true});
            args.push_back({"routes",create,Type::Array});args.push_back({"mode",false});
            args.push_back({fillet?"radius_mm":"distance_a_mm",false,Type::Number});
            args.push_back({fillet?"radius_end_mm":"distance_b_mm",false,Type::Number});
            if(!fillet)args.push_back({"angle_degrees",false,Type::Number});
            args.push_back({fillet?"reverse":"flip",false,Type::Boolean});args.push_back({"name",false});args.push_back({"document",false});
            dispatcher_.add({prefix+(create?".create":".set"),fillet?(create?tr("Create a Fillet on persisted input edge routes."):tr("Change Fillet radii, direction and input routes.")):
                (create?tr("Create a Chamfer on persisted input edge routes."):tr("Change Chamfer distances, side and input routes.")),std::move(args),true},[this,fillet,create](const Json& args) {
                const auto checked=target(args);if(!checked.ok)return checked;const auto id=workspace_.active_document_id();
                try {
                    auto* state=workspace_.open_part(id);
                    if(!state||interaction().template_document)throw Error("unsupported_document","Edge treatment operations require an open Part.");
                    if(!create&&args.size()==1+(args.contains("document")?1:0))throw Error("invalid_arguments","Specify at least one edge treatment parameter.");
                    const auto selected=args.contains("routes")?parse_routes(args.at("routes")):Parameters{};
                    auto feature=create?(fillet?document::PartDocument::create_fillet_container(selected.flattened_edges()):document::PartDocument::create_chamfer_container(selected.flattened_edges())):treatment(state,args.at("container"),fillet);
                    if(create)feature.name=fillet?tr("Zaoblení"):tr("Sražení");
                    const auto* body=create?state->session.document().body_history.find(state->session.document().body_history.active_body_id()):state->session.document().body_owner_for_object(feature.id);
                    if(body&&body->scope.id!=state->session.document().body_history.active_body_id())throw Error("inactive_body","Activate the owning Body before editing its edge treatment.");
                    if(args.contains("name")) {
                        feature.name=args.at("name").get<std::string>();document::validate_native_metadata_text(feature.name);
                        if(feature.name.empty()||std::ranges::all_of(feature.name,[](unsigned char c){return std::isspace(c)!=0;}))
                            throw Error("invalid_arguments","Specify a nonempty object name.");
                    }
                    auto& params=feature.edge_treatment;
                    if(args.contains("routes")){params.routes=selected.routes;params.route_start_vertices=selected.route_start_vertices;}
                    if(args.contains("mode")) {
                        const auto mode=args.at("mode").get<std::string>();
                        if(fillet&&mode=="constant")params.fillet_mode=Parameters::FilletMode::Constant;
                        else if(fillet&&mode=="linear")params.fillet_mode=Parameters::FilletMode::Linear;
                        else if(!fillet&&mode=="equal_distance")params.chamfer_mode=Parameters::ChamferMode::EqualDistance;
                        else if(!fillet&&mode=="two_distances")params.chamfer_mode=Parameters::ChamferMode::TwoDistances;
                        else if(!fillet&&mode=="distance_angle")params.chamfer_mode=Parameters::ChamferMode::DistanceAngle;
                        else throw Error("invalid_arguments","Unsupported edge treatment mode.");
                    }
                    const char* first=fillet?"radius_mm":"distance_a_mm";const char* second=fillet?"radius_end_mm":"distance_b_mm";
                    if(args.contains(first))params.primary_size=args.at(first).get<double>();if(args.contains(second))params.secondary_size=args.at(second).get<double>();
                    if(args.contains("angle_degrees"))params.angle_degrees=args.at("angle_degrees").get<double>();
                    if(args.contains("reverse"))params.reverse=args.at("reverse").get<bool>();if(args.contains("flip"))params.flip=args.at("flip").get<bool>();
                    const auto container=feature.id;
                    const bool changed=workspace::commit_edge_treatment(workspace_,kernel_,id,std::move(feature),create?workspace::EdgeTreatmentEditMode::Create:workspace::EdgeTreatmentEditMode::Replace);
                    if(changed)change_=Change{ChangeKind::Model,id};auto data=details(*state,treatment(state,container,fillet));data["changed"]=changed;return Result::success(std::move(data));
                }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
                 catch(const std::exception& error){return Result::failure("edge_treatment_rejected",tr(error.what()));}
            });
        }
    }
}
} // namespace zima::command_host

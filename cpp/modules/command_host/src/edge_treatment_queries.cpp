#include <zima/command_host/host.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/kernel/tangent_edge_route.hpp>
#include <algorithm>
#include <map>
#include <cmath>
namespace zima::command_host {
namespace {
class QueryError : public std::runtime_error {
public:
    QueryError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
using Key=std::pair<std::string,std::string>;
using Groups=std::map<Key,std::vector<const kernel::ViewerEdge*>>;
struct Input {const workspace::PartState& state;const kernel::BodyResult& result;std::string body;std::string container;};
Input input(const workspace::Workspace& live,const Json& args) {
    const auto id=args.value("document",live.active_document_id());const auto* state=live.open_part(id);
    if(!state)throw QueryError("unsupported_document","Edge treatment queries require an open Part.");
    const auto& doc=state->session.document();const auto container=args.value("container",std::string{});
    if(args.contains("container")) {
        const auto* feature=doc.find_container(container);
        if(!feature)throw QueryError("container_not_found","The requested container does not exist.");
        if(feature->feature_kind!=document::FeatureKind::Fillet&&feature->feature_kind!=document::FeatureKind::Chamfer)
            throw QueryError("wrong_feature","This container is not a Fillet or Chamfer.");
    }
    const auto* result=workspace::calculated_operation_input(state->session,container);
    if(!result||result->mesh.triangles.empty())throw QueryError("missing_input","Edge treatment requires a calculated input body.");
    const auto* body=container.empty()?doc.body_history.find(doc.body_history.active_body_id()):doc.body_owner_for_object(container);
    return {*state,*result,body?body->scope.id:std::string{},container};
}
Groups groups(const Input& source) {
    Groups result;for(const auto& edge:source.result.mesh.edges)
        if(edge.reference.valid()&&edge.reference.instance_path.empty())result[{edge.reference.owner_id,edge.reference.semantic_key}].push_back(&edge);
    return result;
}
template<class Ref> Json identity(const Ref& reference) {
    return {{"owner",reference.owner_id},{"key",reference.semantic_key},{"instance_path",reference.instance_path}};
}
Json point(kernel::Vec3 value){return Json::array({value.x,value.y,value.z});}
Json envelope(const Input& source) {
    return {{"document",source.state.session.document().document_id},{"container",source.container},{"body",source.body},
        {"coordinate_system",source.body.empty()?"document":"body"},{"length_unit","mm"},{"revision",source.state.session.revision()}};
}
std::size_t page(const Json& args,const char* key,std::size_t fallback,std::size_t maximum,bool positive=false) {
    if(!args.contains(key))return fallback;
    if(args.at(key)<0||args.at(key)>maximum||(positive&&args.at(key)==0))
        throw QueryError("invalid_arguments","Edge pagination is outside the supported range.");
    return args.at(key).get<std::size_t>();
}
Json describe(const std::vector<const kernel::ViewerEdge*>& segments) {
    auto item=identity(segments.front()->reference);item["ambiguous"]=segments.size()!=1;auto values=Json::array();
    for(const auto* edge:segments) {
        auto endpoints=Json::array();
        if(edge->edge_treatment_endpoint_references.size()==2&&!edge->points.empty())for(std::size_t i=0;i<2;++i) {
            auto endpoint=identity(edge->edge_treatment_endpoint_references[i]);endpoint["position_mm"]=point(i==0?edge->points.front():edge->points.back());endpoints.push_back(std::move(endpoint));
        }
        values.push_back({{"point_count",edge->points.size()},{"length_mm",edge->measured_length?Json(*edge->measured_length):Json(nullptr)},
            {"parameter_seam",edge->parameter_seam},{"endpoints",std::move(endpoints)}});
    }
    item["segments"]=std::move(values);return item;
}
}
void Host::register_edge_treatment_queries() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"edge_treatment.edges",tr("List persisted edges of the real Fillet or Chamfer input body."),
        {{"container",false},{"owner",false},{"offset",false,Type::Integer},{"limit",false,Type::Integer},{"document",false}},false},[this](const Json& args) {
            try {
                const auto source=input(workspace_,args);const auto edges=groups(source);
                const auto offset=page(args,"offset",0,100000000),limit=page(args,"limit",500,5000,true);
                auto items=Json::array();std::size_t total{};
                for(const auto& [key,segments]:edges) {
                    if(args.contains("owner")&&args.at("owner")!=key.first)continue;
                    if(total>=offset&&items.size()<limit)items.push_back(describe(segments));++total;
                }
                auto data=envelope(source);data["items"]=std::move(items);data["total"]=total;data["offset"]=offset;data["limit"]=limit;
                data["has_more"]=offset<total&&data["items"].size()<total-offset;return Result::success(std::move(data));
            }catch(const QueryError& error){return Result::failure(error.code,tr(error.what()));}
        });
    dispatcher_.add({"edge_treatment.route",tr("Read the unambiguous tangent route of a persisted input edge."),
        {{"seed",true,Type::Object},{"container",false},{"tolerance_degrees",false,Type::Number},{"document",false}},false},[this](const Json& args) {
            try {
                const auto source=input(workspace_,args);const auto edges=groups(source);const auto& value=args.at("seed");
                if(!value.contains("owner")||!value.contains("key"))throw QueryError("invalid_arguments","A target reference requires owner and key.");
                for(const auto& [key,field]:value.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!field.is_string())
                    throw QueryError("invalid_arguments","Reference fields must be supported text fields.");
                const kernel::EdgeReference seed{value.at("owner"),value.at("key"),value.value("instance_path",std::string{})};
                const auto angle=args.value("tolerance_degrees",35.);
                if(!std::isfinite(angle)||angle<0||angle>90)throw QueryError("invalid_arguments","Tangent tolerance must be between 0 and 90 degrees.");
                if(!seed.valid()||!seed.instance_path.empty())throw QueryError("invalid_reference","Select an unambiguous edge of the calculated input body.");
                const auto route=kernel::tangent_edge_route(source.result.mesh.edges,seed,angle);
                if(route.empty())throw QueryError("invalid_reference","Select an unambiguous edge of the calculated input body.");
                auto members=Json::array();std::map<Key,std::pair<Json,std::size_t>> vertices;bool complete_endpoints=true;
                for(const auto& reference:route) {
                    const auto found=edges.find({reference.owner_id,reference.semantic_key});
                    if(found==edges.end()||found->second.size()!=1)throw QueryError("invalid_reference","Select an unambiguous edge of the calculated input body.");
                    const auto& edge=*found->second.front();
                    members.push_back(identity(reference));
                    // A closed circle has no two distinct persisted endpoints
                    // in the current viewer packet. Retain its exact edge;
                    // do not invent a vertex or infer connectivity by distance.
                    if(edge.edge_treatment_endpoint_references.size()!=2||edge.points.empty()||
                        std::ranges::any_of(edge.edge_treatment_endpoint_references,[](const auto& ref){return !ref.valid();})) {
                        complete_endpoints=false;continue;
                    }
                    for(std::size_t i=0;i<2;++i) {
                        const auto& ref=edge.edge_treatment_endpoint_references[i];
                        auto& vertex=vertices[{ref.owner_id,ref.semantic_key}];vertex.first=identity(ref);
                        vertex.first["position_mm"]=point(i==0?edge.points.front():edge.points.back());++vertex.second;
                    }
                }
                auto endpoints=Json::array();if(complete_endpoints)for(const auto& [key,vertex]:vertices)if(vertex.second==1)endpoints.push_back(vertex.first);
                auto data=envelope(source);data["edges"]=std::move(members);
                data["endpoints_complete"]=complete_endpoints;data["closed"]=complete_endpoints?Json(endpoints.empty()):Json(nullptr);
                data["endpoints"]=std::move(endpoints);
                data["tolerance_degrees"]=angle;return Result::success(std::move(data));
            }catch(const QueryError& error){return Result::failure(error.code,tr(error.what()));}
        });
}
} // namespace zima::command_host

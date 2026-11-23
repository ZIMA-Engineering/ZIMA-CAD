#include <zima/workspace/cam_operations.hpp>
#include <zima/workspace/calculated_body_source.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/kernel/stable_id.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <map>
#include <set>

namespace zima::workspace {
namespace {
[[noreturn]] void invalid(){throw BodyOperationError("cam_input_unavailable","CAM requires current, available inputs.");}
std::string digest(const std::string& text) {
    unsigned char bytes[EVP_MAX_MD_SIZE];unsigned size{};
    if(EVP_Digest(text.data(),text.size(),bytes,&size,EVP_sha256(),nullptr)!=1)invalid();
    constexpr char hex[]="0123456789abcdef";std::string result;
    for(unsigned i=0;i<size;++i){result+=hex[bytes[i]>>4];result+=hex[bytes[i]&15];}return result;
}
struct Inputs {
    CamResultState state{CamResultState::Current};
    std::vector<std::pair<std::string,kernel::BodySnapshot>> material;
    std::vector<sketcher::Sketch> sketches;
    std::vector<std::pair<std::string,std::string>> keys;
};
class InputReader {
public:
    explicit InputReader(const document::DocumentSession& s):session(s),doc(s.document()),graph(doc.body_history){}
    CamResultState state(const std::string& id) {
        if(const auto found=states.find(id);found!=states.end())return found->second;
        const auto* body=graph.find(id);
        if(!body||!body->cam)return CamResultState::MissingSource;
        if(body->suppressed)return CamResultState::SuppressedSource;
        if(!visiting.insert(id).second)return CamResultState::InvalidSource;
        const auto inputs=read(id);
        auto status=inputs.state;const auto& d=*body->cam;
        if(status==CamResultState::Current) {
            if(!d.calculated)status=CamResultState::NotCalculated;
            else if(!document::cam_result_matches_definition(d)||d.calculated->sources!=inputs.keys)status=CamResultState::Stale;
            else if(document::cam_result_has_collisions(*d.calculated))status=CamResultState::Collision;
        }
        visiting.erase(id);states.emplace(id,status);return status;
    }
    Inputs read(const std::string& id) {
        Inputs result;const auto* body=graph.find(id);
        if(!body||!body->cam){result.state=CamResultState::MissingSource;return result;}
        if(body->suppressed){result.state=CamResultState::SuppressedSource;return result;}
        const auto& d=*body->cam;
        if(!d.work_frame.reference_valid){result.state=CamResultState::UnresolvedFrame;return result;}
        if(!d.work_frame.references.empty()) {
            auto resolved=d.work_frame;
            if(!document::resolve_placement(resolved,cam_placement_geometry(session,prepare_cam_edit(doc,id)))) {
                result.state=CamResultState::UnresolvedFrame;return result;
            }
            if(resolved!=d.work_frame){result.state=CamResultState::Stale;return result;}
        }
        std::set<std::string> included;
        const auto add=[&](const std::string& source,bool stock,bool material) {
            if(result.state!=CamResultState::Current||!included.insert(source).second)return;
            const auto* source_body=graph.find(source);const auto* operation=graph.find_boolean(source);
            if(!source_body&&!operation){result.state=CamResultState::MissingSource;return;}
            if(source_body?source_body->suppressed:operation->suppressed){result.state=CamResultState::SuppressedSource;return;}
            if(source_body&&source_body->cam) {
                if(!stock){result.state=CamResultState::InvalidSource;return;}
                result.state=state(source);if(result.state!=CamResultState::Current)return;
                const auto& calculated=*source_body->cam->calculated;
                if(material)result.material.emplace_back(source,calculated.remaining);
                result.keys.emplace_back(source,calculated.id+":"+calculated.remaining->source_fingerprint);
            }else {
                const auto* shape=calculated_material_source(session,source);
                if(!shape){result.state=CamResultState::InvalidSource;return;}
                if(material)result.material.emplace_back(source,*shape);
                result.keys.emplace_back(source,(*shape)->source_fingerprint);
            }
        };
        add(d.stock_id,true,true);add(d.target_id,false,true);
        for(const auto& source:d.fixture_ids)add(source,false,true);
        for(const auto& reference:d.work_frame.references) {
            auto owner=reference.owner_id;
            if(owner.ends_with(":origin"))owner.resize(owner.size()-7);
            if(const auto* branch=doc.body_owner_for_object(owner))add(branch->scope.id,false,false);
            else if(graph.find(owner)||graph.find_boolean(owner))add(owner,false,false);
        }
        for(const auto& id:d.clamping_sketch_ids) {
            if(result.state!=CamResultState::Current)break;
            const auto found=std::ranges::find(doc.sketches,id,&sketcher::Sketch::id);
            if(found==doc.sketches.end()){result.state=CamResultState::MissingSource;break;}
            const auto* owner=doc.body_owner_for_object(id);
            const auto* feature=doc.find_container(found->owner_container_id);
            if(!owner){result.state=CamResultState::InvalidSource;break;}
            if(owner->suppressed||found->suppressed||(feature&&feature->suppressed)){
                result.state=CamResultState::SuppressedSource;break;
            }
            if(!owner->scope.placement.reference_valid||(feature&&!feature->placement.reference_valid)){
                result.state=CamResultState::UnresolvedFrame;break;
            }
            add(owner->scope.id,false,false);
            result.keys.emplace_back(id,digest(found->serialized()+nlohmann::json(owner->scope.placement).dump()));
            result.sketches.push_back(*found);
        }
        return result;
    }
private:
    const document::DocumentSession& session;
    const document::PartDocument& doc;
    const document::BodyHistoryGraph& graph;
    std::map<std::string,CamResultState> states;
    std::set<std::string> visiting;
};
}
BodyGraphEdit prepare_cam_edit(const document::PartDocument& doc,const std::string& id) {
    if(!id.empty()&&(!doc.body_history.find(id)||!doc.body_history.find(id)->cam))invalid();
    return {doc.document_id,id.empty()?kernel::make_stable_id():id,doc.body_history,doc.body_history};
}
CamResultState cam_result_state(const document::DocumentSession& session,const std::string& id) {
    return InputReader(session).state(id);
}
std::vector<std::string> cam_source_candidates(const document::DocumentSession& session,const BodyGraphEdit& edit,bool stock) {
    validate_body_graph_edit(session,edit);InputReader reader(session);
    const auto& graph=session.document().body_history;const auto own=std::ranges::find(graph.order(),edit.object_id);
    const auto boundary=own==graph.order().end()?graph.insertion_cursor():static_cast<std::size_t>(own-graph.order().begin());
    std::vector<std::string> result;
    for(std::size_t i=0;i<boundary;++i) {
        const auto& id=graph.order()[i];const auto* body=graph.find(id);
        if(calculated_material_source(session,id)||(stock&&body&&body->cam&&reader.state(id)==CamResultState::Current))result.push_back(id);
    }
    return result;
}
kernel::ViewerReferenceGeometry cam_placement_geometry(const document::DocumentSession& session,const BodyGraphEdit& edit) {
    auto geometry=session.document().origin_viewer_mesh().original_references;
    for(const auto& source_id:calculated_material_candidates(session,edit)) {
        const auto* source=calculated_material_source(session,source_id);
        auto add=material_source_placement_geometry(session.document(),*source,source_id);
        const auto offset=static_cast<std::uint32_t>(geometry.vertices.size());
        geometry.vertices.insert(geometry.vertices.end(),add.vertices.begin(),add.vertices.end());
        for(auto t:add.triangles)geometry.triangles.push_back(t+offset);
        geometry.triangle_references.insert(geometry.triangle_references.end(),add.triangle_references.begin(),add.triangle_references.end());
        geometry.edges.insert(geometry.edges.end(),add.edges.begin(),add.edges.end());
        geometry.points.insert(geometry.points.end(),add.points.begin(),add.points.end());
        geometry.axes.insert(geometry.axes.end(),add.axes.begin(),add.axes.end());
    }
    return geometry;
}
bool commit_cam(Workspace& live,const BodyGraphEdit& edit,document::BodyHistory value) {
    auto* state=live.open_part(edit.document_id);if(!state)invalid();auto& session=state->session;
    validate_body_graph_edit(session,edit);
    if(value.scope.id!=edit.object_id||!value.cam)invalid();
    const auto* initial=session.document().body_history.find(edit.object_id);
    if(initial&&!initial->cam)invalid();
    if(initial&&*initial==value)return false;
    // Only explicit calculation publication may replace a retained result.
    value.cam->calculated=initial?initial->cam->calculated:nullptr;
    const auto stocks=cam_source_candidates(session,edit,true),materials=cam_source_candidates(session,edit,false);
    const auto accepts=[](const auto& list,const std::string& id){return std::ranges::find(list,id)!=list.end();};
    if((!initial||initial->cam->stock_id!=value.cam->stock_id)&&!accepts(stocks,value.cam->stock_id))invalid();
    if((!initial||initial->cam->target_id!=value.cam->target_id)&&!accepts(materials,value.cam->target_id))invalid();
    for(const auto& id:value.cam->fixture_ids)
        if((!initial||!accepts(initial->cam->fixture_ids,id))&&!accepts(materials,id))invalid();
    for(const auto& id:value.cam->clamping_sketch_ids) {
        if(initial&&accepts(initial->cam->clamping_sketch_ids,id))continue;
        const auto& doc=session.document();const auto found=std::ranges::find(doc.sketches,id,&sketcher::Sketch::id);
        const auto* owner=doc.body_owner_for_object(id);
        if(found==doc.sketches.end()||found->suppressed||!owner||!accepts(materials,owner->scope.id))invalid();
        if(const auto* feature=doc.find_container(found->owner_container_id);feature&&feature->suppressed)invalid();
    }
    auto resolved=value.cam->work_frame;
    if(document::resolve_placement(resolved,cam_placement_geometry(session,edit)))value.cam->work_frame=std::move(resolved);
    else {
        if(!initial)invalid();
        value.cam->work_frame=initial->cam->work_frame;
        value.cam->work_frame.references=std::move(resolved.references);value.cam->work_frame.reference_valid=false;
    }
    auto next=session.document();auto graph=next.body_history;
    if(initial)graph.update_body(std::move(value));else static_cast<void>(graph.create_cam(std::move(value)));
    if(graph==next.body_history)return false;
    next.set_body_history(std::move(graph));next.validate_body_ownership();session.commit_metadata(std::move(next));return true;
}
CamCalculationInput prepare_cam_calculation(const Workspace& live,const std::string& document_id,const std::string& body_id) {
    const auto* state=live.open_part(document_id);if(!state)invalid();
    const auto& session=state->session;auto sources=InputReader(session).read(body_id);
    if(sources.state!=CamResultState::Current)invalid();
    CamCalculationInput result;result.document_id=document_id;result.body_id=body_id;result.runtime_identity=state->runtime_identity;
    result.revision=session.revision();result.generation=session.data_generation();
    result.definition=*session.document().body_history.find(body_id)->cam;result.definition.calculated.reset();
    result.intent_key=document::cam_intent_key(result.definition);result.material=std::move(sources.material);
    result.clamping_sketches=std::move(sources.sketches);result.source_keys=std::move(sources.keys);return result;
}
bool publish_cam_calculation(Workspace& live,const CamCalculationInput& input,document::CamCalculated result) {
    auto* state=live.open_part(input.document_id);
    if(!state||state->runtime_identity!=input.runtime_identity||state->session.revision()!=input.revision||
            state->session.data_generation()!=input.generation)return false;
    auto current=prepare_cam_calculation(live,input.document_id,input.body_id);
    if(current.intent_key!=input.intent_key||current.source_keys!=input.source_keys)return false;
    if(result.intent_key!=input.intent_key||result.sources!=input.source_keys||result.machine!=input.definition.machine||
       result.tools!=input.definition.tools||result.machine_work_origin!=input.definition.machine_work_origin||
       result.work_frame!=input.definition.work_frame||result.tolerance_mm!=input.definition.tolerance_mm)invalid();
    document::validate_cam_calculated(result);
    auto next=state->session.document();auto body=*next.body_history.find(input.body_id);
    if(body.cam->calculated&&*body.cam->calculated==result)return false;
    body.cam->calculated=std::make_shared<const document::CamCalculated>(std::move(result));
    auto graph=next.body_history;graph.update_body(std::move(body));next.set_body_history(std::move(graph));
    state->session.commit_metadata(std::move(next));return true;
}
} // namespace zima::workspace

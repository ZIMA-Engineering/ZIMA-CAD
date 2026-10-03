#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <numbers>

using namespace zima::kernel;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){require(std::isfinite(a)&&std::abs(a-b)<1e-5,"Unexpected state volume");}
RevolutionRequest source() {
    RevolutionRequest r;
    r.outer_profile=ExtrusionRequest::PolygonProfile{{{100,0,0},{120,0,0},{120,4,0},{104,4,0},{104,20,0},{100,20,0}}};
    r.profile_region_id="section";r.outer_boundary_id="outline";
    r.outer_edge_source_ids={"c0","c1","c2","c3","c4","c5"};
    r.outer_vertex_source_ids={"p0","p1","p2","p3","p4","p5"};
    r.axis_direction={0,1,0};r.angle_degrees=90;return r;
}
void ancestry() {
    for(const std::string owner:{"source","source:with:colons"})for(const std::string key:{"generated:p0","key:with:12:colons"}) {
        const auto parent=solid_state_parent(solid_state_child_key(owner,key));
        require(parent&&parent->first==owner&&parent->second==key,"Ancestry failed to round-trip");
    }
    for(const std::string key:{"face:0","solid-state:parent:1:x:1:y:extra", "solid-state:parent:999999999999999999999:x", "solid-state:parent:0::1:x"})
        require(!solid_state_parent(key),"Malformed ancestry accepted");
}
void curved_chain(bool sweep) {
    RevolutionRequest root;
    root.outer_profile=ExtrusionRequest::PolygonProfile{{{9,-1,0},{11,-1,0},{11,1,0},{9,1,0}}};
    root.profile_region_id="section";root.outer_boundary_id="outline";
    root.outer_edge_source_ids={"a","b","c","d"};root.outer_vertex_source_ids={"p0","p1","p2","p3"};
    root.axis_direction={0,1,0};root.angle_degrees=90;
    auto child=root;
    child.outer_profile=ExtrusionRequest::PolygonProfile{{{0,-.5,-10.5},{0,.5,-10.5},{0,.5,-9.5},{0,-.5,-9.5}}};
    child.axis_point={0,0,-20};child.axis_direction={0,-1,0};child.profile_normal={-1,0,0};
    ExtrusionRequest tip;
    tip.outer_profile=ExtrusionRequest::PolygonProfile{{{-10.3,-.3,-20},{-9.7,-.3,-20},{-9.7,.3,-20},{-10.3,.3,-20}}};
    tip.profile_region_id="section";tip.outer_boundary_id="outline";
    tip.outer_edge_source_ids=root.outer_edge_source_ids;tip.outer_vertex_source_ids=root.outer_vertex_source_ids;
    tip.direction={0,0,-3};
    OcctKernel kernel;std::vector<HistoryOperation> history{{"root",root},{"child",child},{"tip",tip}};
    if(sweep) {
        Sweep3DRequest path;path.path_points={{0,0,-10},{-10,0,-20}};path.path_point_ids={"s0","s1"};
        path.path_segments.push_back({"arc",path.path_points.front(),path.path_points.back(),{},Vec3{-std::sqrt(50.),0,-20+std::sqrt(50.)}});
        Sweep3DRequest::Section section;section.profile_id="section";section.point_id="s0";section.profile_normal={-1,0,0};
        section.profile.region_id="section";section.profile.outer_boundary_id="outline";section.profile.outer_profile=child.outer_profile;
        section.profile.outer_edge_source_ids=child.outer_edge_source_ids;section.profile.outer_vertex_source_ids=child.outer_vertex_source_ids;
        path.sections={section};path.attachment_endpoints=true;path.linear_tolerance=1e-5;
        history[1].primitive=path;
    }
    const auto authored=kernel.evaluate_history(history).back();
    const auto fingerprint=history_fingerprint(history,history.size());
    for(const std::string selected:{"root","child"}) {
        history.resize(3);history.push_back({"individual",SolidStateRequest{false,false,.9,{selected}}});
        history.back().solid_state_face_transfers={{"child","root"},{"tip","child"}};
        const auto result=kernel.evaluate_history(history).back();
        require(result.calculation_errors.empty(),"Individual curved-chain selection failed");
        near(result.volume,(selected=="root"?23:24.5)*std::numbers::pi+1.08);
        const auto& points=result.solid_state_reference_views.at("individual")->points;
        std::size_t tip_count=0;
        for(const auto& original:authored.mesh.original_references.points) {
            if(original.reference.owner_id!="tip"&&(selected!="root"||original.reference.owner_id!="child"))continue;
            const auto point=std::ranges::find_if(points,[&](const auto& p){return p.reference==original.reference;});
            require(point!=points.end(),"Individual state lost a carried vertex");
            const auto p=original.position;
            if(selected=="root") {near(point->position.x,-p.z);near(point->position.z,p.x-4.5*std::numbers::pi);}
            else {near(point->position.x,p.z+20-4.5*std::numbers::pi);near(point->position.z,-p.x-20);}
            near(point->position.y,p.y);if(original.reference.owner_id=="tip")++tip_count;
        }
        require(tip_count>=8,"Individual curved-chain check has no tip vertices");
    }
    for(double coefficient:{.9,1.1}) {
        history.resize(3);history.push_back({"straight",SolidStateRequest{false,true,coefficient,{}}});
        history.back().solid_state_face_transfers={{"child","root"},{"tip","child"}};
        const auto result=kernel.evaluate_history(history).back();
        require(result.calculation_errors.empty(),"Curved continuation failed to straighten");
        near(result.volume,25*std::numbers::pi*coefficient+1.08);
        const auto& packet=*result.solid_state_reference_views.at("straight");
        std::size_t child_count=0;
        for(const auto& original:authored.mesh.original_references.points)if(original.reference.owner_id=="child") {
            const auto point=std::ranges::find_if(packet.points,[&](const auto& item){return item.reference==original.reference;});
            require(point!=packet.points.end(),"Curved continuation lost its semantic vertex");
            const auto p=original.position;const bool start=std::abs(p.x)<1e-7;
            require(start||std::abs(p.z+20)<1e-7,"Curved fixture has a point outside both end sections");
            near(point->position.x,start?-p.z:p.x+20);near(point->position.y,p.y);
            near(point->position.z,-5*std::numbers::pi*coefficient*(start?1:2));++child_count;
        }
        require(child_count>=8,"Straight curved-chain check has no section vertices");
        std::size_t tip_count=0;
        for(const auto& point:packet.points)if(point.reference.owner_id=="tip") {
            const auto original=std::ranges::find_if(authored.mesh.original_references.points,[&](const auto& p){return p.reference==point.reference;});
            require(original!=authored.mesh.original_references.points.end(),"Tip lost its authored vertex");
            near(point.position.x,original->position.x+20);near(point.position.y,original->position.y);
            near(point.position.z,original->position.z+20-10*std::numbers::pi*coefficient);
            ++tip_count;
        }
        require(tip_count>=8,"Straight curved-chain check has no tip vertices");
        history.push_back({"restore",SolidStateRequest{true,true,1,{}}});
        history.back().solid_state_face_transfers={{"child","root"},{"tip","child"}};
        const auto restored=kernel.evaluate_history(history).back();near(restored.volume,authored.volume);
        const auto& points=restored.solid_state_reference_views.at("restore")->points;
        for(const auto& original:authored.mesh.original_references.points) {
            const auto point=std::ranges::find_if(points,[&](const auto& p){return p.reference==original.reference;});
            require(point!=points.end(),"Restored curved chain lost its semantic vertex");
            near(point->position.x,original.position.x);near(point->position.y,original.position.y);near(point->position.z,original.position.z);
        }
        require(history_fingerprint(history,3)==fingerprint,"Curved transfer changed authored operations");
    }
}
}
int main(){try {
    ancestry();curved_chain(false);curved_chain(true);OcctKernel kernel;
    const auto original=source();
    const double area=144, radius=100+(80*10+64*2)/144.;
    const double volume=area*radius*std::numbers::pi/2;
    std::vector<HistoryOperation> history{{"source",original},
        {"straight",SolidStateRequest{false,true,.9,{}}}};
    auto straight=kernel.evaluate_history(history);
    near(straight[0].volume,volume);near(straight.back().volume,volume*.9);
    require(straight.front().solid_state_reference_views.empty(),"State evaluation leaked before its boundary");
    const auto& alias=*straight.back().solid_state_reference_views.at("straight");
    const auto endpoint=std::ranges::find_if(alias.points,[](const auto& p){return p.reference.owner_id=="source"&&p.reference.semantic_key=="end:p0";});
    require(endpoint!=alias.points.end(),"State evaluation lost complete source end point");
    const auto original_end=std::ranges::find_if(straight.back().mesh.original_references.points,
        [](const auto& p){return p.reference.owner_id=="source"&&p.reference.semantic_key=="end:p0";});
    require(original_end!=straight.back().mesh.original_references.points.end()&&original_end->position!=endpoint->position,
        "State evaluation overwrote selectable original reference geometry");
    bool owned=false,original_retained=false;
    for(const auto& face:straight.back().mesh.original_references.triangle_references) {
        if(face.owner_id=="straight") {
            owned=true;const auto parent=solid_state_parent(face.semantic_key);
            require(parent&&parent->first=="source","State topology has no authored parent");
        }
        if(face.owner_id=="source")original_retained=true;
    }
    require(owned&&original_retained,"State did not retain independent original and derived topology");
    auto changed=history;std::get<SolidStateRequest>(changed.back().primitive).coefficient=1.1;
    require(history_fingerprint(history,2)!=history_fingerprint(changed,2),"Coefficient absent from cache key");
    near(kernel.evaluate_history_incremental(changed,straight).back().volume,volume*1.1);
    const FilletRequest fillet{{{"straight",solid_state_child_key("source","generated:p0")}},1};
    history.push_back({"fillet",fillet});
    const auto treated=kernel.evaluate_history(history);
    require(treated.back().solid_state_reference_views.contains("straight"),"Later Fillet dropped the state evaluation packet");
    require(treated.back().volume<straight.back().volume,"Fillet added after state was not evaluated");
    history.push_back({"restore",SolidStateRequest{true,true,1,{}}});
    const auto restored=kernel.evaluate_history(history);
    const auto expected=kernel.evaluate_history({{"source",original},{"fillet",FilletRequest{{{"source","generated:p0"}},1}}});
    near(restored.back().volume,expected.back().volume);
    require(restored.back().volume<volume,"Restore discarded the later Fillet");
    OcctKernel cold;near(cold.evaluate_history(history).back().volume,restored.back().volume);
    history.push_back({"straight-again",SolidStateRequest{false,true,1.1,{}}});
    const auto repeated=kernel.evaluate_history(history);
    require(repeated.back().volume>volume,"Repeated state accumulated the earlier coefficient");
    auto suppressed=history;suppressed.back().suppressed=true;
    near(kernel.evaluate_history_incremental(suppressed,repeated).back().volume,restored.back().volume);
    auto bad=history;std::get<SolidStateRequest>(bad.back().primitive).coefficient=0;
    bool rejected=false;try{kernel.evaluate_history(bad);}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid state coefficient accepted");
    near(kernel.evaluate_history(history).back().volume,repeated.back().volume);
    // Body-local replay must not include another Body's solid or its state.
    auto scoped=changed;for(auto& op:scoped)op.body.id="body-one";
    auto other=HistoryOperation{"other",original};other.body.id="body-two";other.body.translation={500,0,0};
    scoped.push_back(other);
    near(kernel.evaluate_history(scoped).back().volume,volume*2.1);
    auto placed=std::vector<HistoryOperation>{{"source",original},{"straight",SolidStateRequest{false,true,.9,{}}}};
    for(auto& op:placed){op.body.id="placed";op.body.translation={17,23,31};op.body.rotation_degrees={0,0,90};}
    const auto world=kernel.evaluate_history(placed);
    const auto& packet=*world.back().solid_state_reference_views.at("straight");
    const auto moved=std::ranges::find_if(packet.points,[](const auto& p){return p.reference.owner_id=="source"&&p.reference.semantic_key=="end:p0";});
    require(moved!=packet.points.end(),"Placed Body lost evaluation packet");
    near(moved->position.x,17-endpoint->position.y);near(moved->position.y,23+endpoint->position.x);near(moved->position.z,31+endpoint->position.z);
    std::cout<<"Solid state kernel replay, ancestry, later Fillet, cold calculation and Body isolation passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/solid_straightening.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <zima/kernel/state_display.hpp>
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
void state_display() {
    auto request=source();request.centerlines.origin_enabled=true;
    request.centerlines.origin={110,10,0};request.centerlines.normal={0,0,1};
    std::vector<HistoryOperation> history{{"source",request},
        {"straight",SolidStateRequest{false,true,.9,{}}},
        {"restore",SolidStateRequest{true,true,1,{}}},
        {"straight-again",SolidStateRequest{false,true,1.1,{}}}};
    OcctKernel kernel;const auto calculated=kernel.evaluate_history(history);
    for(std::size_t i=1;i<calculated.size();++i) {
        const auto& body=calculated[i];const auto& packet=*body.solid_state_reference_views.at(history[i].owner_id);
        require(!packet.axes.empty(),"State fixture has no calculated centerline axes");
        require(body.mesh.axes.size()==packet.axes.size(),"Solid state displays stale or missing axes");
        for(const auto& axis:packet.axes) {
            const AxisReference expected{history[i].owner_id,solid_state_child_key(axis.reference.owner_id,axis.reference.semantic_key),{}};
            const auto found=std::ranges::find(body.mesh.axes,expected,&ViewerAxis::reference);
            require(found!=body.mesh.axes.end()&&found->point==axis.point&&found->direction==axis.direction&&
                found->display_length==axis.display_length,"Solid state axis differs from calculated geometry");
            require(std::ranges::find(calculated.back().mesh.original_references.axes,expected,&ViewerAxis::reference)!=calculated.back().mesh.original_references.axes.end(),
                "Displayed state axis has no persisted reference");
        }
        require(std::ranges::none_of(body.mesh.axes,[](const auto& axis){return axis.reference.owner_id=="source";}),
            "Solid state retains an authored axis in ordinary display");
        for(const auto& edge:packet.edges)if(edge.reference.semantic_key.starts_with("centerline:from:")) {
            const EdgeReference expected{history[i].owner_id,solid_state_child_key(edge.reference.owner_id,edge.reference.semantic_key),{}};
            const auto found=std::ranges::find(body.mesh.edges,expected,&ViewerEdge::reference);
            require(found!=body.mesh.edges.end()&&found->points==edge.points,"State centerline differs from calculated geometry");
        }
        auto display=body.mesh;associate_solid_state_display(display);
        require(std::ranges::all_of(display.points,[](const auto& point){return point.always_visible||!point.label.empty()||point.display_owner_id.empty();}),
            "Solid topology vertices participate in whole-container marker highlighting");
        for(const auto& original:calculated.front().mesh.axes) {
            const auto retained=std::ranges::find(calculated.back().mesh.original_references.axes,original.reference,&ViewerAxis::reference);
            require(retained!=calculated.back().mesh.original_references.axes.end()&&retained->point==original.point&&retained->direction==original.direction,
                "State display overwrote the authored axis reference");
        }
    }
    const auto reused=kernel.evaluate_history_incremental(history,calculated);
    require(reused.back().mesh.axes.size()==calculated.back().mesh.axes.size(),"Reused state duplicated displayed axes");
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
        // Fitted rotation-axis grips are datums, not vertices of an end section.
        // Straightening replaces the rotation primitive; Restore checks all datums below.
        for(const auto& original:authored.mesh.original_references.points)if(original.reference.owner_id=="child"&&
            original.reference.semantic_key.find(":from:axis:")==std::string::npos) {
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
void combined_side_variants() {
    OcctKernel kernel;
    unsigned cases=0;
    // None, Extrusion, Revolution on each side; all curved combinations.
    for(int first=0;first<3;++first)for(int second=0;second<3;++second) {
        if(first!=2&&second!=2)continue;
        for(int variant=0;variant<6;++variant)
        for(double normal_sign:{-1.,1.})for(double axis_sign:{-1.,1.})for(double radial_sign:{-1.,1.}) {
            try {
            FeatureGroupRequest group;
            std::vector<ExtrusionRequest> expected;
            for(int side=0;side<2;++side) {
                const int mode=side?second:first;if(!mode)continue;
                auto profile=source();profile.angle_degrees=45;
                double radius=100+(80*10.+64*2)/144.;
                if(variant==1||variant==2||variant==3||variant==4) {
                    profile.outer_profile=ExtrusionRequest::CircleProfile{{110,10,0},4};
                    profile.outer_edge_source_ids={"circle"};profile.outer_vertex_source_ids={"circle-point"};radius=110;
                }
                if(variant==2) {
                    profile.inner_profiles={ExtrusionRequest::CircleProfile{{111,10,0},1}};
                    profile.inner_boundary_ids={"hole"};profile.inner_edge_source_ids={{"hole-circle"}};
                    profile.inner_vertex_source_ids={{"hole-point"}};
                    radius=(16*110.-111)/15.;
                }
                if(variant==3)profile.wall=ProfileWall{-1,1,{}};
                if(variant==5)profile.angle_degrees=first==2&&second==2?180:360;
                profile.profile_normal={0,0,normal_sign};profile.axis_direction={0,axis_sign,0};
                const auto mirror=[](auto& loop){std::visit([](auto& p) {
                    if constexpr(requires{p.vertices;})for(auto& v:p.vertices)v.x=-v.x;
                    else if constexpr(requires{p.center;})p.center.x=-p.center.x;
                },loop);};
                if(radial_sign<0){mirror(profile.outer_profile);for(auto& hole:profile.inner_profiles)mirror(hole);}
                profile.first_cap_is_start=side==0;
                auto scoped=feature_side_request(profile,"combined-feature",side?FeatureSide::Start:FeatureSide::End);
                ExtrusionRequest straight;
                straight.outer_profile=scoped.outer_profile;straight.profile_region_id=scoped.profile_region_id;
                straight.outer_boundary_id=scoped.outer_boundary_id;
                straight.outer_edge_source_ids=scoped.outer_edge_source_ids;straight.outer_vertex_source_ids=scoped.outer_vertex_source_ids;
                straight.inner_profiles=scoped.inner_profiles;straight.inner_boundary_ids=scoped.inner_boundary_ids;
                straight.inner_edge_source_ids=scoped.inner_edge_source_ids;straight.inner_vertex_source_ids=scoped.inner_vertex_source_ids;
                straight.wall=scoped.wall;
                if(variant==4&&mode==1)straight.draft_angle_degrees=2;
                straight.first_cap_is_start=scoped.first_cap_is_start;
                const double length=mode==1?60.:radius*std::numbers::pi*profile.angle_degrees/180;
                straight.direction={0,0,normal_sign*(side?-1:1)*length};
                expected.push_back(straight);
                if(mode==1)group.children.push_back(straight);else group.children.push_back(scoped);
            }
            const auto plan=kernel.prepare_straightening(group);
            const auto& actual=std::get<FeatureGroupRequest>(plan.primitive);
            require(actual.children.size()==expected.size(),"Straightening changed the side count");
            for(std::size_t index=0;index<expected.size();++index) {
                const auto& child=std::get<ExtrusionRequest>(actual.children[index]);
                near(child.direction.x,expected[index].direction.x);near(child.direction.y,expected[index].direction.y);
                near(child.direction.z,expected[index].direction.z);
                require(child.profile_region_id==expected[index].profile_region_id&&child.draft_angle_degrees==expected[index].draft_angle_degrees&&
                    child.outer_vertex_source_ids==expected[index].outer_vertex_source_ids&&child.first_cap_is_start==expected[index].first_cap_is_start,
                    "Straightening moved the shared Sketch or changed the side identity");
            }
            FeatureGroupRequest independent;for(const auto& child:expected)independent.children.push_back(child);
            const auto reference=kernel.evaluate_history({{"combined",independent}}).back();
            const std::vector<HistoryOperation> operations{{"combined",group},{"straight",SolidStateRequest{false,true,1,{}}},
                {"restore",SolidStateRequest{true,true,1,{}}}};
            const auto fingerprints=history_fingerprints(operations);
            for(std::size_t prefix=0;prefix<fingerprints.size();++prefix)
                require(fingerprints[prefix]==history_fingerprint(operations,prefix),"Combined state batch fingerprint differs");
            const auto result=kernel.evaluate_history(operations);
            near(result[1].volume,reference.volume);near(result[1].surface_area,reference.surface_area);
            near(result.back().volume,result.front().volume);
            const auto& packet=*result[1].solid_state_reference_views.at("straight");
            for(const auto& point:reference.mesh.original_references.points) {
                const auto found=std::ranges::find_if(packet.points,[&](const auto& item){return item.reference==point.reference;});
                require(found!=packet.points.end(),"Straightening lost a side point");
                near(found->position.x,point.position.x);near(found->position.y,point.position.y);near(found->position.z,point.position.z);
            }
            ++cases;
            } catch(const std::exception& error) {
                throw std::runtime_error("Combined sides "+std::to_string(first)+"/"+std::to_string(second)+
                    ", profile "+std::to_string(variant)+", normal "+std::to_string(normal_sign)+
                    ", axis "+std::to_string(axis_sign)+", radial "+std::to_string(radial_sign)+": "+error.what());
            }
        }
    }
    std::cout<<cases<<" combined-side cases: shared Sketch, direction, endpoint identities, shape and Restore passed\n";
}
}
int main(int argc,char** argv){try {
    if(argc==2&&std::string_view(argv[1])=="--display") {
        state_display();std::cout<<"Solid state axes, centerlines, ancestry and vertex display passed\n";return 0;
    }
    combined_side_variants();
    {
        OcctKernel twist_kernel;
        Sweep3DRequest twist;twist.twist=Sweep3DRequest::Twist{100,90,true};
        Sweep3DRequest::Section section;section.profile_id="L-sketch";section.point_id="twist:path:start";
        section.profile.region_id="L-region";section.profile.outer_boundary_id="L-outline";
        section.profile.outer_profile=ExtrusionRequest::PolygonProfile{{{0,0,0},{20,0,0},{20,4,0},{4,4,0},{4,20,0},{0,20,0}}};
        section.profile.outer_edge_source_ids={"c0","c1","c2","c3","c4","c5"};
        section.profile.outer_vertex_source_ids={"p0","p1","p2","p3","p4","p5"};
        twist.sections={section};
        for(bool smooth:{false,true})for(double angle:{90.,-90.}) {
            twist.twist->smooth=smooth;twist.twist->angle_degrees=angle;
            const auto plan=twist_kernel.prepare_straightening(twist,.9);
            near(plan.section_area,144);near(plan.source_start_centroid.x,(80*10.+64*2)/144.);
            near(plan.source_start_centroid.y,(80*2.+64*12)/144.);
            near(plan.source_end_centroid.x,plan.source_start_centroid.x);
            near(plan.source_end_centroid.y,plan.source_start_centroid.y);
            FeatureGroupRequest group;group.children={twist};
            const std::vector<HistoryOperation> history{{"twist",group},{"straight",SolidStateRequest{false,true,.9,{}}},
                {"restore",SolidStateRequest{true,true,1,{}}}};
            const auto result=twist_kernel.evaluate_history(history);
            require(result.back().calculation_errors.empty(),"Twist state history failed");
            near(result[1].volume,144*90.);
            require(std::abs(result.front().volume-144*100.)<2,"Twist changed section volume");
            near(result.back().volume,result.front().volume);
            require(result[1].solid_state_reference_views.contains("straight"),"Twist lost state references");
        }
        twist.sections.front().profile.inner_profiles={ExtrusionRequest::CircleProfile{{10,2,0},1}};
        twist.sections.front().profile.inner_boundary_ids={"hole"};
        twist.sections.front().profile.inner_edge_source_ids={{"hole-circle"}};
        twist.sections.front().profile.inner_vertex_source_ids={{"hole-point"}};
        const auto hollow=twist_kernel.prepare_straightening(twist);
        const double hollow_area=144-std::numbers::pi;
        near(hollow.section_area,hollow_area);
        near(hollow.source_start_centroid.x,(80*10.+64*2-10*std::numbers::pi)/hollow_area);
        near(hollow.source_start_centroid.y,(80*2.+64*12-2*std::numbers::pi)/hollow_area);
        FeatureGroupRequest hollow_group;hollow_group.children={twist};
        const auto hollow_result=twist_kernel.evaluate_history({{"hollow",hollow_group},
            {"hollow-straight",SolidStateRequest{false,true,1,{}}},{"hollow-restore",SolidStateRequest{true,true,1,{}}}});
        near(hollow_result[1].volume,hollow_area*100);
        near(hollow_result.back().volume,hollow_result.front().volume);
    }
    ancestry();state_display();curved_chain(false);curved_chain(true);OcctKernel kernel;
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

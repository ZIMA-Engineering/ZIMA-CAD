#include <zima/kernel/profile_centerlines.hpp>
#include <zima/kernel/feature_side_identity.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/profile_serialization.hpp>
#include <zima/document/feature_rotation_span.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <set>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){if(!std::isfinite(a)||std::abs(a-b)>1e-7)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
void near(kernel::Vec3 a,kernel::Vec3 b){near(a.x,b.x);near(a.y,b.y);near(a.z,b.z);}
std::set<std::string> identities(const kernel::ViewerReferenceGeometry& refs){std::set<std::string> result;for(const auto& a:refs.axes)if(a.reference.semantic_key.starts_with("centerline:from:"))result.insert(a.reference.semantic_key);for(const auto& e:refs.edges)if(e.reference.semantic_key.starts_with("centerline:from:"))result.insert(e.reference.semantic_key);for(const auto& p:refs.points)if(p.reference.semantic_key.starts_with("profile:path-point:"))result.insert(p.reference.semantic_key);return result;}
document::PartDocument fixture(bool revolve){
 auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(5,0,15,8));
 auto feature=revolve?document::PartDocument::create_revolution_container(sketch.id):document::PartDocument::create_extrusion_container(sketch.id);
 if(revolve){const auto axis=sketch.add_segment(0,-2,0,10);sketch.set_segment_centerline(axis,true);feature.revolution.axis_segment_id=axis;feature.revolution.angle_degrees=90;feature.revolution.origin_centerline=feature.revolution.centroid_centerline=true;}
 else feature.extrusion.origin_centerline=feature.extrusion.centroid_centerline=true;
 sketch.owner_container_id=feature.id;auto part=document::PartDocument::create_default();part.history={feature};part.sketches={sketch};
 document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Centerlines"));graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();return part;
}
}
void grouped_centerlines(const kernel::OcctKernel& kernel) {
    const auto extruded=fixture(false).kernel_operations().front();
    const auto revolved=fixture(true).kernel_operations().front();
    kernel::FeatureGroupRequest group;
    group.children.push_back(std::get<kernel::ExtrusionRequest>(extruded.primitive));
    group.children.push_back(std::get<kernel::RevolutionRequest>(revolved.primitive));
    auto expected=identities(kernel.evaluate_history({extruded}).back().mesh.original_references);
    const auto rotated=identities(kernel.evaluate_history({revolved}).back().mesh.original_references);
    expected.insert(rotated.begin(),rotated.end());
    kernel::HistoryOperation combined=extruded;
    combined.primitive=group;
    const auto check_group=[&] {
        const auto bodies=kernel.evaluate_history({combined});
        check(identities(bodies.back().mesh.original_references)==expected,
            "Combining Extrusion and Revolution lost authored centerline references");
        return bodies.back().volume;
    };
    const double volume=check_group();
    std::reverse(group.children.begin(),group.children.end());
    combined.primitive=group;
    near(check_group(),volume);
}
void coincident_revolution_endpoints() {
    auto part=fixture(true);
    auto request=std::get<kernel::RevolutionRequest>(part.kernel_operations().front().primitive);
    request.centerlines.centroid_enabled=false;
    request.centerlines.origin=request.axis_point;
    for(const bool forward:{true,false}) {
        request.first_cap_is_start=forward;
        const auto refs=kernel::profile_centerlines::revolution(request,"feature");
        check(refs.points.size()==2,"On-axis rotation must retain both authored endpoints");
        check(refs.points[0].reference!=refs.points[1].reference,
            "Coincident automatic-axis endpoints have the same identity");
        check(refs.points[0].reference.semantic_key.starts_with("profile:path-point:start:"),
            "On-axis Start identity was replaced with a center identity");
        check(refs.points[1].reference.semantic_key.starts_with("profile:path-point:end:"),
            "On-axis End identity was replaced with a center identity");
        near(refs.points[0].position,request.axis_point);
        near(refs.points[1].position,request.axis_point);
        for(const auto& point:refs.points) {
            auto follower=document::PartDocument::create_construction(document::ConstructionKind::Point);
            follower.definition=document::ConstructionDefinition::PointReference;
            follower.references={{"",point.reference.owner_id,point.reference.semantic_key}};
            check(document::resolve_construction(follower,refs),
                "A follower cannot distinguish coincident Start/End references");
            near(follower.origin,point.position);
        }
    }
}
void extrusion_identity_matrix(const kernel::OcctKernel& kernel) {
    const auto source=fixture(false);
    std::set<std::string> baseline;
    for(const auto result:{document::ProfileResultType::Solid,
                          document::ProfileResultType::Thin,
                          document::ProfileResultType::Surface})
    for(const auto extent:{document::ProfileExtentMode::OneSide,
                          document::ProfileExtentMode::TwoSides,
                          document::ProfileExtentMode::Symmetric})
    for(const auto direction:{document::ExtrusionDirection::Forward,
                             document::ExtrusionDirection::Reverse}) {
        auto part=source;
        auto& p=part.history.front().extrusion;
        p.result_type=result;p.extent_mode=extent;p.direction=direction;
        p.length_forward=17;p.length_reverse=9;p.thin_thickness=.5;
        const auto operations=part.kernel_operations();
        const auto bodies=kernel.evaluate_history(operations);
        const auto& refs=bodies.back().mesh.original_references;
        if(baseline.empty())baseline=identities(refs);
        check(identities(refs)==baseline,"Extrusion mode changed automatic-axis identity");
        const auto& request=std::get<kernel::ExtrusionRequest>(operations.front().primitive);
        const auto seeds=kernel::profile_centerlines::seeds(request);
        const auto vector=request.direction;
        const double length=std::sqrt(vector.x*vector.x+vector.y*vector.y+vector.z*vector.z);
        const kernel::Vec3 unit{vector.x/length,vector.y/length,vector.z/length};
        for(const auto& [key,seed]:seeds)for(const bool start:{true,false}) {
            const auto semantic="profile:path-point:"+std::string(start?"start:from:":"end:from:")+key;
            const auto point=std::ranges::find_if(refs.points,[&](const auto& item){return item.reference.semantic_key==semantic;});
            check(point!=refs.points.end(),"Extrusion mode lost an authored endpoint");
            const double t=request.start_offset+(start==request.first_cap_is_start?0:length);
            near(point->position,{seed.x+t*unit.x,seed.y+t*unit.y,seed.z+t*unit.z});
            auto follower=document::PartDocument::create_construction(document::ConstructionKind::Point);
            follower.definition=document::ConstructionDefinition::PointReference;
            follower.references={{"",point->reference.owner_id,semantic}};
            check(document::resolve_construction(follower,refs),"Extrusion endpoint rejected a dependent reference");
            near(follower.origin,point->position);
        }
        std::vector<kernel::BodyResult> reopened;
        static_cast<void>(document::PartDocument::from_serialized(part.serialized(bodies),&reopened));
        check(identities(reopened.back().mesh.original_references)==baseline,
            "Extrusion mode lost centerline identities on reopen");
    }
}
void limited_centerline_matrix(const kernel::OcctKernel& kernel) {
    using E=kernel::ExtrusionRequest;
    auto operations=fixture(false).kernel_operations();
    auto& request=std::get<E>(operations.front().primitive);
    const auto expected=identities(kernel.evaluate_history(operations).back().mesh.original_references);
    request.target_face={"upper-datum","plane",{}};
    request.target_is_datum=true;
    request.target_plane_origin={0,0,20};request.target_plane_normal={-.2,0,1};
    request.extent=E::Extent::UpToPlane;
    for(const bool reverse:{false,true})
    for(const bool thin:{false,true}) {
        request.wall=thin?std::optional<kernel::ProfileWall>{{-.25,.25,{}}}:std::nullopt;
        request.reverse_limit=reverse?std::optional<kernel::ExtrusionLimit>{
            {true,{"lower-datum","plane",{}},true,{0,0,-8},{-.1,0,1},{}}}:std::nullopt;
        const auto bodies=kernel.evaluate_history(operations);
        const auto& refs=bodies.back().mesh.original_references;
        check(identities(refs)==expected,"Reference limit changed automatic-axis identity");
        for(const auto& point:refs.points) {
            if(point.reference.semantic_key.starts_with("profile:path-point:end:"))
                near(point.position.z,20+.2*point.position.x);
            if(point.reference.semantic_key.starts_with("profile:path-point:start:"))
                near(point.position.z,reverse?-8+.1*point.position.x:0);
        }
    }
}
void stationary_references(const kernel::OcctKernel& kernel) {
    for(const bool rotated:{false,true}) {
        const auto part=fixture(rotated);
        const auto operations=part.kernel_operations();
        const auto owner=operations.front().owner_id;
        const auto calculated=kernel.evaluate_history(operations);
        const auto& active=calculated.back().mesh.original_references;
        const auto stationary=rotated
            ?kernel::profile_centerlines::stationary(std::get<kernel::RevolutionRequest>(operations.front().primitive),owner)
            :kernel::profile_centerlines::stationary(std::get<kernel::ExtrusionRequest>(operations.front().primitive),owner);
        check(stationary.axes.size()==2&&stationary.points.size()==4,
            "A stationary profile needs two automatic datums and four endpoint identities");
        for(const auto& target:stationary.points) {
            const auto original=std::ranges::find_if(active.points,[&](const auto& point){return point.reference==target.reference;});
            check(original!=active.points.end(),"Disabling an operation changed endpoint identity");
            auto follower=document::PartDocument::create_construction(document::ConstructionKind::Point);
            follower.definition=document::ConstructionDefinition::PointReference;
            follower.references={{"",target.reference.owner_id,target.reference.semantic_key}};
            const auto authored=follower.references;
            check(document::resolve_construction(follower,active),"Active endpoint reference failed");
            near(follower.origin,original->position);
            check(document::resolve_construction(follower,stationary),"Stationary endpoint reference failed");
            near(follower.origin,target.position);
            check(follower.references==authored,"Disabling an operation rewrote its follower reference");
            check(document::resolve_construction(follower,active),"Restoring an operation broke its follower");
            near(follower.origin,original->position);
        }
        for(const auto& target:stationary.axes) {
            auto follower=document::PartDocument::create_construction(document::ConstructionKind::Axis);
            follower.definition=document::ConstructionDefinition::AxisReference;
            follower.references={{"",target.reference.owner_id,target.reference.semantic_key}};
            check(document::resolve_construction(follower,stationary),"Stationary automatic axis cannot be referenced");
        }
        const auto seeds=rotated
            ?kernel::profile_centerlines::seeds(std::get<kernel::RevolutionRequest>(operations.front().primitive))
            :kernel::profile_centerlines::seeds(std::get<kernel::ExtrusionRequest>(operations.front().primitive));
        for(const auto& [key,position]:seeds)for(const auto& point:stationary.points)
            if(point.reference.semantic_key.ends_with(key))near(point.position,position);
    }
}

void mirrored_feature_limit(const kernel::OcctKernel& kernel) {
    auto operation=fixture(false).kernel_operations().front();
    auto forward=std::get<kernel::ExtrusionRequest>(operation.primitive);
    forward.extent=kernel::ExtrusionRequest::Extent::UpToPlane;
    forward.target_face={"datum","plane",{}};forward.target_is_datum=true;
    forward.target_plane_origin={0,0,20};forward.target_plane_normal={-.2,0,1};
    auto reverse=forward;reverse.direction={-forward.direction.x,-forward.direction.y,-forward.direction.z};
    reverse.first_cap_is_start=false;reverse.mirror_forward_limit=true;
    kernel::FeatureGroupRequest group;
    group.children={kernel::feature_side_request(forward,"feature",kernel::FeatureSide::End),
        kernel::feature_side_request(reverse,"feature",kernel::FeatureSide::Start)};
    operation.primitive=group;
    const auto bodies=kernel.evaluate_history({operation});near(bodies.back().volume,3520);
    std::size_t positive{},negative{};
    for(const auto& point:bodies.back().mesh.original_references.points) {
        const auto& key=point.reference.semantic_key;
        if(!key.starts_with("profile:path-point:"))continue;
        if(point.position.z>1e-8){near(point.position.z,20+.2*point.position.x);++positive;}
        if(point.position.z<-1e-8){near(point.position.z,-20-.2*point.position.x);++negative;}
    }
    check(positive==2&&negative==2,"Mirrored Feature target did not preserve both automatic paths");
    const auto keys=identities(bodies.back().mesh.original_references);
    std::reverse(group.children.begin(),group.children.end());operation.primitive=group;
    const auto swapped=kernel.evaluate_history({operation});near(swapped.back().volume,3520);
    check(keys==identities(swapped.back().mesh.original_references),"Mirrored Feature limit depends on child evaluation order");
}
void feature_side_ancestry(const kernel::OcctKernel& kernel) {
    for(const auto side:{kernel::FeatureSide::Start,kernel::FeatureSide::End}) {
        const kernel::FeatureSideParent expected{"feature:with:colons",side,"sketch:point:with:colons"};
        const auto key=kernel::feature_side_parent_key(expected);
        const auto recovered=kernel::feature_side_parent(key);
        check(recovered&&*recovered==expected,"Side ancestry cannot recover its exact source parent");
        check(!kernel::feature_side_parent(key+":trailing"),"Side ancestry accepted trailing data");
        check(!kernel::feature_side_parent(key.substr(0,key.size()-1)),"Side ancestry accepted a truncated parent");
    }
    for(const auto* malformed:{"", "feature-side:", "feature-side:0::end:1:x",
        "feature-side:01:x:end:1:y", "feature-side:1:x:unknown:1:y",
        "feature-side:999999999999999999999999999999:x:end:1:y"})
        check(!kernel::feature_side_parent(malformed),"Malformed side ancestry was accepted");

    auto part=fixture(true);
    const auto rotated=std::get<kernel::RevolutionRequest>(part.kernel_operations().front().primitive);
    auto& container=part.history.front();
    container.feature_kind=document::FeatureKind::Extrusion;
    container.extrusion.sketch_id=part.sketches.front().id;
    container.extrusion.origin_centerline=container.extrusion.centroid_centerline=true;
    auto operation=part.kernel_operations().front();
    const auto extruded=std::get<kernel::ExtrusionRequest>(operation.primitive);
    check(extruded.profile_region_id==rotated.profile_region_id,"Fixture does not share one authored profile");
    const auto start=kernel::feature_side_request(extruded,container.feature_id,kernel::FeatureSide::Start);
    const auto end=kernel::feature_side_request(rotated,container.feature_id,kernel::FeatureSide::End);
    const auto replacement=kernel::feature_side_request(extruded,container.feature_id,kernel::FeatureSide::End);
    check(end.profile_region_id==replacement.profile_region_id&&
          end.outer_edge_source_ids==replacement.outer_edge_source_ids&&
          end.outer_vertex_source_ids==replacement.outer_vertex_source_ids,
        "Replacing Revolution with Extrusion changed authored side ancestry");
    check(start.profile_region_id!=end.profile_region_id,"Both sides have the same profile-region child identity");
    for(std::size_t i=0;i<start.outer_edge_source_ids.size();++i) {
        const auto source=kernel::feature_side_parent(start.outer_edge_source_ids[i]);
        check(source&&source->source_id==extruded.outer_edge_source_ids[i]&&
              source->feature_id==container.feature_id&&source->side==kernel::FeatureSide::Start,
            "Side face ancestry lost its original Sketch curve");
    }
    kernel::FeatureGroupRequest group;group.children={start,end};operation.primitive=group;
    const auto bodies=kernel.evaluate_history({operation});
    std::set<std::string> caps;
    for(const auto& face:bodies.back().mesh.original_references.triangle_references)caps.insert(face.semantic_key);
    for(const auto& parent:{start.profile_region_id,end.profile_region_id})
        check(caps.contains("end:from:"+std::to_string(parent.size())+":"+parent),
            "A combined Feature lost an original endpoint cap");
    const auto inactive=kernel::profile_centerlines::stationary(end,operation.owner_id);
    for(const auto& point:inactive.points)
        check(std::ranges::any_of(bodies.back().mesh.original_references.points,
                [&](const auto& active){return active.reference==point.reference;}),
            "Disabling a side changed its scoped automatic endpoint identity");
}
void grouped_original_topology(const kernel::OcctKernel& kernel) {
  for(const bool opposite:{false,true}) {
    auto part=fixture(false);
    auto operation=part.kernel_operations().front();
    const auto source=std::get<kernel::ExtrusionRequest>(operation.primitive);
    auto first=kernel::feature_side_request(source,part.history.front().feature_id,kernel::FeatureSide::Start);
    auto second=kernel::feature_side_request(source,part.history.front().feature_id,kernel::FeatureSide::End);
    first.direction={0,0,opposite?-10.:10.};second.direction={0,0,20};
    // The shorter solid is fully consumed by the longer one. Its references
    // must still describe the authored child, not fragments of the union.
    operation.primitive=first;
    const auto first_body=kernel.evaluate_history({operation}).back();
    operation.primitive=second;
    const auto second_body=kernel.evaluate_history({operation}).back();
    kernel::FeatureGroupRequest group;group.children={first,second};
    for(const bool reversed:{false,true}) {
        if(reversed)std::reverse(group.children.begin(),group.children.end());
        operation.primitive=group;
        const auto combined=kernel.evaluate_history({operation}).back();
        near(combined.volume,second_body.volume+(opposite?first_body.volume:0));
        // Exercise the production native packet codec as well as calculation.
        const auto restored=document::load_body_result(document::serialize_body_result(combined));
        const auto& actual=restored.mesh.original_references;
        for(const auto* body:{&first_body,&second_body}) {
            const auto& expected=body->mesh.original_references;
            for(const auto& edge:expected.edges) {
                const auto found=std::ranges::find_if(actual.edges,[&](const auto& candidate){return candidate.reference==edge.reference;});
                check(found!=actual.edges.end(),"Union lost an original child edge identity");
                check(found->points.size()==edge.points.size(),"Union trimmed an original child edge");
                for(std::size_t i=0;i<edge.points.size();++i)near(found->points[i],edge.points[i]);
            }
            for(const auto& face:expected.triangle_references)
                check(std::ranges::any_of(actual.triangle_references,[&](const auto& found){return found==face;}),
                    "Union lost an original child face identity");
            for(const auto& point:expected.points) {
                const auto found=std::ranges::find_if(actual.points,[&](const auto& candidate){return candidate.reference==point.reference;});
                check(found!=actual.points.end(),"Union lost an original child point identity");
                near(found->position,point.position);
                auto follower=document::PartDocument::create_construction(document::ConstructionKind::Point);
                follower.definition=document::ConstructionDefinition::PointReference;
                follower.references={{"",point.reference.owner_id,point.reference.semantic_key}};
                check(document::resolve_construction(follower,actual),"Union broke a child point follower");
                near(follower.origin,point.position);
            }
        }
    }
  }
}
void inactive_profile_history(const kernel::OcctKernel& kernel) {
    for(const bool rotated:{false,true})
    for(const auto side:{kernel::FeatureSide::Start,kernel::FeatureSide::End})
    for(const auto result:{document::ProfileResultType::Solid,document::ProfileResultType::Thin,document::ProfileResultType::Surface}) {
        auto part=fixture(rotated);
        part.history.front().extrusion.result_type=result;
        part.history.front().revolution.result_type=result;
        auto operation=part.kernel_operations().front();
        kernel::FeatureGroupRequest active,inactive;
        const auto add=[&](auto request) {
            request=kernel::feature_side_request(request,part.history.front().feature_id,side);
            if(side==kernel::FeatureSide::Start) {
                request.first_cap_is_start=false;
                if constexpr(std::is_same_v<decltype(request),kernel::ExtrusionRequest>)
                    request.direction=kernel::Vec3{0,0,-20};
                else request.axis_direction=kernel::Vec3{0,-1,0};
            }
            active.children.push_back(request);inactive.reference_profiles.push_back(request);
        };
        if(rotated)add(std::get<kernel::RevolutionRequest>(operation.primitive));
        else add(std::get<kernel::ExtrusionRequest>(operation.primitive));
        operation.primitive=active;
        const auto before=kernel.evaluate_history({operation});
        auto mixed=active;
        const auto opposite=side==kernel::FeatureSide::Start?kernel::FeatureSide::End:kernel::FeatureSide::Start;
        const auto source=part.kernel_operations().front();
        if(rotated)mixed.reference_profiles.push_back(kernel::feature_side_request(
            std::get<kernel::RevolutionRequest>(source.primitive),part.history.front().feature_id,opposite));
        else mixed.reference_profiles.push_back(kernel::feature_side_request(
            std::get<kernel::ExtrusionRequest>(source.primitive),part.history.front().feature_id,opposite));
        operation.primitive=mixed;
        const auto one_disabled=kernel.evaluate_history({operation}).back();
        near(one_disabled.volume,before.back().volume);
        const auto expected_stationary=std::visit([&](const auto& request){return kernel::profile_centerlines::stationary(request,operation.owner_id);},mixed.reference_profiles.front());
        for(const auto& point:expected_stationary.points) {
            const auto found=std::ranges::find_if(one_disabled.mesh.original_references.points,[&](const auto& p){return p.reference==point.reference;});
            check(found!=one_disabled.mesh.original_references.points.end(),"Active side removed inactive side datums");
            near(found->position,point.position);
        }
        operation.primitive=inactive;
        const auto disabled=kernel.evaluate_history_incremental({operation},before);
        const auto& body=disabled.back();
        near(body.volume,0);near(body.surface_area,0);
        check(body.mesh.triangles.empty(),"Inactive side created visible material");
        check(body.source_fingerprint!=before.back().source_fingerprint,"Inactive side reused the active cache");
        const auto restored=document::load_body_result(document::serialize_body_result(body));
        const auto& refs=restored.mesh.original_references;
        for(const auto& edge:before.back().mesh.original_references.edges) {
            if(!edge.reference.semantic_key.starts_with("start:")&&!edge.reference.semantic_key.starts_with("end:"))continue;
            check(std::ranges::any_of(refs.edges,[&](const auto& p){return p.reference==edge.reference;}),"Inactive side lost an endpoint rim");
        }
        for(const auto& face:before.back().mesh.original_references.triangle_references) {
            if(!face.semantic_key.starts_with("start:from:")&&!face.semantic_key.starts_with("end:from:"))continue;
            check(std::ranges::any_of(refs.triangle_references,[&](const auto& p){return p==face;}),"Inactive side lost an endpoint cap");
        }
        for(const auto& point:before.back().mesh.original_references.points) {
            if(!point.reference.semantic_key.starts_with("start:")&&!point.reference.semantic_key.starts_with("end:")&&!point.reference.semantic_key.starts_with("profile:path-point:"))continue;
            const auto found=std::ranges::find_if(refs.points,[&](const auto& p){return p.reference==point.reference;});
            check(found!=refs.points.end(),"Inactive side lost an endpoint point");
            near(found->position.z,0);
            auto follower=document::PartDocument::create_construction(document::ConstructionKind::Point);
            follower.definition=document::ConstructionDefinition::PointReference;
            follower.references={{"",point.reference.owner_id,point.reference.semantic_key}};
            check(document::resolve_construction(follower,refs),"Inactive history endpoint broke its follower");
            near(follower.origin,found->position);
        }
        operation.primitive=active;
        const auto enabled=kernel.evaluate_history_incremental({operation},disabled);
        near(enabled.back().volume,before.back().volume);
        check(identities(enabled.back().mesh.original_references)==identities(before.back().mesh.original_references),"Reactivation changed automatic path identity");
        // A datum-only operation neither adds nor subtracts from preceding
        // material, and a following real operation still calculates normally.
        auto preceding=fixture(false).kernel_operations().front();
        preceding.body=operation.body;
        const auto base=kernel.evaluate_history({preceding}).back();
        operation.primitive=inactive;
        for(const auto boolean:{kernel::BooleanOperation::Add,kernel::BooleanOperation::Subtract}) {
            operation.operation=boolean;
            near(kernel.evaluate_history({preceding,operation}).back().volume,base.volume);
        }
        operation.operation=kernel::BooleanOperation::Add;
        near(kernel.evaluate_history({operation,preceding}).back().volume,base.volume);
    }
}
void inactive_profile_regions(const kernel::OcctKernel& kernel) {
    auto part=fixture(false);
    static_cast<void>(part.sketches.front().add_circle(10,4,1));
    auto operation=part.kernel_operations().front();
    auto request=std::get<kernel::ExtrusionRequest>(operation.primitive);
    auto island=fixture(false);
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(20,0,24,4));
    sketch.owner_container_id=island.history.front().id;
    island.history.front().extrusion.sketch_id=sketch.id;island.sketches={sketch};
    const auto second=std::get<kernel::ExtrusionRequest>(island.kernel_operations().front().primitive);
    kernel::ExtrusionRequest::ProfileRegion region;
    region.region_id=second.profile_region_id;region.outer_profile=second.outer_profile;
    region.outer_boundary_id=second.outer_boundary_id;
    region.outer_edge_source_ids=second.outer_edge_source_ids;region.outer_vertex_source_ids=second.outer_vertex_source_ids;
    request.additional_profile_regions.push_back(region);
    check(!request.additional_profile_regions.empty(),"Inactive region fixture has only one region");
    operation.primitive=request;
    const auto active=kernel.evaluate_history({operation}).back();
    kernel::FeatureGroupRequest group;group.reference_profiles.push_back(request);
    operation.primitive=group;
    const auto inactive=kernel.evaluate_history({operation}).back();
    std::set<std::string> caps;
    double area=0;
    for(const auto& face:inactive.mesh.original_references.triangle_references) {
        if(!caps.insert(face.semantic_key).second)continue;
        check(face.measured_area.has_value(),"Inactive cap has no measured source area");
        area+=*face.measured_area;
        check(std::ranges::any_of(active.mesh.original_references.triangle_references,[&](const auto& f){return f==face;}),"Inactive region changed cap parent");
    }
    check(caps.size()==4,"Inactive regions lost distinct Start/End caps");
    near(area,2*(96-std::numbers::pi));
    near(inactive.volume,0);near(inactive.surface_area,0);
}
void inactive_cap_target(const kernel::OcctKernel& kernel) {
    auto source=fixture(false).kernel_operations().front();
    const auto request=std::get<kernel::ExtrusionRequest>(source.primitive);
    kernel::FeatureGroupRequest group;group.reference_profiles.push_back(request);
    source.primitive=group;
    const auto datums=kernel.evaluate_history({source});
    const auto& faces=datums.back().mesh.original_references.triangle_references;
    const auto cap=std::ranges::find_if(faces,[](const auto& face){return face.semantic_key.starts_with("end:from:");});
    check(cap!=faces.end(),"Inactive target cap missing");
    auto follower=fixture(false).kernel_operations().front();follower.body=source.body;
    auto& extrusion=std::get<kernel::ExtrusionRequest>(follower.primitive);
    for(auto& point:std::get<kernel::ExtrusionRequest::PolygonProfile>(extrusion.outer_profile).vertices)point.z-=5;
    extrusion.direction={0,0,10};
    extrusion.extent=kernel::ExtrusionRequest::Extent::UpToPlane;
    extrusion.target_face=*cap;extrusion.target_is_datum=false;
    extrusion.target_plane_origin={0,0,0};extrusion.target_plane_normal={0,0,1};
    extrusion.centerlines.origin_enabled=extrusion.centerlines.centroid_enabled=false;
    const auto result=kernel.evaluate_history_incremental({source,follower},datums).back();
    near(result.volume,400);
}
void native_feature_history(const kernel::OcctKernel& kernel) {
    auto part=fixture(true);
    const auto axis=part.history.front().revolution.axis_segment_id;
    auto feature=document::PartDocument::create_feature_container(part.sketches.front().id);
    auto& p=feature.feature;p.axis_segment_id=axis;
    p.origin_centerline=p.centroid_centerline=true;
    p.sides[0].length=20;p.sides[1].operation=document::FeatureSideOperation::Extrusion;p.sides[1].length=7;
    part.sketches.front().owner_container_id=feature.id;
    part.history={feature};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Feature"));
    graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    auto bodies=kernel.evaluate_history(part.kernel_operations());near(bodies.back().volume,2160);
    std::vector<kernel::BodyResult> saved;
    auto reopened=document::PartDocument::from_serialized(part.serialized(bodies),&saved);
    check(reopened.history.front().feature_kind==document::FeatureKind::Feature&&reopened.history.front().feature==p,
        "Native Feature lost its type or definition");
    near(kernel.evaluate_history(reopened.kernel_operations()).back().volume,2160);
    const auto directory=std::filesystem::temp_directory_path()/("zima-feature-"+part.document_id);
    check(std::filesystem::create_directory(directory),"Cannot create native Feature test directory");
    const auto file=directory/"feature.prtz";
    part.save(file,bodies);
    const auto from_file=document::PartDocument::load(file,&saved);
    check(from_file.history.front().feature==p,"PRTZ file lost Feature parameters");
    near(kernel.evaluate_history(from_file.kernel_operations()).back().volume,2160);
    std::filesystem::remove(file);
    for(const auto& item:std::filesystem::directory_iterator(directory))std::filesystem::remove(item.path());
    std::filesystem::remove(directory);
    const auto parameters=[&]() -> document::FeatureParameters& {return part.history.front().feature;};
    parameters().sides[1].operation=document::FeatureSideOperation::Revolution;
    bodies=kernel.evaluate_history(part.kernel_operations());
    const auto references=bodies.back().mesh.original_references;
    const auto parent=kernel::feature_side_parent_key({feature.feature_id,kernel::FeatureSide::Start,part.sketches.front().id});
    const auto endpoint=std::ranges::find_if(references.points,[&](const auto& point){
        return point.reference.semantic_key.starts_with("profile:path-point:start:")&&point.reference.semantic_key.ends_with(parent);
    });
    check(endpoint!=references.points.end(),"Native rotated side has no centroid endpoint");
    auto follower=document::PartDocument::create_construction(document::ConstructionKind::Point);
    follower.definition=document::ConstructionDefinition::PointReference;
    follower.references={{"",endpoint->reference.owner_id,endpoint->reference.semantic_key}};
    const auto stored_reference=follower.references;
    part.constructions.push_back(follower);part.insert_history_entry(document::PartHistoryKind::Construction,follower.id);
    part.resolve_constructions(references);
    near(part.constructions.back().origin,endpoint->position);
    parameters().sides[1].operation=document::FeatureSideOperation::None;
    const auto disabled=kernel.evaluate_history(part.kernel_operations());near(disabled.back().volume,1600);
    part.resolve_constructions(disabled.back().mesh.original_references);
    check(part.constructions.back().references==stored_reference,"Disabling native Feature rewrote the follower reference");
    near(part.constructions.back().origin.z,0);
    const auto with_follower=document::PartDocument::from_serialized(part.serialized(disabled));
    check(with_follower.constructions.back().references==stored_reference,"Native save lost the dependent Point reference");
    check(part.history.front().feature_id==feature.feature_id,"Operation edit changed Feature identity");
    for(const auto& point:references.points)if(point.reference.semantic_key.starts_with("profile:path-point:")) {
        check(std::ranges::any_of(disabled.back().mesh.original_references.points,[&](const auto& p){return p.reference==point.reference;}),
            "Native Feature operation edit lost an automatic endpoint");
    }
    for(const auto result:{document::ProfileResultType::Thin,document::ProfileResultType::Surface}) {
        parameters().result_type=result;parameters().sides[1].operation=document::FeatureSideOperation::Extrusion;
        const auto calculated=kernel.evaluate_history(part.kernel_operations());
        if(result==document::ProfileResultType::Surface) {near(calculated.back().volume,0);near(calculated.back().surface_area,972);}
        else check(calculated.back().volume>0,"Native thin Feature has no volume");
    }
    parameters().result_type=document::ProfileResultType::Solid;
    parameters().sides[0].operation=parameters().sides[1].operation=document::FeatureSideOperation::None;
    const auto reference_only=kernel.evaluate_history(part.kernel_operations());
    near(reference_only.back().volume,0);near(reference_only.back().surface_area,0);
    check(!reference_only.back().mesh.original_references.points.empty(),"Native reference-only Feature lost its points");
    const auto closed=part.sketches.front();
    part.erase_history_object(follower.id);
    // Empty and open sketches are valid only while both sides are inactive.
    for(const bool open:{false,true}) {
        part.sketches.front().segments.clear();
        part.sketches.front().points.clear();
        part.sketches.front().constraints.clear();
        part.sketches.front().dimensions.clear();
        if(open) {
            part.sketches.front().points=closed.points;
            part.sketches.front().segments.push_back(closed.segments.front());
        }
        const auto requests=part.kernel_operations();
        const auto calculated=kernel.evaluate_history(requests);
        near(calculated.back().volume,0);near(calculated.back().surface_area,0);
        const auto& refs=calculated.back().mesh.original_references;
        for(const auto& old:reference_only.back().mesh.original_references.points) {
            if(old.reference.semantic_key.find("centerline:from:origin:")==std::string::npos)continue;
            check(std::ranges::any_of(refs.points,[&](const auto& point){return point.reference==old.reference;}),
                "Sketch-only Feature lost its persistent origin endpoints");
        }
        check(part.feature_preview_edges(part.history.front()).empty(),"Sketch-only Feature created a body preview");
        const auto roundtrip=document::PartDocument::from_serialized(part.serialized(calculated));
        near(kernel.evaluate_history(roundtrip.kernel_operations()).back().volume,0);
    }
}
void bounded_feature_rotations(const kernel::OcctKernel& kernel) {
    auto baseline=fixture(true);baseline.history.front().revolution.angle_degrees=360;
    const auto full_volume=kernel.evaluate_history(baseline.kernel_operations()).back().volume;
    auto part=fixture(true);const auto axis=part.history.front().revolution.axis_segment_id;
    auto& feature=part.history.front();feature.feature_kind=document::FeatureKind::Feature;
    auto& p=feature.feature;p.sketch_id=part.sketches.front().id;p.axis_segment_id=axis;
    for(auto& side:p.sides){side.operation=document::FeatureSideOperation::Revolution;side.angle_degrees=180;}
    near(kernel.evaluate_history(part.kernel_operations()).back().volume,full_volume);
    check(!part.feature_preview_edges(feature).empty(),"Two half-turn preview missing");
    p.sides[0].angle_degrees=300;document::normalize_feature_rotations(p,0);
    near(kernel.evaluate_history(part.kernel_operations()).back().volume,full_volume);
    p.sides[0].rotation_extent=document::FeatureRotationExtent::Full;document::normalize_feature_rotations(p,0);
    near(kernel.evaluate_history(part.kernel_operations()).back().volume,full_volume);
    const auto reopened=document::PartDocument::from_serialized(part.serialized());
    check(reopened.history.front().feature==p,"Rotation normalization did not survive serialization");
    p.sides[0].rotation_extent=document::FeatureRotationExtent::UpTo;
    p.sides[1].operation=document::FeatureSideOperation::Revolution;
    p.sides[1].rotation_extent=document::FeatureRotationExtent::UpTo;
    for(auto& side:p.sides) {
        document::ExtrusionParameters::EndTarget target;target.kind=document::EndTargetKind::Plane;
        target.reference={"target","face:plane",{}};target.fallback_normal={1,0,0};side.targets={target};
    }
    near(kernel.evaluate_history(part.kernel_operations()).back().volume,full_volume);
    check(!part.feature_preview_edges(feature).empty(),"Reference-limited full-turn preview missing");
    p.sides[0].targets[0].fallback_normal={-1,0,0};
    const auto targets=p.sides;
    const auto rejected=[&](auto action) {
        try{action();}catch(const std::exception& e){
            check(std::string(e.what())=="Combined rotation angle must not exceed 360 degrees.","Wrong rotation conflict");return;
        }throw std::runtime_error("Overlapping reference-limited rotations were accepted");
    };
    rejected([&]{static_cast<void>(part.kernel_operations());});
    rejected([&]{static_cast<void>(part.feature_preview_edges(feature));});
    check(p.sides==targets,"Rotation conflict changed an oriented target");
    p.symmetric=true;
    rejected([&]{static_cast<void>(part.kernel_operations());});
    rejected([&]{static_cast<void>(part.feature_preview_edges(feature));});
    p.symmetric=false;p.sides[1].operation=document::FeatureSideOperation::Extrusion;p.sides[1].length=20;
    check(!part.feature_preview_edges(feature).empty(),"Mixed operation preview was restricted");
    check(kernel.evaluate_history(part.kernel_operations()).back().volume>0,"Mixed rotation/extrusion was restricted");
}
void grouped_surface_history(const kernel::OcctKernel& kernel) {
    auto base=fixture(false).kernel_operations().front();
    const auto solid=kernel.evaluate_history({base}).back();
    auto first=std::get<kernel::ExtrusionRequest>(base.primitive);first.surface_result=true;
    first.direction={0,0,10};
    auto second=first;second.direction={0,0,-10};second.first_cap_is_start=false;
    kernel::FeatureGroupRequest group;
    group.children={kernel::feature_side_request(first,"surface",kernel::FeatureSide::End),
                    kernel::feature_side_request(second,"surface",kernel::FeatureSide::Start)};
    auto surface=base;surface.owner_id="surface";surface.primitive=group;
    const auto standalone=kernel.evaluate_history({surface}).back();
    const auto combined=kernel.evaluate_history({base,surface}).back();
    near(combined.volume,solid.volume);
    near(combined.surface_area,solid.surface_area+standalone.surface_area);
}
int main(){try {
 coincident_revolution_endpoints();
 using E=kernel::ExtrusionRequest;E request;request.centerlines.origin={0,0,0};request.centerlines.normal={0,0,1};
 request.outer_profile=E::PolygonProfile{{{0,0,0},{20,0,0},{20,10,0},{0,10,0}}};
 near(kernel::profile_centerlines::centroid(request),{10,5,0});
 request.inner_profiles={E::CircleProfile{{5,5,0},2}};
 const double hole=4*std::numbers::pi;near(kernel::profile_centerlines::centroid(request),{(2000-5*hole)/(200-hole),5,0});
 request.outer_profile=E::CurvedProfile{{E::ArcCurve{{-3,0,0},{0,3,0},{3,0,0}},E::LineCurve{{3,0,0},{-3,0,0}}}};request.inner_profiles.clear();
 near(kernel::profile_centerlines::centroid(request),{0,4/std::numbers::pi,0});
 request.outer_profile=E::CurvedProfile{{E::ArcCurve{{7,4,0},{10,7,0},{13,4,0}},E::LineCurve{{13,4,0},{7,4,0}}}};
 near(kernel::profile_centerlines::centroid(request),{10,4+4/std::numbers::pi,0});
 request.outer_profile=E::EllipseProfile{{3,7,0},{1,0,0},5,2};near(kernel::profile_centerlines::centroid(request),{3,7,0});
 E::BSplineCurve parabola;parabola.start={-1,0,0};parabola.end={1,0,0};parabola.control_points={{-1,0,0},{0,1,0},{1,0,0}};parabola.degree=2;parabola.knots={0,0,0,1,1,1};parabola.weights={1,1,1};
 request.outer_profile=E::CurvedProfile{{parabola,E::LineCurve{{1,0,0},{-1,0,0}}}};near(kernel::profile_centerlines::centroid(request),{0,.2,0});
 request.centerlines.origin={3,5,7};request.centerlines.normal={1,0,0};request.outer_profile=E::PolygonProfile{{{3,5,7},{3,15,7},{3,15,13},{3,5,13}}};near(kernel::profile_centerlines::centroid(request),{3,10,10});
 kernel::OcctKernel kernel;
 bounded_feature_rotations(kernel);
 inactive_profile_history(kernel);
 inactive_profile_regions(kernel);
 inactive_cap_target(kernel);
 native_feature_history(kernel);
 grouped_surface_history(kernel);
 grouped_original_topology(kernel);
 grouped_centerlines(kernel);
 extrusion_identity_matrix(kernel);
 limited_centerline_matrix(kernel);
 mirrored_feature_limit(kernel);
 stationary_references(kernel);
 feature_side_ancestry(kernel);
 for(bool revolve:{false,true}){
  auto part=fixture(revolve);auto operations=part.kernel_operations();auto bodies=kernel.evaluate_history(operations);const auto original=identities(bodies.back().mesh.original_references);
  check(!original.empty(),"Profile references absent from persisted packet");
  auto disabled=part;if(revolve)disabled.history.front().revolution.origin_centerline=disabled.history.front().revolution.centroid_centerline=false;else disabled.history.front().extrusion.origin_centerline=disabled.history.front().extrusion.centroid_centerline=false;
  auto plain=kernel.evaluate_history(disabled.kernel_operations());near(plain.back().volume,bodies.back().volume);check(identities(plain.back().mesh.original_references).empty(),"Disabled options created centerlines");
  auto changed=part;if(revolve)changed.history.front().revolution.angle_degrees=135;else changed.history.front().extrusion.length_forward=25;
  auto moved=kernel.evaluate_history(changed.kernel_operations());check(identities(moved.back().mesh.original_references)==original,"Extent edit changed reference identity");
  check(moved.back().source_fingerprint!=bodies.back().source_fingerprint,"Centerline extent reused stale cache");
  nlohmann::json data;document::save_profile_parameters(changed.history.front(),data);auto restored=changed.history.front();document::load_profile_parameters(restored,data);
  check(restored.extrusion==changed.history.front().extrusion&&restored.revolution==changed.history.front().revolution,"Centerline options did not round-trip");
  if(revolve){
   const auto& refs=moved.back().mesh.original_references;check(refs.edges.end()!=std::ranges::find_if(refs.edges,[](const auto& e){return e.reference.semantic_key.starts_with("centerline:from:centroid:")&&e.exact_spline.has_value();}),"Rotated centerline has no exact curve");
   changed.history.front().revolution.angle_degrees=360;auto full=kernel.evaluate_history(changed.kernel_operations());
   check(std::ranges::none_of(full.back().mesh.original_references.points,[](const auto& p){return p.reference.semantic_key.starts_with("profile:path-point:");}),"Full revolution exposed an unwanted grip point");
   changed.history.front().revolution.angle_degrees=135;
  }else {
   const auto& refs=moved.back().mesh.original_references;check(original.size()==6,"Extrusion requires two axes and four endpoints");
   for(const auto& point:refs.points)if(point.reference.semantic_key.starts_with("profile:path-point:end:"))near(point.position.z,25);
   auto& r=std::get<E>(operations.front().primitive);r.extent=E::Extent::UpToPlane;r.target_is_datum=true;r.target_face={"origin","plane:xy",{}};r.target_plane_origin={0,0,20};r.target_plane_normal={-.5,0,1};
   const auto clipped=kernel.evaluate_history(operations);for(const auto& point:clipped.back().mesh.original_references.points)if(point.reference.semantic_key.starts_with("profile:path-point:end:"))near(point.position.z,20+.5*point.position.x);
  }
  std::vector<kernel::BodyResult> reopened_results;auto reopened=document::PartDocument::from_serialized(changed.serialized(moved),&reopened_results);
  check(identities(reopened_results.back().mesh.original_references)==identities(moved.back().mesh.original_references),"Native save/reopen lost centerline references");
  const auto& refs=reopened_results.back().mesh.original_references;
  for(const auto& point:refs.points)if(point.reference.semantic_key.starts_with("profile:path-point:end:")) {
   auto follower=document::PartDocument::create_construction(document::ConstructionKind::Point);follower.definition=document::ConstructionDefinition::PointReference;
   follower.references={{"",point.reference.owner_id,point.reference.semantic_key}};
   check(document::resolve_construction(follower,refs),"Downstream point cannot bind profile endpoint");near(follower.origin,point.position);
  }
  if(!revolve){
   const auto axis=std::ranges::find_if(refs.axes,[](const auto& a){return a.reference.semantic_key.starts_with("centerline:from:centroid:");});
   check(axis!=refs.axes.end(),"Centroid axis missing");auto follower=document::PartDocument::create_construction(document::ConstructionKind::Axis);follower.definition=document::ConstructionDefinition::AxisReference;follower.references={{"",axis->reference.owner_id,axis->reference.semantic_key}};
   check(document::resolve_construction(follower,refs),"Downstream axis cannot bind centroid axis");
   check(std::ranges::any_of(moved.back().mesh.axes,[&](const auto& a){return a.reference==axis->reference;}),"Profile axis is not visible in ordinary View");
  }
  document::DocumentSession session(part);session.commit(changed,moved);check(session.undo(),"Centerline edit lacks Undo");check(session.redo(),"Centerline edit lacks Redo");
 }
 std::cout<<"Profile centerlines: analytic moments, holes, stable references, end limits, exact arcs, full rotation and persistence passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

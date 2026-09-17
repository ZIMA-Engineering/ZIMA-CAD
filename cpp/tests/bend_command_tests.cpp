#include <zima/command_host/host.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
using commands::Json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",args}});
    if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
    std::cout<<command<<' '<<args.dump()<<'\n';return result;
}
std::set<std::string> faces(const kernel::BodyResult& body,const std::string& owner) {
    std::set<std::string> result;
    for(const auto& face:body.mesh.original_references.triangle_references)if(face.owner_id==owner)result.insert(face.semantic_key);
    return result;
}
void verify_cross_branch_box(std::filesystem::path directory) {
    const auto path=directory/"box-state.prtz";
    std::filesystem::copy_file("cpp/tests/fixtures/sheet/box-cross-branch.prtz",path);
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Host host(live,kernel,directory);
    run(host,"open",{{"path",path.string()}});
    const auto id=live.active_document_id();
    const std::string first="01a0af5d46a97ac7bdafefbc7c826e7d",second="01a0af5d46a97ac7bdafefbc7c826ecb";
    const std::string profile="01a0af5d46a97ac7bdafefbc7c826ef6",source_profile="01a0af5d46a97ac7bdafefbc7c826ea3";
    // Earlier edits commit. Only the later owner of the conflicting C loses
    // its geometry, with a persisted diagnostic and automatic recovery.
    {
        auto* part=live.open_part(id);
        const auto wall_before=workspace::document_sketch(live,id,source_profile);
        const auto contacts_before=workspace::document_sketch(live,id,profile).constraints;
        auto next=part->session.document();auto* feature=next.find_container(first);
        auto trajectory=sketcher::Sketch::from_serialized(feature->bend.auxiliary_sketches[0]);
        check(trajectory.set_dimension_value(trajectory.id+":angle",180.),"First box Bend angle solver rejected 180 degrees");
        const auto& start=*std::ranges::find(next.sketches,feature->bend.sketch_id,&sketcher::Sketch::id);
        document::accept_bend_sketch(*feature,start,0,trajectory,document::sheet_metal_defaults(next));
        workspace::commit_part_parameter_edit(*part,kernel,std::move(next),trajectory.id,false,{});
        near(part->session.document().find_container(first)->bend.angle_degrees,180.);
        const auto wall_after=workspace::document_sketch(live,id,source_profile);
        const auto a=wall_before.resolved_normal,b=wall_after.resolved_normal;
        near(a.x*b.x+a.y*b.y+a.z*b.z,0.);
        check(wall_before.id==wall_after.id&&wall_before.segments==wall_after.segments,
            "Rotating Bend replaced its attached wall identity");
        for(std::size_t i=0;i<wall_before.external_references.size();++i)
            check(wall_before.external_references[i].source_semantic_key==wall_after.external_references[i].source_semantic_key,
                "Rotating Bend changed its wall attachment endpoint identities");
        const auto failed=std::string("01a0af5d46a97ac7bdafefbc7c826ef7");
        const auto verify_failure=[&](const auto& body) {
            check(body.calculation_errors.size()==1&&body.calculation_errors.contains(failed),"Reference failure was not isolated to the later wall");
            check(faces(body,failed).empty()&&!faces(body,first).empty(),"Failed wall stayed visible or preceding Bend disappeared");
        };
        verify_failure(part->session.calculated_boundaries().back());
        check(workspace::document_sketch(live,id,profile).constraints==contacts_before,"Recovery removed the C constraint");
        check(!host.execute({{"command","regenerate"}}).ok,"Regenerate did not report the retained failed feature");
        verify_failure(part->session.calculated_boundaries().back());
        run(host,"save");std::vector<kernel::BodyResult> cache;
        const auto saved=document::PartDocument::load(path,&cache);
        check(saved.reference_errors.contains(failed)&&!cache.empty(),"Native save lost the failing reference diagnostic");
        verify_failure(cache.back());
        run(host,"bend.set",{{"container",first},{"angle_degrees",90.}});
        check(part->session.document().reference_errors.empty()&&part->session.calculated_boundaries().back().calculation_errors.empty(),"Returning to 90 degrees did not recover references");
        check(!faces(part->session.calculated_boundaries().back(),failed).empty(),"Recovered wall geometry was not restored");
        run(host,"undo");verify_failure(part->session.calculated_boundaries().back());
        run(host,"redo");
    }
    for(const auto& owner:{second}) {
        auto* part=live.open_part(id);auto next=part->session.document();auto* feature=next.find_container(owner);
        auto trajectory=sketcher::Sketch::from_serialized(feature->bend.auxiliary_sketches[0]);
        std::cout<<"Box View angle 180: "<<owner<<std::endl;
        check(trajectory.set_dimension_value(trajectory.id+":angle",180.),"Box View angle solver rejected 180 degrees");
        const auto& start=*std::ranges::find(next.sketches,feature->bend.sketch_id,&sketcher::Sketch::id);
        document::accept_bend_sketch(*feature,start,0,trajectory,document::sheet_metal_defaults(next));
        workspace::commit_part_parameter_edit(*part,kernel,std::move(next),trajectory.id,false,{});
        near(part->session.document().find_container(owner)->bend.angle_degrees,180.);
        run(host,"undo");
    }
    const auto original=workspace::document_sketch(live,id,profile);
    const auto check_profile=[&](double height) {
        const auto sketch=workspace::document_sketch(live,id,profile);
        check(sketch.id==original.id&&sketch.segments==original.segments,"State change replaced rectangle identities");
        check(sketch.constraints==original.constraints,"State change removed or replaced a box constraint");
        for(std::size_t i=0;i<sketch.points.size();++i)check(sketch.points[i].id==original.points[i].id,"State change replaced a rectangle corner");
        for(std::size_t i=0;i<sketch.external_references.size();++i) {
            check(sketch.external_references[i].id==original.external_references[i].id&&
                sketch.external_references[i].source_semantic_key==original.external_references[i].source_semantic_key&&
                !sketch.external_references[i].broken,"State change lost a box reference");
        }
        near(sketch.points[2].y,height);near(sketch.points[3].y,height);
        check(live.open_part(id)->session.calculated_boundaries().back().calculation_errors.empty(),"Box state has a calculation error");
        for(const auto& edge:live.open_part(id)->session.calculated_boundaries().back().mesh.edges) {
            bool first_wall=false,second_wall=false;
            for(const auto& face:edge.edge_treatment_side_references) {
                first_wall=first_wall||face.owner_id=="01a0af5d46a97ac7bdafefbc7c826ea4";
                second_wall=second_wall||face.owner_id=="01a0af5d46a97ac7bdafefbc7c826ef7";
            }
            check(!(first_wall&&second_wall),"Free box walls acquired a shared joined edge");
        }
    };
    for(const auto states:{std::pair{true,false},std::pair{true,true},std::pair{false,true},std::pair{false,false}}) {
        run(host,"bend.set",{{"container",first},{"state",states.first?"unbend":"bend"}});
        run(host,"bend.set",{{"container",second},{"state",states.second?"unbend":"bend"}});
        check_profile(150.);run(host,"regenerate");check_profile(150.);
    }
    run(host,"bend.set",{{"container",first},{"state","unbend"}});
    check(workspace::mutate_document_sketch(live,id,source_profile,[](auto& sketch) {
        const auto dimension=std::ranges::find_if(sketch.dimensions,[](const auto& d){return d.driving&&std::abs(d.value-150.)<1e-8;});
        check(dimension!=sketch.dimensions.end(),"Source wall height dimension is missing");
        const auto dimension_id=dimension->id;
        check(sketch.set_dimension_value(dimension_id,175.),"Source wall height edit failed");
    }),"Cannot change the source wall height in the unfolded state");
    run(host,"regenerate");check_profile(175.);
    run(host,"save");
    const auto reopened=document::PartDocument::load(path);
    check(reopened.sheet_reference_state!="{}","Unfolded Part lost its design reference snapshot");
    run(host,"close",{{"document",id},{"discard",true}});
    run(host,"open",{{"path",path.string()}});check_profile(175.);
    auto sketch=workspace::document_sketch(live,id,profile);
    static_cast<void>(workspace::refresh_sketch_reference_snapshot(live,id,sketch));
    near(sketch.points[2].y,175.);
    const auto& face=original.external_references.back();
    const auto recreated=workspace::prepare_sketch_external_reference(live,id,sketch,face.kind,face.source_owner_id,face.source_semantic_key,{});
    near(recreated.cached_points.front()[1],175.);
    run(host,"bend.set",{{"container",first},{"state","bend"}});check_profile(175.);
    run(host,"undo");check_profile(175.);run(host,"redo");check_profile(175.);
}
void verify_sketches(document::HistoryContainer feature,const sketcher::Sketch& start,document::SheetMetalDefaults defaults) {
    auto path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    auto end=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[1]);
    const auto path_id=path.id,end_id=end.id,arc_id=path.arcs.front().id;
    check(path.set_dimension_value(path.id+":radius",9),"Cannot edit Bend trajectory radius");
    document::accept_bend_sketch(feature,start,0,path,defaults);near(feature.bend.radius,8);
    path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    check(path.set_dimension_value(path.id+":angle",45),"Cannot edit Bend trajectory angle");
    document::accept_bend_sketch(feature,start,0,path,defaults);near(feature.bend.angle_degrees,45);
    for(double angle:{30.,120.,180.,45.}) {
        path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
        check(path.set_dimension_value(path.id+":angle",angle),"Bend trajectory angle cannot reach its tested limit");
        document::accept_bend_sketch(feature,start,0,path,defaults);near(feature.bend.angle_degrees,angle);
    }
    check(end.set_dimension_value(end.id+":difference:first",3),"Cannot edit first endpoint difference");
    check(end.set_dimension_value(end.id+":difference:last",10),"Cannot edit last endpoint difference");
    document::accept_bend_sketch(feature,start,1,end,defaults);
    const auto extensions=document::bend_profile_extensions(feature);near(extensions[0],3);near(extensions[1],10);
    feature.bend.unbend=true;document::prepare_bend_sketches(feature,start,defaults);
    feature.bend.unbend=false;document::prepare_bend_sketches(feature,start,defaults);
    path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    end=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[1]);
    check(path.id==path_id&&end.id==end_id&&path.arcs.front().id==arc_id,"Reframing replaced Bend Sketch identities");
    near(document::bend_profile_extensions(feature)[0],3);near(document::bend_profile_extensions(feature)[1],10);
    const auto point=path.world_point(path.find_point(path.arcs.front().end_point_id)->x,path.find_point(path.arcs.front().end_point_id)->y);
    near(point.x,end.resolved_origin.x);near(point.y,end.resolved_origin.y);near(point.z,end.resolved_origin.z);
}
void verify_sheet_attachment(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","sheet-attachment"}});
    run(host,"flat.create",{{"width_mm",40.},{"height_mm",30.},{"thickness_mm",2.}});
    const auto* state=live.open_part(live.active_document_id());
    const auto source=state->session.document();
    const auto original=state->session.calculated_boundaries().back();
    const auto restored=document::load_body_result(document::serialize_body_result(original));
    const auto geometry=restored.mesh.original_references;
    unsigned boundary_count=0;
    for(const auto& edge:geometry.edges) {
        if(kernel::sheet_edge_role(edge)==kernel::SheetEdgeRole::Thickness) {
            bool rejected=false;try{static_cast<void>(document::bend_sheet_references(edge));}catch(const std::invalid_argument&){rejected=true;}
            check(rejected,"Thickness edge accepted for Bend");continue;
        }
        if(kernel::sheet_edge_role(edge)!=kernel::SheetEdgeRole::Boundary)continue;
        ++boundary_count;
        std::optional<kernel::Vec3> previous_inward;
        for(const auto& endpoint:edge.edge_treatment_endpoint_references)for(double angle:{0.,90.,180.}) {
            auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Bend;
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            document::initialize_bend_start_profile(sketch,5);
            feature.bend.sketch_id=sketch.id;feature.bend.sheet_attachment=true;feature.bend.angle_degrees=angle;
            feature.placement.references=document::bend_sheet_references(edge,endpoint);
            auto part=document::PartDocument::create_default();part.history={feature};part.sketches={sketch};
            part.resolve_constructions(geometry);
            const auto& resolved=part.sketches.front();
            check(part.history.front().placement.reference_valid,"Automatic sheet placement is unresolved");
            near(document::resolved_bend_parameters(part.history.front(),document::sheet_metal_defaults(part)).thickness,2);
            check(resolved.external_references.size()==2,"Bend lacks native endpoint references");
            for(const auto& dimension:resolved.dimensions)near(dimension.value,0);
            check(resolved.viewer_mesh().dimensions.size()==2,"Zero Bend endpoint dimensions are hidden");
            near(resolved.plane_offset,0);
            near(resolved.resolved_origin.x,part.history.front().placement.x);
            near(resolved.resolved_origin.y,part.history.front().placement.y);
            near(resolved.resolved_origin.z,part.history.front().placement.z);
            const auto& segment=resolved.segments.front();
            const auto a=resolved.world_point(resolved.find_point(segment.first_point_id)->x,0);
            const auto b=resolved.world_point(resolved.find_point(segment.second_point_id)->x,0);
            const auto distance=[](kernel::Vec3 p,kernel::Vec3 q){return std::hypot(p.x-q.x,p.y-q.y,p.z-q.z);};
            check((distance(a,edge.points.front())<1e-6&&distance(b,edge.points.back())<1e-6)||
                (distance(b,edge.points.front())<1e-6&&distance(a,edge.points.back())<1e-6),"Bend span does not match the selected boundary");
            if(previous_inward)check(distance(*previous_inward,resolved.resolved_y_axis)<1e-6,"Changing origin flipped the material side");
            previous_inward=resolved.resolved_y_axis;
            if(angle>0) {
                const auto result=kernel.evaluate_history(part.kernel_operations()).back();
                check(result.volume>0,"Automatic Bend produced no solid");
                bool outer=false,inner=false;
                for(const auto& face:result.mesh.original_references.triangle_references) {
                    outer|=face.sheet_role==kernel::SheetFaceRole::SideA;inner|=face.sheet_role==kernel::SheetFaceRole::SideB;
                    check(face.sheet_role!=kernel::SheetFaceRole::Unknown,"Bend face lost its sheet role");
                }
                check(outer&&inner,"Bend lacks outer/inner sheet faces");
            }
            auto edited=part;
            check(edited.sketches.front().set_dimension_value(resolved.id+":position:first",2),"Cannot edit Bend endpoint offset");
            edited.resolve_constructions(geometry);
            near(edited.sketches.front().find_point(segment.first_point_id)->x,resolved.find_point(segment.first_point_id)->x+2);
            part.save(directory/"sheet-attachment-check.prtz");
            auto reopened=document::PartDocument::load(directory/"sheet-attachment-check.prtz");reopened.resolve_constructions(geometry);
            check(reopened.history.front().bend.sheet_attachment,"Sheet attachment flag lost on reopen");
            near(reopened.sketches.front().dimensions.front().value,0);
        }
    }
    check(boundary_count==8,"Serialized Flat lost its boundary roles");
    const auto selected=std::ranges::find_if(geometry.edges,[](const auto& e) {
        return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&
            std::ranges::all_of(e.points,[](const auto& p){return std::abs(p.y)<1e-7&&std::abs(p.z-2)<1e-7;});
    });
    check(selected!=geometry.edges.end(),"Missing top boundary in attachment transaction test");
    const auto created=run(host,"bend.create",{{"edge_owner",selected->reference.owner_id},{"edge_key",selected->reference.semantic_key},
        {"radius_mm",5.},{"angle_degrees",90.}}).data;
    const auto owner=created.at("container").get<std::string>();
    for(const auto& sketch:state->session.document().sketches)if(sketch.owner_container_id==owner)
        for(const auto& reference:sketch.external_references)
            check(reference.source_document_id==state->session.document().document_id,"Attachment points refer to a temporary Body document");
    near(state->session.calculated_boundaries().back().volume,2400+40*std::numbers::pi/2*12);
    run(host,"bend.set",{{"container",owner},{"origin_last",true}});
    near(state->session.calculated_boundaries().back().volume,2400+40*std::numbers::pi/2*12);
    const auto profile_id=state->session.document().find_container(owner)->bend.sketch_id;
    check(workspace::mutate_document_sketch(live,live.active_document_id(),profile_id,[&](auto& sketch) {
        check(sketch.set_dimension_value(profile_id+":position:first",3),"Cannot set first attachment offset");
        check(sketch.set_dimension_value(profile_id+":position:last",11),"Cannot set last attachment offset");
    }),"Asymmetric attachment offset edit was not committed");
    run(host,"regenerate");
    const auto locations=[&] {
        const auto& sketch=*std::ranges::find(state->session.document().sketches,profile_id,&sketcher::Sketch::id);
        std::array<double,2> result;
        for(std::size_t i=0;i<2;++i) {
            const auto& dimension=*std::ranges::find(sketch.dimensions,profile_id+(i?":position:last":":position:first"),&sketcher::SketchDimension::id);
            near(sketcher::dimension_display_value(dimension),i?11:3);
            const auto* point=sketch.find_point(dimension.second_point_id);
            result[i]=sketch.world_point(point->x,point->y).x;
        }
        return result;
    };
    const auto before_switch=locations();
    std::array<std::string,2> endpoint_ids;
    {
        const auto& sketch=*std::ranges::find(state->session.document().sketches,profile_id,&sketcher::Sketch::id);
        for(std::size_t i=0;i<2;++i)endpoint_ids[i]=std::ranges::find(sketch.dimensions,profile_id+(i?":position:last":":position:first"),&sketcher::SketchDimension::id)->second_point_id;
    }
    const auto opposite=std::ranges::find_if(geometry.edges,[](const auto& e) {
        return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&
            std::ranges::all_of(e.points,[](const auto& p){return std::abs(p.y)<1e-7&&std::abs(p.z)<1e-7;});
    });
    check(opposite!=geometry.edges.end(),"Missing opposite attachment boundary");
    for(const auto* edge:{&*opposite,&*selected,&*opposite})for(bool last:{false,true}) {
        run(host,"bend.set",{{"container",owner},{"edge_owner",edge->reference.owner_id},{"edge_key",edge->reference.semantic_key},{"origin_last",last}});
        const auto after=locations();near(after[0],before_switch[0]);near(after[1],before_switch[1]);
        const auto& sketch=*std::ranges::find(state->session.document().sketches,profile_id,&sketcher::Sketch::id);
        for(std::size_t i=0;i<2;++i)check(std::ranges::find(sketch.dimensions,profile_id+(i?":position:last":":position:first"),&sketcher::SketchDimension::id)->second_point_id==endpoint_ids[i],
            "Changing the boundary exchanged profile point identities");
    }
    auto reversed_geometry=state->session.calculated_boundaries().back().mesh.original_references;
    for(auto& edge:reversed_geometry.edges) {
        std::ranges::reverse(edge.points);std::ranges::reverse(edge.edge_treatment_endpoint_references);
        if(edge.exact_spline) {std::ranges::reverse(edge.exact_spline->poles);kernel::reverse_bspline_parameters(edge.exact_spline->knots,edge.exact_spline->weights);}
    }
    auto reversed_part=state->session.document();reversed_part.resolve_constructions(reversed_geometry);
    const auto& reversed_sketch=*std::ranges::find(reversed_part.sketches,profile_id,&sketcher::Sketch::id);
    for(std::size_t i=0;i<2;++i) {
        const auto* point=reversed_sketch.find_point(endpoint_ids[i]);near(reversed_sketch.world_point(point->x,point->y).x,before_switch[i]);
    }
    near(std::abs(before_switch[1]-before_switch[0]),26);
    run(host,"flat.set",{{"container",selected->reference.owner_id},{"thickness_mm",3.}});
    near(state->session.document().find_container(owner)->bend.thickness,3);
    run(host,"save");
    const auto reopened=document::PartDocument::load(directory/"sheet-attachment.prtz");
    check(reopened.find_container(owner)->bend.sheet_attachment,"Committed attachment lost on native reopen");
    std::cout<<"Sheet attachment: eight edges, both endpoints, zero/90/180 degrees, offsets and persistence passed\n";
}
void verify_attachment(const std::filesystem::path& directory) {
    kernel::OcctKernel kernel;
    const auto distance=[](kernel::Vec3 a,kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);};
    const auto close=[&](kernel::Vec3 a,kernel::Vec3 b){return distance(a,b)<1e-6;};
    for(double angle:{35.,90.,145.,180.})for(bool rotated:{false,true})for(bool reversed:{false,true}) {
        std::cout<<"Bend attachment angle="<<angle<<" rotated="<<rotated<<" reversed="<<reversed<<std::endl;
        auto source=document::PartDocument::create_default();
        auto first=document::PartDocument::create_sketch_container();first.feature_kind=document::FeatureKind::Bend;
        auto initial=sketcher::Sketch::create_default();initial.owner_container_id=first.id;
        static_cast<void>(initial.add_segment(-20,0,20,0));first.bend.sketch_id=initial.id;first.bend.angle_degrees=angle;
        if(rotated) {
            first.placement.x=75;first.placement.y=-31;first.placement.z=22;
            first.placement.absolute_rotation_x=31;first.placement.absolute_rotation_y=22;first.placement.absolute_rotation_z=53;
        }
        source.history={first};source.sketches={initial};source.resolve_constructions();
        // Each annotation and its grips must remain in its own Sketch plane,
        // including profiles rotated away from the global XY plane.
        auto dimension_feature=source.history.front();
        for(bool unbend:{false,true}) {
            dimension_feature.bend.unbend=unbend;
            document::prepare_bend_sketches(dimension_feature,source.sketches.front(),document::sheet_metal_defaults(source));
            std::vector<sketcher::Sketch> profiles{source.sketches.front()};
            for(const auto& data:dimension_feature.bend.auxiliary_sketches)profiles.push_back(sketcher::Sketch::from_serialized(data));
            for(const auto& sketch:profiles)for(const auto& dim:sketch.viewer_mesh().dimensions) {
                check(close(dim.plane_normal,sketch.resolved_normal),"Bend dimension/grip normal left its Sketch plane");
                if(dim.kind==kernel::ViewerDimensionKind::Linear) {
                    check(dim.measurement_direction.has_value(),"Bend coordinate lost its explicit measurement direction");
                    const auto axis=*dim.measurement_direction;
                    near(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z,1);
                    near(axis.x*sketch.resolved_normal.x+axis.y*sketch.resolved_normal.y+axis.z*sketch.resolved_normal.z,0);
                    near(std::abs(axis.x*sketch.resolved_x_axis.x+axis.y*sketch.resolved_x_axis.y+axis.z*sketch.resolved_x_axis.z),1);
                }
                for(const auto& point:{dim.witness_first,dim.witness_second,dim.line_first,dim.line_second}) {
                    const auto o=sketch.resolved_origin,n=sketch.resolved_normal;
                    near((point.x-o.x)*n.x+(point.y-o.y)*n.y+(point.z-o.z)*n.z,0);
                }
            }
        }
        const auto body=kernel.evaluate_history(source.kernel_operations()).back();
        near(body.volume,40*(angle*std::numbers::pi/180)*(36-25)/2);
        std::cout<<"  Source End calculated"<<std::endl;
        auto geometry=body.mesh.original_references;
        const auto end=sketcher::Sketch::from_serialized(source.history.front().bend.auxiliary_sketches[1]);
        const auto a=end.world_point(-20,0),b=end.world_point(20,0);
        const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e) {
            return e.points.size()>=2&&((close(e.points.front(),a)&&close(e.points.back(),b))||
                (close(e.points.front(),b)&&close(e.points.back(),a)));
        });
        if(edge==geometry.edges.end()) {
            std::cerr<<"Expected End "<<a.x<<','<<a.y<<','<<a.z<<" -> "<<b.x<<','<<b.y<<','<<b.z<<'\n';
            for(const auto& e:geometry.edges)if(!e.points.empty())std::cerr<<e.reference.semantic_key<<" "
                <<e.points.front().x<<','<<e.points.front().y<<','<<e.points.front().z<<" -> "
                <<e.points.back().x<<','<<e.points.back().y<<','<<e.points.back().z<<'\n';
            throw std::runtime_error("Calculated Bend has no outer End generatrix");
        }
        const auto face=std::ranges::find_if(geometry.triangle_references,[](const auto& f){return f.semantic_key.starts_with("sweep:cap:end:from:");});
        check(face!=geometry.triangle_references.end(),"Calculated Bend has no End cap");
        const auto vertex=std::ranges::find_if(geometry.points,[&](const auto& p){return close(p.position,a);});
        check(vertex!=geometry.points.end(),"Calculated Bend has no End vertex");
        if(reversed) {
            std::ranges::reverse(edge->points);
            std::ranges::reverse(edge->edge_treatment_endpoint_references);
            if(edge->exact_spline) {
                std::ranges::reverse(edge->exact_spline->poles);
                kernel::reverse_bspline_parameters(edge->exact_spline->knots,edge->exact_spline->weights);
            }
        }
        auto part=document::PartDocument::create_default();
        auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Bend;
        auto start=sketcher::Sketch::create_default();start.owner_container_id=feature.id;
        document::initialize_bend_start_profile(start,40);feature.bend.sketch_id=start.id;
        feature.placement.references={
            {{},edge->reference.owner_id,edge->reference.semantic_key,0,false,"front",true},
            {{},face->owner_id,face->semantic_key,0,true,"top",true}};
        const auto direction=document::bend_attachment_profile_direction(feature.placement.references,geometry);
        check(direction.has_value(),"Edge and narrow End face did not define a Bend attachment");
        check(document::point_constraint_state(feature.placement.references,geometry,a).remaining_dof==1,
            "Edge/face must leave one translation along the edge");
        check(document::orientation_constraint_state(feature.placement.references,geometry,true,a).remaining_dof==0,
            "Edge/face must fix all rotations without a third planar face");
        feature.placement.references.push_back({{},vertex->reference.owner_id,vertex->reference.semantic_key});
        part.history={feature};part.sketches={start};part.resolve_constructions(geometry);
        check(part.history.front().placement.reference_valid,"Edge/End face/vertex placement failed");
        const auto resolved=part.sketches.front();
        check(resolved.plane==sketcher::SketchPlane::XY&&resolved.plane_auto,"Automatic Bend start plane is not the attachment plane");
        check(close(resolved.world_point(0,0),a)&&close(resolved.world_point(40,0),b),
            "Bend outer start generatrix does not coincide with selected edge");
        check(close(resolved.resolved_y_axis,end.resolved_y_axis),"Bend material points outside the attachment face");
        check(close(resolved.resolved_normal,end.resolved_normal),"Bend does not leave the End face outwards");
        for(const auto& endpoint:edge->edge_treatment_endpoint_references) {
            auto automatic=part;
            automatic.history.front().bend.sheet_attachment=true;
            automatic.history.front().placement.references=document::bend_sheet_references(*edge,endpoint);
            automatic.resolve_constructions(geometry);
            const auto& profile=automatic.sketches.front();
            check(automatic.history.front().placement.reference_valid&&profile.external_references.size()==2,
                "Automatic Bend-to-Bend attachment did not bind both endpoints");
            check(close(profile.resolved_y_axis,end.resolved_y_axis)&&close(profile.resolved_normal,end.resolved_normal),
                "Automatic Bend-to-Bend attachment reversed the material or outgoing direction");
            const auto& segment=profile.segments.front();
            const auto* first=profile.find_point(segment.first_point_id);
            const auto* last=profile.find_point(segment.second_point_id);
            const auto first_world=profile.world_point(first->x,first->y);
            const auto last_world=profile.world_point(last->x,last->y);
            check((close(first_world,a)&&close(last_world,b))||(close(first_world,b)&&close(last_world,a)),
                "Automatic Bend-to-Bend start profile left its selected edge");
        }
        const auto refs=part.history.front().placement.references;
        for(bool last:{false,true}) {
            const auto inner=end.world_point(last?20:-20,1);
            const auto corner=std::ranges::find_if(geometry.points,[&](const auto& p){return close(p.position,inner);});
            check(corner!=geometry.points.end(),"Calculated Bend has no opposite End corner");
            part.history.front().placement.references[2]={{},corner->reference.owner_id,corner->reference.semantic_key};
            part.resolve_constructions(geometry);
            check(part.history.front().placement.reference_valid&&close(part.sketches.front().resolved_origin,last?b:a),
                "Opposite End corner did not project onto the selected edge");
            const auto state=document::point_constraint_state(part.history.front().placement.references,geometry,part.sketches.front().resolved_origin);
            check(state.remaining_dof==0&&state.third_point_is_station,"Projected point has incorrect DOF or presentation");
            auto prepared=start;
            document::orient_bend_start_toward_edge(prepared,part.history.front().placement,geometry);
            auto seeded=part;seeded.sketches.front()=prepared;seeded.resolve_constructions(geometry);
            const auto& line=seeded.sketches.front();const auto& s=line.segments.front();
            check(close(line.world_point(line.find_point(s.first_point_id)->x,0),a)&&
                close(line.world_point(line.find_point(s.second_point_id)->x,0),b),
                "Prepared line points away from the selected edge at its opposite endpoint");
            check(close(line.resolved_y_axis,end.resolved_y_axis),"Reversing the initial span reversed the material side");
            auto edited=line;
            const double coordinate=line.find_point(s.first_point_id)->x;
            check(edited.set_dimension_value(line.id+":position:first",coordinate),"Cannot re-enter the prepared Bend coordinate");
            near(edited.find_point(s.first_point_id)->x,coordinate);
            near(sketcher::dimension_display_value(edited.dimensions.front()),coordinate);
        }
        // Point-first remains coincidence: an off-edge point conflicts with
        // the selected edge, rather than silently acquiring projection meaning.
        auto coincident=part.history.front().placement;
        coincident.references={coincident.references[2],refs[0],refs[1]};
        check(!document::resolve_placement(coincident,geometry),"Point-first anchor was silently projected");
        part.history.front().placement.references=refs;part.resolve_constructions(geometry);
        const auto start_id=resolved.segments.front().id;
        for(bool unbend:{false,true,false}) {
            std::cout<<"  Attached state "<<(unbend?"unbend":"bend")<<std::endl;
            part.history.front().bend.unbend=unbend;part.resolve_constructions(geometry);
            check(close(part.sketches.front().resolved_x_axis,resolved.resolved_x_axis)&&
                close(part.sketches.front().resolved_normal,resolved.resolved_normal),"Bend/Unbend moved the attached start profile");
            const auto result=kernel.evaluate_history(part.kernel_operations()).back();
            near(result.volume,40*std::numbers::pi/2*5.5);
        }
        part.save(directory/"bend-attachment.prtz");
        auto reopened=document::PartDocument::load(directory/"bend-attachment.prtz");reopened.resolve_constructions(geometry);
        check(close(reopened.sketches.front().resolved_y_axis,resolved.resolved_y_axis),"Reopening changed Bend material side");
        // Point first and curve second is the same geometric attachment.
        auto& reordered=part.history.front().placement.references;
        reordered={refs[2],refs[0],refs[1]};part.resolve_constructions(geometry);
        check(close(part.sketches.front().resolved_x_axis,resolved.resolved_x_axis),"Point-first attachment changed Bend frame");
        // Losing the source preserves the last complete frame and its authored IDs.
        part.resolve_constructions({});
        check(!part.history.front().placement.reference_valid&&
            close(part.sketches.front().resolved_normal,resolved.resolved_normal)&&
            close(part.sketches.front().resolved_y_axis,resolved.resolved_y_axis)&&
            part.sketches.front().segments.front().id==start_id,"Missing source lost Bend frame or profile identity");
        part.sketches.front().plane_auto=false;part.sketches.front().plane=sketcher::SketchPlane::XY;
        part.resolve_constructions(geometry);
        check(close(part.sketches.front().resolved_x_axis,resolved.resolved_x_axis),"Selecting explicit XY changed the automatic XY attachment axes");
        part.sketches.front().plane=sketcher::SketchPlane::YZ;
        part.resolve_constructions(geometry);
        check(part.sketches.front().plane==sketcher::SketchPlane::XY&&part.sketches.front().plane_auto,
            "Bend did not derive its profile plane from the attachment");
    }
    std::cout<<"16 Bend-to-Bend attachments: rotated frames, reversed edges, point anchor, state, persistence and missing references passed\n";
}
void verify_prepared_start() {
    std::cout<<"Prepared Bend profile dimension edits"<<std::endl;
    auto start=sketcher::Sketch::create_default();
    auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Bend;
    start.owner_container_id=feature.id;feature.bend.sketch_id=start.id;
    document::initialize_bend_start_profile(start,40);
    check(start.constraints.size()==2&&start.dimensions.size()==2,"Bend start needs two C constraints and two origin dimensions");
    const auto first=start.segments.front().first_point_id,last=start.segments.front().second_point_id;
    near(start.find_point(first)->x,0);near(start.find_point(last)->x,40);
    const auto initial=start;
    for(double first_value:{5.,-7.,0.})for(double last_value:{35.,52.}) {
        std::cout<<"Bend origin dimensions "<<first_value<<", "<<last_value<<std::endl;
        start=initial;feature.bend.auxiliary_sketches={};
        check(start.set_dimension_value(start.id+":position:first",first_value),"Cannot edit first Bend origin dimension");
        check(start.set_dimension_value(start.id+":position:last",last_value),"Cannot edit last Bend origin dimension");
        near(start.find_point(first)->y,0);near(start.find_point(last)->y,0);
        near(start.find_point(first)->x,first_value);near(start.find_point(last)->x,last_value);
        document::prepare_bend_sketches(feature,start,{});
        auto end=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[1]);
        near(end.find_point(end.id+":reference:first")->x,first_value);
        near(end.find_point(end.id+":reference:last")->x,last_value);
        check(end.constraints.size()==2&&end.dimensions.size()==2,"Bend end needs two C constraints and two difference dimensions");
        check(end.set_dimension_value(end.id+":difference:first",3),"Cannot edit prepared first end difference");
        check(end.set_dimension_value(end.id+":difference:last",4),"Cannot edit prepared last end difference");
        document::accept_bend_sketch(feature,start,1,end,{});
        const auto ext=document::bend_profile_extensions(feature);near(ext[0],3);near(ext[1],4);
    }
}
void verify(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","bend-test"}});
    const auto id=live.active_document_id();
    const auto created=run(host,"bend.create",{{"width_mm",40.},{"radius_mm",5.},{"angle_degrees",90.}}).data;
    const auto owner=created.at("container").get<std::string>();auto* state=live.open_part(id);
    const auto volume=[&]{return state->session.calculated_boundaries().back().volume;};
    const double pi=std::numbers::pi;
    const double initial_k=created.at("k_factor");
    near(volume(),40*pi/2*(36-25)/2); // Annular sector: width * angle * (Ro²-Ri²)/2.
    {
        const auto unchanged=*state->session.document().find_container(owner);
        const auto unchanged_profile=workspace::document_sketch(live,id,unchanged.bend.sketch_id);
        state->session.update_calculated_boundaries({});
        static_cast<void>(workspace::commit_bend(live,kernel,id,unchanged,unchanged_profile));
        check(!state->session.calculated_boundaries().empty(),"Bend OK left its body uncalculated");
        near(volume(),40*pi/2*(36-25)/2);
    }
    check(created.at("thickness_mm")==1.&&!created.at("thickness_override").get<bool>(),"Bend did not inherit default 1 mm");
    run(host,"bend.set",{{"container",owner},{"radius_follows_thickness",true}});
    near(volume(),40*pi/2*(4-1)/2);
    check(run(host,"bend.get",{{"container",owner}}).data.at("radius_mm")==1.,"Linked radius did not inherit thickness");
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",1.5}}}});
    near(volume(),40*pi/2*(9-2.25)/2);
    check(run(host,"bend.get",{{"container",owner}}).data.at("radius_mm")==1.5,"Linked radius did not follow regenerated Part thickness");
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",1.}}}});run(host,"regenerate");
    check(!host.execute({{"command","bend.set"},{"arguments",{{"container",owner},{"radius_mm",8.}}}}).ok,
        "Manual radius changed while linked to thickness");
    run(host,"bend.set",{{"container",owner},{"thickness_mm",2.}});near(volume(),40*pi/2*(16-4)/2);
    run(host,"save");
    check(document::PartDocument::load(directory/"bend-test.prtz").find_container(owner)->bend.radius_follows_thickness,
        "Radius/thickness link was lost on reload");
    run(host,"bend.set",{{"container",owner},{"radius_follows_thickness",false}});
    check(run(host,"bend.get",{{"container",owner}}).data.at("radius_mm")==2.,"Unlinking radius changed its effective value");
    run(host,"bend.set",{{"container",owner},{"radius_mm",5.},{"thickness_override",false}});
    near(volume(),40*pi/2*(36-25)/2);
    const auto bent_body=state->session.calculated_boundaries().back();
    const auto bent_faces=faces(bent_body,owner);
    // Exercise the actual Drawing measuring contract against calculated body
    // edges, not merely the stored Bend radius parameter or a preview.
    auto drawing_view=drawing::DrawingDocument::create_view(id,{},bent_body.mesh,drawing::ViewOrientation::Front);
    const auto start_profile=*std::ranges::find(state->session.document().sketches,
        state->session.document().find_container(owner)->bend.sketch_id,&sketcher::Sketch::id);
    const auto trajectory=sketcher::Sketch::from_serialized(state->session.document().find_container(owner)->bend.auxiliary_sketches[0]);
    verify_sketches(*state->session.document().find_container(owner),start_profile,document::sheet_metal_defaults(state->session.document()));
    drawing_view.camera={trajectory.resolved_x_axis,trajectory.resolved_y_axis,trajectory.resolved_normal};
    drawing_view.projected_edges=drawing::project_edges(bent_body.mesh,drawing_view.camera);
    int measured_arcs=0;
    for(const auto& curve:drawing::projected_measurement_curves(drawing_view))if(curve.circular) {
        auto dimension=drawing::make_drawing_dimension(drawing_view.id,drawing::DrawingDimensionKind::Radius);
        dimension.attachments={{drawing::DimensionAttachmentKind::CurvePoint,curve.source,{},.5}};
        const auto evaluated=drawing::evaluate_drawing_dimension(drawing_view,dimension);
        check(evaluated.state==drawing::MeasurementState::Resolved,"Bend radius cannot be measured in Drawing");
        const auto radius=evaluated.presentations.front().value;
        check(std::abs(radius-5)<1e-5||std::abs(radius-6)<1e-5,"Drawing measured a wrong Bend radius");
        ++measured_arcs;
    }
    check(measured_arcs>=4,"Calculated Bend did not preserve measurable circular edges");
    const auto edit_path_radius=[&](double value) {
        check(workspace::mutate_document_sketch(live,id,trajectory.id,[&](auto& sketch){
            check(sketch.set_dimension_value(trajectory.id+":radius",value),"Shared Sketch edit could not solve Bend radius");
        }),"Shared Sketch mutation did not change Bend");
        run(host,"regenerate");
    };
    edit_path_radius(8.);near(volume(),40*pi/2*(64-49)/2);
    edit_path_radius(6.);near(volume(),40*pi/2*(36-25)/2);
    check(bent_faces.size()==6,"Bend must have six authored faces");
    check(std::ranges::count_if(bent_faces,[](const auto& s){return s.starts_with("sweep:cap:start:from:")||s.starts_with("sweep:cap:end:from:");})==2,"Start/End cap ancestry missing");
    run(host,"bend.set",{{"container",owner},{"state","unbend"}});near(volume(),40*pi/2*(5+initial_k));
    check(faces(state->session.calculated_boundaries().back(),owner)==bent_faces,"Bend/Unbend changed face identities");
    for(const auto& key:bent_faces)if(key.starts_with("sweep:cap:start:from:")||key.starts_with("sweep:cap:end:from:")) {
        auto plane=document::PartDocument::create_construction(document::ConstructionKind::Plane);
        plane.definition=document::ConstructionDefinition::PlaneReference;plane.references={{{},owner,key}};
        check(document::resolve_construction(plane,bent_body.mesh.original_references),"Cannot attach a plane to bent cap");
        const auto direction=plane.direction;
        check(document::resolve_construction(plane,state->session.calculated_boundaries().back().mesh.original_references),"Cap reference did not survive Unbend");
        const auto dot=direction.x*plane.direction.x+direction.y*plane.direction.y+direction.z*plane.direction.z;
        near(std::abs(dot),key.starts_with("sweep:cap:start:")?1.:0.);
    }
    const auto& saved=state->session.document();const auto* feature=saved.find_container(owner);
    const auto sketch=*std::ranges::find(saved.sketches,feature->bend.sketch_id,&sketcher::Sketch::id);
    const auto preview=document::bend_preview(*feature,sketch,document::sheet_metal_defaults(saved));
    check(preview.edges.size()==12&&preview.axes.size()==1,"Unbend preview or bend axis missing");
    run(host,"undo");near(volume(),40*pi/2*(36-25)/2);run(host,"redo");near(volume(),40*pi/2*(5+initial_k));
    const auto revision=state->session.revision();
    check(!run(host,"bend.set",{{"container",owner},{"state","unbend"}}).data.at("changed").get<bool>(),"No-op Bend edit changed history");
    for(auto bad: {Json{{"radius_mm",-1}},Json{{"angle_degrees",181}},Json{{"angle_degrees",-1}},Json{{"thickness_mm",0}},Json{{"k_factor",1.1}},Json{{"state","unknown"}}}) {
        bad["container"]=owner;check(!host.execute({{"command","bend.set"},{"arguments",bad}}).ok,"Invalid Bend parameters accepted");
    }
    check(state->session.revision()==revision,"Rejected Bend edit changed history");
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",2.},{"k_factor",.4}}}});
    near(volume(),40*2*pi/2*5.8); // Settings confirmation calculates inherited thickness and K together.
    run(host,"undo");near(volume(),40*pi/2*(5+initial_k));
    run(host,"redo");near(volume(),40*2*pi/2*5.8);
    run(host,"bend.set",{{"container",owner},{"thickness_mm",1.5},{"k_factor",.3}});near(volume(),40*1.5*pi/2*5.45);
    run(host,"bend.set",{{"container",owner},{"thickness_override",false},{"k_factor_override",false}});near(volume(),40*2*pi/2*5.8);
    run(host,"bend.set",{{"container",owner},{"state","bend"},{"angle_degrees",180.}});near(volume(),40*pi*(49-25)/2);
    check(faces(state->session.calculated_boundaries().back(),owner)==bent_faces,"180-degree Bend changed face identity");
    run(host,"bend.set",{{"container",owner},{"angle_degrees",0.}});near(volume(),0);
    check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Zero angle produced a calculation error");
    run(host,"bend.set",{{"container",owner},{"angle_degrees",90.}});
    run(host,"bend.set",{{"container",owner},{"first_extension_mm",3.},{"last_extension_mm",10.}});
    // Linear width transition: average width times the annular-sector area.
    near(volume(),46.5*pi/2*(49-25)/2);
    const auto sources=workspace::drawing_annotation_sources(&live,id,{});
    drawing::refresh_model_annotations(drawing_view,sources);
    const auto radius_annotation=std::ranges::find_if(drawing_view.model_annotations,[&](const auto& annotation){
        return annotation.source.owner_id==trajectory.id&&annotation.source.semantic_id=="dimension:"+trajectory.id+":radius";
    });
    check(radius_annotation!=drawing_view.model_annotations.end()&&!radius_annotation->unresolved,
        "Variable-width Bend lost its authored Drawing radius annotation");
    near(radius_annotation->value,7.);
    check(faces(state->session.calculated_boundaries().back(),owner)==bent_faces,"End profile widths changed face identity");
    run(host,"bend.set",{{"container",owner},{"state","unbend"}});
    near(volume(),46.5*2*pi/2*5.8);
    run(host,"bend.set",{{"container",owner},{"first_extension_mm",-3.},{"last_extension_mm",-10.}});
    near(volume(),33.5*2*pi/2*5.8);
    const auto rejected_revision=state->session.revision();
    check(!host.execute({{"command","bend.set"},{"arguments",{{"container",owner},{"first_extension_mm",-40.},{"last_extension_mm",0.}}}}).ok,
        "Collapsed end profile was accepted");
    check(state->session.revision()==rejected_revision,"Rejected end profile changed history");
    run(host,"bend.set",{{"container",owner},{"first_extension_mm",0.},{"last_extension_mm",0.},{"state","bend"}});
    run(host,"bend.set",{{"container",owner},{"radius_mm",0.},{"angle_degrees",180.}});
    near(volume(),40*pi*4/2);
    const auto zero_faces=faces(state->session.calculated_boundaries().back(),owner);
    check(zero_faces.size()==5&&std::ranges::all_of(zero_faces,[&](const auto& key){return bent_faces.contains(key);}),
        "R=0 replaced surviving face identities or kept a degenerate inner face");
    const auto zero_revision=state->session.revision();
    check(!host.execute({{"command","bend.set"},{"arguments",{{"container",owner},{"first_extension_mm",3.}}}}).ok,
        "R=0 accepted an unsupported variable-width profile");
    check(state->session.revision()==zero_revision,"Rejected R=0 transition changed history");
    for(double angle:{45.,90.,180.}) {
        run(host,"bend.set",{{"container",owner},{"angle_degrees",angle}});near(volume(),40*angle*pi/180*4/2);
        check(faces(state->session.calculated_boundaries().back(),owner)==zero_faces,"Zero-radius angle changed surviving face identities");
    }
    run(host,"bend.set",{{"container",owner},{"state","unbend"}});near(volume(),40*2*pi*.8);
    check(!host.execute({{"command","bend.set"},{"arguments",{{"container",owner},{"k_factor",0.}}}}).ok,
        "Zero developed length created a degenerate solid");
    run(host,"bend.set",{{"container",owner},{"radius_mm",5.},{"angle_degrees",90.},{"state","bend"}});
    run(host,"save");std::vector<kernel::BodyResult> cached;
    const auto loaded=document::PartDocument::load(directory/"bend-test.prtz",&cached);
    check(loaded.find_container(owner)->bend==state->session.document().find_container(owner)->bend,"Native Bend parameters changed on reload");
    check(faces(cached.back(),owner)==bent_faces,"Native cached face identity changed on reload");
    auto bad_sketch=sketch;static_cast<void>(bad_sketch.add_segment(0,20,10,20));const auto before=state->session.revision();
    try{static_cast<void>(workspace::commit_bend(live,kernel,id,*state->session.document().find_container(owner),bad_sketch));throw std::runtime_error("Multiple Bend segments accepted");}
    catch(const std::invalid_argument&){}
    check(state->session.revision()==before,"Invalid owned Sketch changed the Part");
    document::FamilyTable table;table.columns={"Bend angle"};table.bindings["Bend angle"]={"dimension",owner,"parameter:angle"};
    table.instances={{"Half angle",{{"Bend angle","45"}}},{"Zero",{{"Bend angle","0"}}}};
    const auto references=workspace::family_references(live,id);
    check(std::ranges::any_of(references,[&](const auto& ref){return ref.binding.owner_id==trajectory.id;}),
        "Bend auxiliary dimensions have no Family Table identifiers");
    const auto end_id=sketcher::Sketch::from_serialized(state->session.document().find_container(owner)->bend.auxiliary_sketches[1]).id;
    table.columns.push_back("Outside radius");table.columns.push_back("First extension");
    table.bindings["Outside radius"]={"dimension",trajectory.id,"dimension:"+trajectory.id+":radius"};
    table.bindings["First extension"]={"dimension",end_id,"dimension:"+end_id+":difference:first"};
    table.instances.push_back({"Wide",{{"Bend angle","90"},{"Outside radius","9"},{"First extension","3"}}});
    static_cast<void>(workspace::set_family_table(live,id,table));
    const auto half=workspace::open_family_instance(live,kernel,id,"Half angle",false);
    near(live.open_part(half)->session.calculated_boundaries().back().volume,40*pi/4*(49-25)/2);
    const auto zero=workspace::open_family_instance(live,kernel,id,"Zero",false);
    near(live.open_part(zero)->session.calculated_boundaries().back().volume,0);
    const auto wide=workspace::open_family_instance(live,kernel,id,"Wide",false);
    near(live.open_part(wide)->session.calculated_boundaries().back().volume,41.5*pi/2*(81-49)/2);
    run(host,"new",{{"type","assembly"},{"name","no-bend"}});
    check(!host.execute({{"command","bend.create"},{"arguments",Json::object()}}).ok,"Assembly accepted Bend");
}
}
int main() {
    const auto directory=std::filesystem::temp_directory_path()/("zima-bend-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try{verify_cross_branch_box(directory);verify_sheet_attachment(directory);verify_prepared_start();verify_attachment(directory);verify(directory);std::filesystem::remove_all(directory);std::cout<<"Bend geometry, identities, defaults, history and persistence passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"; fixture: "<<directory<<'\n';return 1;}
}

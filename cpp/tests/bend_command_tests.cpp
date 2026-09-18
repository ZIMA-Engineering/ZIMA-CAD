#include <zima/command_host/host.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
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
void near_clearance_volume(double actual,double expected) {
    if(std::abs(actual-expected)>1e-3)
        throw std::runtime_error("Expected clearance volume "+std::to_string(expected)+", got "+std::to_string(actual));
}
void verify_clearance_passage(const kernel::OcctKernel& kernel,const kernel::BodyResult& result,
        double x,double y,double width,double height) {
    kernel::BoxRequest prism(width,height,200.);prism.translation={x,y,-100.};
    const auto cutter=kernel.make_box(prism);
    const auto recut=kernel.subtract_bodies(result,cutter,{},{},1e-7);
    near(recut.volume,result.volume);
}
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
    check_profile(150.);
    check(workspace::mutate_document_sketch(live,id,source_profile,[](auto& sketch) {
        const auto dimension=std::ranges::find_if(sketch.dimensions,[](const auto& d){return d.driving&&std::abs(d.value-150.)<1e-8;});
        check(dimension!=sketch.dimensions.end(),"Source wall height dimension is missing");
        const auto dimension_id=dimension->id;
        check(sketch.set_dimension_value(dimension_id,175.),"Source wall height edit failed");
    }),"Cannot change the source wall height in the unfolded state");
    run(host,"regenerate");check_profile(175.);
    run(host,"save");
    const auto reopened=document::PartDocument::load(path);
    run(host,"close",{{"document",id},{"discard",true}});
    run(host,"open",{{"path",path.string()}});check_profile(175.);
    auto sketch=workspace::document_sketch(live,id,profile);
    static_cast<void>(workspace::refresh_sketch_reference_snapshot(live,id,sketch));
    near(sketch.points[2].y,175.);
    const auto& face=original.external_references.back();
    const auto recreated=workspace::prepare_sketch_external_reference(live,id,sketch,face.kind,face.source_owner_id,face.source_semantic_key,{});
    near(recreated.cached_points.front()[1],175.);
    check_profile(175.);
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
    document::prepare_bend_sketches(feature,start,defaults);
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
        {
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
        part.resolve_constructions(geometry);
        check(close(part.sketches.front().resolved_x_axis,resolved.resolved_x_axis),"Regeneration moved the attached start profile");
        near(kernel.evaluate_history(part.kernel_operations()).back().volume,40*std::numbers::pi/2*5.5);
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
    const auto& saved=state->session.document();const auto* feature=saved.find_container(owner);
    const auto sketch=*std::ranges::find(saved.sketches,feature->bend.sketch_id,&sketcher::Sketch::id);
    const auto preview=document::bend_preview(*feature,sketch,document::sheet_metal_defaults(saved));
    check(preview.edges.size()==12&&preview.axes.size()==1,"Sheet Profile preview or axis missing");
    const auto revision=state->session.revision();
    for(auto bad: {Json{{"radius_mm",-1}},Json{{"angle_degrees",181}},Json{{"angle_degrees",-1}},Json{{"thickness_mm",0}},Json{{"k_factor",1.1}},Json{{"state","unknown"}}}) {
        bad["container"]=owner;check(!host.execute({{"command","bend.set"},{"arguments",bad}}).ok,"Invalid Bend parameters accepted");
    }
    check(state->session.revision()==revision,"Rejected Bend edit changed history");
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",2.},{"k_factor",.4}}}});
    near(volume(),40*pi/2*(49-25)/2); // Settings confirmation calculates inherited thickness and K together.
    run(host,"undo");near(volume(),40*pi/2*(36-25)/2);
    run(host,"redo");near(volume(),40*pi/2*(49-25)/2);
    run(host,"bend.set",{{"container",owner},{"thickness_mm",1.5},{"k_factor",.3}});near(volume(),40*pi/2*(6.5*6.5-25)/2);
    run(host,"bend.set",{{"container",owner},{"thickness_override",false},{"k_factor_override",false}});near(volume(),40*pi/2*(49-25)/2);
    run(host,"bend.set",{{"container",owner},{"angle_degrees",180.}});near(volume(),40*pi*(49-25)/2);
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
    run(host,"bend.set",{{"container",owner},{"first_extension_mm",-3.},{"last_extension_mm",-10.}});
    near(volume(),33.5*pi/2*(49-25)/2);
    const auto rejected_revision=state->session.revision();
    check(!host.execute({{"command","bend.set"},{"arguments",{{"container",owner},{"first_extension_mm",-40.},{"last_extension_mm",0.}}}}).ok,
        "Collapsed end profile was accepted");
    check(state->session.revision()==rejected_revision,"Rejected end profile changed history");
    run(host,"bend.set",{{"container",owner},{"first_extension_mm",0.},{"last_extension_mm",0.}});
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
    run(host,"bend.set",{{"container",owner},{"radius_mm",5.},{"angle_degrees",90.}});
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
void verify_continuation(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","bend-continuation"}});
    const auto owner=run(host,"bend.create",{{"width_mm",40.},{"radius_mm",5.},{"angle_degrees",90.}}).data.at("container").get<std::string>();
    const auto id=live.active_document_id();auto* state=live.open_part(id);
    auto feature=*state->session.document().find_container(owner);
    const auto start=workspace::document_sketch(live,id,feature.bend.sketch_id);
    const auto arc_faces=faces(state->session.calculated_boundaries().back(),owner);
    const auto end_before=feature.bend.auxiliary_sketches[1];
    auto path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    const auto arc=path.arcs.front();const auto join=*path.find_point(arc.end_point_id);
    const auto line=path.add_segment(join.x,join.y,join.x,join.y+20.);
    static_cast<void>(path.add_segment_constraint(line,sketcher::ConstraintKind::Vertical));
    document::accept_bend_sketch(feature,start,0,path,document::sheet_metal_defaults(state->session.document()));
    check(feature.bend.auxiliary_sketches[1]==end_before,"Continuation moved or replaced the arc end profile");
    static_cast<void>(workspace::commit_bend(live,kernel,id,feature,start));
    const auto volume=[&]{return state->session.calculated_boundaries().back().volume;};
    near(volume(),40*std::numbers::pi/2*(36-25)/2+800);
    const auto folded_faces=faces(state->session.calculated_boundaries().back(),owner);
    for(const auto& key:arc_faces)check(folded_faces.contains(key),"Continuation replaced an original arc face reference");
    const auto& continued_body=state->session.calculated_boundaries().back();
    check(std::ranges::any_of(continued_body.mesh.original_references.edges,[&](const auto& edge) {
        return edge.reference.owner_id==owner&&kernel::sheet_edge_role(edge)==kernel::SheetEdgeRole::Boundary&&
            edge.reference.semantic_key.find(path.segments.back().second_point_id)!=std::string::npos;
    }),"Continuation end has no persisted sheet boundary edge");
    feature=*state->session.document().find_container(owner);
    path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    auto length_dimension=*std::ranges::find(path.dimensions,line+":length",&sketcher::SketchDimension::id);
    length_dimension.value=30.;path.apply_dimension(length_dimension);
    document::accept_bend_sketch(feature,start,0,path,document::sheet_metal_defaults(state->session.document()));
    static_cast<void>(workspace::commit_bend(live,kernel,id,feature,start));
    near(document::bend_straight_length(*state->session.document().find_container(owner)),30.);
    near(volume(),40*std::numbers::pi/2*(36-25)/2+1200);
    feature=*state->session.document().find_container(owner);
    path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    check(path.set_dimension_value(path.id+":angle",180.),"Angle dimension rejected continuation at 180 degrees");
    document::accept_bend_sketch(feature,start,0,path,document::sheet_metal_defaults(state->session.document()));
    static_cast<void>(workspace::commit_bend(live,kernel,id,feature,start));
    near(volume(),40*std::numbers::pi*(36-25)/2+1200);
    for(double angle:{45.,180.,0.,90.}) {
        run(host,"bend.set",{{"container",owner},{"angle_degrees",angle}});
        near(volume(),40*angle*std::numbers::pi/180*(36-25)/2+1200);
        near(document::bend_straight_length(*state->session.document().find_container(owner)),30.);
    }
    run(host,"save");
    const auto saved=document::PartDocument::load(directory/"bend-continuation.prtz");
    near(document::bend_straight_length(*saved.find_container(owner)),30.);
    run(host,"bend.set",{{"container",owner},{"radius_mm",0.},{"angle_degrees",180.}});
    near(volume(),40*std::numbers::pi/2+1200);
    feature=*state->session.document().find_container(owner);
    path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
    path.remove_geometry(line);
    document::accept_bend_sketch(feature,start,0,path,document::sheet_metal_defaults(state->session.document()));
    static_cast<void>(workspace::commit_bend(live,kernel,id,feature,start));
    near(document::bend_straight_length(*state->session.document().find_container(owner)),0.);
    near(volume(),40*std::numbers::pi/2);
    run(host,"undo");near(volume(),40*std::numbers::pi/2+1200);
    run(host,"redo");near(volume(),40*std::numbers::pi/2);
}
void verify_continuation_side_attachment(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","bend-continuation-side"}});
    const auto first=run(host,"bend.create",{{"width_mm",40.},{"radius_mm",5.},{"angle_degrees",90.}}).data.at("container").get<std::string>();
    const auto id=live.active_document_id();auto* state=live.open_part(id);
    auto source=*state->session.document().find_container(first);
    const auto start=workspace::document_sketch(live,id,source.bend.sketch_id);
    auto path=sketcher::Sketch::from_serialized(source.bend.auxiliary_sketches[0]);
    const auto arc=path.arcs.front();const auto join=*path.find_point(arc.end_point_id);
    const auto continuation=path.add_segment(join.x,join.y,join.x,join.y+20.);
    static_cast<void>(path.add_segment_constraint(continuation,sketcher::ConstraintKind::Vertical));
    document::accept_bend_sketch(source,start,0,path,document::sheet_metal_defaults(state->session.document()));
    static_cast<void>(workspace::commit_bend(live,kernel,id,source,start));
    const auto edge_length=[](const auto& edge) {
        double value{};for(std::size_t i=1;i<edge.points.size();++i)value+=std::hypot(
            edge.points[i].x-edge.points[i-1].x,edge.points[i].y-edge.points[i-1].y,
            edge.points[i].z-edge.points[i-1].z);return value;
    };
    const auto& geometry=state->session.calculated_boundaries().back().mesh.original_references;
    const auto side=std::ranges::find_if(geometry.edges,[&](const auto& edge) {
        return edge.reference.owner_id==first&&kernel::sheet_edge_role(edge)==kernel::SheetEdgeRole::Boundary&&
            edge.edge_treatment_endpoint_references.size()==2&&std::abs(edge_length(edge)-20.)<1e-5;
    });
    check(side!=geometry.edges.end(),"Sheet Profile tangent continuation has no selectable longitudinal boundary edge");
    const auto second=run(host,"bend.create",{{"edge_owner",side->reference.owner_id},{"edge_key",side->reference.semantic_key},
        {"radius_mm",3.},{"angle_degrees",60.}}).data.at("container").get<std::string>();
    const auto& attached=*state->session.document().find_container(second);
    const auto attached_sketch_id=attached.bend.sketch_id;
    check(attached.bend.sheet_attachment,"Sheet Profile did not attach to the tangent continuation side edge");
    const auto attached_start=workspace::document_sketch(live,id,attached.bend.sketch_id);
    const auto& attached_segment=*std::ranges::find_if(attached_start.segments,[](const auto& segment){return !segment.construction;});
    const auto* attached_first=attached_start.find_point(attached_segment.first_point_id);
    const auto* attached_last=attached_start.find_point(attached_segment.second_point_id);
    near(std::hypot(attached_last->x-attached_first->x,attached_last->y-attached_first->y),20.);
    check(state->session.calculated_boundaries().back().calculation_errors.empty(),
        "Sheet Profile attached to a tangent continuation side edge did not calculate");
    source=*state->session.document().find_container(first);
    path=sketcher::Sketch::from_serialized(source.bend.auxiliary_sketches[0]);
    check(path.set_dimension_value(continuation+":length",30.),
        "Tangent continuation length could not be edited");
    document::accept_bend_sketch(source,workspace::document_sketch(live,id,source.bend.sketch_id),0,path,
        document::sheet_metal_defaults(state->session.document()));
    static_cast<void>(workspace::commit_bend(live,kernel,id,source,
        workspace::document_sketch(live,id,source.bend.sketch_id)));
    const auto resized=workspace::document_sketch(live,id,attached_sketch_id);
    const auto& resized_segment=*std::ranges::find_if(resized.segments,[](const auto& segment){return !segment.construction;});
    const auto* resized_first=resized.find_point(resized_segment.first_point_id);
    const auto* resized_last=resized.find_point(resized_segment.second_point_id);
    near(std::hypot(resized_last->x-resized_first->x,resized_last->y-resized_first->y),30.);
    const auto origin_before=resized.resolved_origin;
    run(host,"bend.set",{{"container",first},{"angle_degrees",45.}});
    const auto reframed=workspace::document_sketch(live,id,attached_sketch_id);
    check(std::hypot(reframed.resolved_origin.x-origin_before.x,reframed.resolved_origin.y-origin_before.y,
        reframed.resolved_origin.z-origin_before.z)>1e-4,
        "Downstream Sheet Profile did not follow the edited tangent continuation");
    check(state->session.calculated_boundaries().back().calculation_errors.empty(),
        "Editing the upstream Sheet Profile invalidated its side attachment");
    run(host,"save");std::vector<kernel::BodyResult> restored;
    const auto saved=document::PartDocument::load(directory/"bend-continuation-side.prtz",&restored);
    check(saved.find_container(first)&&saved.find_container(second)&&!restored.empty()&&
        restored.back().calculation_errors.empty(),
        "Tangent continuation side attachment did not survive save and reload");
    const auto recalculated=kernel.evaluate_history(saved.kernel_operations());
    check(!recalculated.empty()&&recalculated.back().calculation_errors.empty(),
        "Fresh regeneration lost the tangent continuation side attachment");
}
void verify_sheet_cut(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","sheet-cut-cylinder"}});
    const auto id=live.active_document_id();auto* state=live.open_part(id);
    auto source_sketch=sketcher::Sketch::create_default();auto source=document::PartDocument::create_revolution_container(source_sketch.id);
    document::initialize_sheet_revolution(source,source_sketch,document::sheet_metal_defaults(state->session.document()));
    workspace::commit_profile(live,kernel,id,source,workspace::ProfileEditMode::Create,source_sketch);
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(10,1,20,5));
    auto cut=document::PartDocument::create_extrusion_container(sketch.id);cut.name="Sheet Cut";
    cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;
    cut.extrusion.end_condition_forward=document::EndCondition::ThroughAll;
    cut.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;
    cut.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
    workspace::commit_profile(live,kernel,id,cut,workspace::ProfileEditMode::Create,sketch);
    const auto& result=state->session.calculated_boundaries().back();
    for(const auto& [failed,message]:result.calculation_errors)throw std::runtime_error("Sheet Cut calculation: "+message);
    // Side A has radius 10 and y=10-10*cos(angle). Project y=[1,5]
    // onto that skin, then carry exactly that angular domain through r=[9,10].
    const auto removed=95*(std::acos(.5)-std::acos(.9));
    check(!result.sheet_cuts.empty(),"Sheet Cut did not retain material-space regions");
    near(result.volume,190*std::numbers::pi-removed);
    for(const auto& region:result.sheet_cuts) {
        check(region.cut_owner==cut.id&&region.source.owner_id==source.id&&region.surface_type=="cylinder", "Sheet Cut lost source ancestry");
        check(!region.loops.empty()&&!region.loops.front().empty(),"Sheet Cut lost material-space boundary curves");
    }
    run(host,"save");std::vector<kernel::BodyResult> loaded;
    const auto reopened=document::PartDocument::load(directory/"sheet-cut-cylinder.prtz",&loaded);
    check(reopened.find_container(cut.id)->extrusion.sheet_cut&&!loaded.back().sheet_cuts.empty(),"Sheet Cut did not persist native material-space data");
    check(document::serialize_body_result(loaded.back()).at("sheet_cuts")==document::serialize_body_result(result).at("sheet_cuts"),
        "Native roundtrip changed material-space trimming curves");
    kernel::OcctKernel fresh_kernel;
    const auto recalculated=fresh_kernel.evaluate_history(reopened.kernel_operations()).back();
    near(recalculated.volume,result.volume);
    check(recalculated.calculation_errors.empty()&&!recalculated.sheet_cuts.empty(),"Fresh calculation lost the sheet cut");
    run(host,"undo");near(state->session.calculated_boundaries().back().volume,190*std::numbers::pi);
    run(host,"redo");near(state->session.calculated_boundaries().back().volume,190*std::numbers::pi-removed);
    // A normal cut differs from a spatial prism, including on the inner skin.
    bool cut_wall=false;
    for(const auto& face:state->session.calculated_boundaries().back().mesh.triangle_references) {
        check(face.valid(),"Sheet Cut created an anonymous result face");
        if(face.owner_id==cut.id) {cut_wall=true;check(face.sheet_role==kernel::SheetFaceRole::ThicknessFace,"Cut wall lost its sheet role");}
    }
    check(cut_wall,"Sheet Cut has no attributed thickness walls");
    auto circle=sketcher::Sketch::create_default();static_cast<void>(circle.add_circle(30,3,1));
    auto second=document::PartDocument::create_extrusion_container(circle.id);
    second.combine_mode=document::CombineMode::Subtract;second.extrusion=cut.extrusion;second.extrusion.sketch_id=circle.id;
    workspace::commit_profile(live,kernel,id,second,workspace::ProfileEditMode::Create,circle);
    const auto circle_result=state->session.calculated_boundaries().back();
    check(circle_result.volume<recalculated.volume&&circle_result.calculation_errors.empty(),"Circular normal cut failed");
    check(std::ranges::any_of(circle_result.sheet_cuts,[&](const auto& r){return r.cut_owner==cut.id;})&&
        std::ranges::any_of(circle_result.sheet_cuts,[&](const auto& r){return r.cut_owner==second.id;}),"Second cut lost an earlier material region");
    for(const auto& face:circle_result.mesh.triangle_references)check(face.valid(),"Circular cut created an anonymous face");
    auto outside=sketcher::Sketch::create_default();static_cast<void>(outside.add_rectangle(100,100,110,110));
    auto missing=document::PartDocument::create_extrusion_container(outside.id);
    missing.combine_mode=document::CombineMode::Subtract;missing.extrusion=cut.extrusion;missing.extrusion.sketch_id=outside.id;
    const auto history_size=state->session.document().history.size();bool rejected=false;
    try{workspace::commit_profile(live,kernel,id,missing,workspace::ProfileEditMode::Create,outside);}catch(const std::exception&){rejected=true;}
    check(rejected&&state->session.document().history.size()==history_size,"Nonintersecting Sheet Cut committed a history row");
    near(state->session.calculated_boundaries().back().volume,circle_result.volume);
    for(const std::string kind:{"flat","bend","continuation","cone"}) {
        run(host,"new",{{"type","part"},{"name","sheet-cut-"+kind}});
        const auto document_id=live.active_document_id();state=live.open_part(document_id);
        if(kind=="flat")run(host,"flat.create",{{"width_mm",40.},{"height_mm",30.},{"thickness_mm",1.}});
        else if(kind=="bend"||kind=="continuation") {
            const auto owner=run(host,"bend.create",{{"width_mm",40.},{"radius_mm",5.},{"angle_degrees",90.}}).data.at("container").get<std::string>();
            if(kind=="continuation") {
                auto feature=*state->session.document().find_container(owner);
                const auto start=workspace::document_sketch(live,document_id,feature.bend.sketch_id);
                auto path=sketcher::Sketch::from_serialized(feature.bend.auxiliary_sketches[0]);
                const auto join=*path.find_point(path.arcs.front().end_point_id);
                static_cast<void>(path.add_segment(join.x,join.y,join.x,join.y+20.));
                document::accept_bend_sketch(feature,start,0,path,document::sheet_metal_defaults(state->session.document()));
                static_cast<void>(workspace::commit_bend(live,kernel,document_id,feature,start));
            }
        }
        else {
            source_sketch=sketcher::Sketch::create_default();source=document::PartDocument::create_revolution_container(source_sketch.id);
            document::initialize_sheet_revolution(source,source_sketch,document::sheet_metal_defaults(state->session.document()));
            const auto axis=std::ranges::find(source_sketch.segments,source.revolution.axis_segment_id,&sketcher::SketchSegment::id);
            source_sketch.find_point(axis->second_point_id)->y=20.;
            workspace::commit_profile(live,kernel,document_id,source,workspace::ProfileEditMode::Create,source_sketch);
        }
        const double before=state->session.calculated_boundaries().back().volume;
        sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(10,1,20,kind=="bend"?3:kind=="continuation"?8:5));
        auto next=document::PartDocument::create_extrusion_container(sketch.id);next.combine_mode=document::CombineMode::Subtract;
        next.extrusion.sheet_cut=true;next.extrusion.end_condition_forward=document::EndCondition::ThroughAll;
        next.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;next.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
        workspace::commit_profile(live,kernel,document_id,next,workspace::ProfileEditMode::Create,sketch);
        const auto& after=state->session.calculated_boundaries().back();
        check(after.calculation_errors.empty(),"Sheet Cut returned a failed feature");
        for(const auto& face:after.mesh.triangle_references) {
            check(face.valid(),"Normal cut created an anonymous face");
            if(face.owner_id==next.id)
                check(face.sheet_role==kernel::SheetFaceRole::ThicknessFace,
                    "Normal cut introduced a sheet skin instead of a thickness wall");
        }
        check(after.volume>0&&after.volume<before,"Sheet Cut did not remove material from its sheet source");
        if(kind=="flat")near(after.volume,before-40.);
        // Bend Side A has radius 6 and y=6-6*cos(angle); its inner radius is 5.
        if(kind=="bend")near(after.volume,before-55*(std::acos(.5)-std::acos(5./6)));
        if(kind=="continuation") {
            near(after.volume,before-55*(std::numbers::pi/2-std::acos(5./6))-20.);
            check(std::ranges::any_of(after.sheet_cuts,[](const auto& r){return r.surface_type=="plane";})&&
                std::ranges::any_of(after.sheet_cuts,[](const auto& r){return r.surface_type=="cylinder";}),"One cut did not span both profile regions");
            const auto source_owner=state->session.document().history.front().id;
            for(const auto& region:after.sheet_cuts)
                check(region.cut_owner==next.id&&region.source.owner_id==source_owner,
                    "Cut across a Bend and its tangent continuation lost its shared source owner");
        }
        if(kind=="cone")check(std::ranges::any_of(after.sheet_cuts,[](const auto& r){return r.surface_type=="cone";}),"Cone cut did not retain its cone coordinates");
        if(kind=="flat") {
            const auto before_ring=after.volume;
            auto ring=sketcher::Sketch::create_default();
            static_cast<void>(ring.add_rectangle(24,10,36,22));
            static_cast<void>(ring.add_rectangle(27,13,33,19));
            auto ring_cut=document::PartDocument::create_extrusion_container(ring.id);
            ring_cut.combine_mode=document::CombineMode::Subtract;ring_cut.extrusion=next.extrusion;
            ring_cut.extrusion.sketch_id=ring.id;
            workspace::commit_profile(live,kernel,document_id,ring_cut,workspace::ProfileEditMode::Create,ring);
            const auto& ring_result=state->session.calculated_boundaries().back();
            check(ring_result.calculation_errors.empty(),"Sheet Cut with an inner profile loop failed");
            near(ring_result.volume,before_ring-(12.*12-6.*6));
            check(std::ranges::any_of(ring_result.sheet_cuts,[&](const auto& region){
                return region.cut_owner==ring_cut.id&&region.loops.size()==2;
            }),"Sheet Cut lost its material island when persisting the inner loop");
            for(const auto& face:ring_result.mesh.triangle_references)
                check(face.valid(),"Sheet Cut island created an anonymous face");
        }
        run(host,"save");
        std::vector<kernel::BodyResult> restored;
        static_cast<void>(document::PartDocument::load(directory/("sheet-cut-"+kind+".prtz"),&restored));
        check(document::serialize_body_result(state->session.calculated_boundaries().back()).at("sheet_cuts")==
            document::serialize_body_result(restored.back()).at("sheet_cuts"),
            "Native roundtrip changed sheet region frames or trimming curves");
        for(const auto& region:restored.back().sheet_cuts) {
            near(std::hypot(region.x_axis.x,region.x_axis.y,region.x_axis.z),1.);
            near(std::hypot(region.y_axis.x,region.y_axis.y,region.y_axis.z),1.);
            near(region.x_axis.x*region.y_axis.x+region.x_axis.y*region.y_axis.y+region.x_axis.z*region.y_axis.z,0.);
        }
    }
}
void verify_sheet_cut_across_attachment(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    for(const bool conical:{false,true}) {
    run(host,"new",{{"type","part"},{"name",conical?"sheet-cut-across-cone-attachment":"sheet-cut-across-attachment"}});
    run(host,"flat.create",{{"width_mm",40.},{"height_mm",30.},{"thickness_mm",1.}});
    const auto id=live.active_document_id();auto* state=live.open_part(id);
    const auto flat_id=state->session.document().history.front().id;
    const auto geometry=state->session.calculated_boundaries().back().mesh.original_references;
    const auto edge=std::ranges::find_if(geometry.edges,[](const auto& e){return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&
        std::ranges::all_of(e.points,[](const auto& p){return std::abs(p.y)<1e-7&&std::abs(p.z-1.)<1e-7;});});
    check(edge!=geometry.edges.end(),"Sheet Cut attachment boundary missing");
    auto profile=sketcher::Sketch::create_default();
    auto feature=conical?document::PartDocument::create_revolution_container(profile.id):document::PartDocument::create_sketch_container();
    if(conical) {
        document::initialize_sheet_revolution(feature,profile,document::sheet_metal_defaults(state->session.document()));
        const auto axis=std::ranges::find(profile.segments,feature.revolution.axis_segment_id,&sketcher::SketchSegment::id);
        profile.find_point(axis->second_point_id)->y=20.;feature.revolution.sheet_attachment=true;
        feature.placement.references=document::bend_sheet_references(*edge);
        workspace::commit_profile(live,kernel,id,feature,workspace::ProfileEditMode::Create,profile);
    } else {
        feature.feature_kind=document::FeatureKind::Bend;profile.owner_container_id=feature.id;
        document::initialize_bend_start_profile(profile,40);
        feature.bend.sketch_id=profile.id;feature.bend.sheet_attachment=true;feature.bend.angle_degrees=90.;
        feature.bend.radius=5.;feature.bend.radius_follows_thickness=false;
        feature.placement.references=document::bend_sheet_references(*edge);
        static_cast<void>(workspace::commit_bend(live,kernel,id,feature,profile));
    }
    const auto before=state->session.calculated_boundaries().back().volume;
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(10,-3,20,3));
    auto cut=document::PartDocument::create_extrusion_container(sketch.id);cut.combine_mode=document::CombineMode::Subtract;
    cut.extrusion.sheet_cut=true;cut.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
    cut.extrusion.end_condition_forward=document::EndCondition::ThroughAll;
    cut.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;
    workspace::commit_profile(live,kernel,id,cut,workspace::ProfileEditMode::Create,sketch);
    const auto& result=state->session.calculated_boundaries().back();
    check(result.calculation_errors.empty()&&result.volume>0&&result.volume<before,"Sheet Cut across an attachment failed");
    std::set<std::string> owners;
    for(const auto& region:result.sheet_cuts) {
        check(region.cut_owner==cut.id,"Sheet Cut split into unrelated cut owners");
        owners.insert(region.source.owner_id);
    }
    check(owners==std::set<std::string>{flat_id,feature.id},"One Sheet Cut did not retain both Flat and attached Profile regions");
    for(const auto& face:result.mesh.triangle_references)check(face.valid(),"Attached Sheet Cut created an anonymous face");
    }
}
void verify_sheet_cut_cone_orientation(std::filesystem::path directory,bool clearance=false) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    const double angle=std::numbers::pi/2,cosine=40/std::sqrt(1700.);
    for(const double taper:{-.25,.25})for(const bool reverse_profile:{false,true})for(const bool reverse_axis:{false,true}) {
        const auto label=std::string(taper<0?"contracting":"expanding")+(reverse_profile?"-reversed-profile":"-profile")+(reverse_axis?"-reversed-axis":"-axis");
        std::cout<<"Sheet Cut cone orientation: "<<label<<(clearance?" clearance":" normal")<<std::endl;
        run(host,"new",{{"type","part"},{"name",std::string(clearance?"sheet-cut-clearance-cone-":"sheet-cut-cone-")+label}});
        const auto id=live.active_document_id();auto* state=live.open_part(id);
        auto profile=sketcher::Sketch::create_default();
        const auto radius=[&](double x){return 15+taper*(x-20);};
        static_cast<void>(profile.add_segment(reverse_profile?40:0,radius(reverse_profile?40:0),
            reverse_profile?0:40,radius(reverse_profile?0:40)));
        const auto axis=profile.add_segment(reverse_axis?40:0,0,reverse_axis?0:40,0,1e-6,true);
        profile.set_segment_centerline(axis,true);
        auto source=document::PartDocument::create_revolution_container(profile.id);
        source.revolution.sheet_metal=true;source.revolution.result_type=document::ProfileResultType::Thin;
        source.revolution.axis_segment_id=axis;source.revolution.angle_degrees=90;
        source.revolution.thin_thickness=1;source.revolution.thickness_override=true;
        source.revolution.direction=reverse_axis?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
        workspace::commit_profile(live,kernel,id,source,workspace::ProfileEditMode::Create,profile);
        // Pappus / direct volume integration over the generator and normal depth.
        // Reversing the generator changes material to the other side of the same
        // authored Side A; reversing the axis plus sweep direction changes none.
        const double thickness_sign=reverse_profile?-1:1;
        const double expected=angle*(600/cosine+thickness_sign*20);
        near(state->session.calculated_boundaries().back().volume,expected);
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(10,-30,20,30));
        auto cut=document::PartDocument::create_extrusion_container(sketch.id);
        cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;
        cut.extrusion.sheet_cut_clearance=clearance;
        cut.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
        cut.extrusion.end_condition_forward=document::EndCondition::ThroughAll;
        cut.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;
        workspace::commit_profile(live,kernel,id,cut,workspace::ProfileEditMode::Create,sketch);
        const auto& result=state->session.calculated_boundaries().back();
        check(result.calculation_errors.empty(),"Sheet Cut failed on a reversed cone generator or axis");
        // The projection selects x=[10,20] on Side A. The normal cut must carry
        // that whole interval across thickness, not leave a tapered membrane.
        if(clearance) {
            // A full normal fibre must be removed whenever any of its depths
            // intersects the prism. Its axial displacement enlarges the domain.
            const double normal_x=-thickness_sign*taper*cosine;
            const double lo=10-std::max(0.,normal_x),hi=20-std::min(0.,normal_x);
            const double removed=angle*(hi-lo)*(radius((lo+hi)*.5)/cosine+thickness_sign*.5);
            near_clearance_volume(result.volume,expected-removed);
            verify_clearance_passage(kernel,result,10,-30,10,60);
        } else {
            const double removed=angle*(10*radius(15)/cosine+thickness_sign*5);
            near(result.volume,expected-removed);
        }
        check(!result.sheet_cuts.empty(),"Conical cut did not retain its material domain");
        for(const auto& region:result.sheet_cuts) {
            check(region.surface_type=="cone"&&region.source.owner_id==source.id,
                "Conical cut lost its source identity or surface coordinates");
            near(region.thickness,1.);
        }
        for(const auto& face:result.mesh.triangle_references)check(face.valid(),"Conical cut created an anonymous face");
        if(clearance) {
            // A circular pin on a cone exercises an interior normal envelope;
            // a rectangular axial band alone cannot expose sampling scallops.
            const double before_round=result.volume;
            auto round=sketcher::Sketch::create_default();static_cast<void>(round.add_circle(30,8,.15));
            auto circular=document::PartDocument::create_extrusion_container(round.id);
            circular.combine_mode=document::CombineMode::Subtract;circular.extrusion=cut.extrusion;
            circular.extrusion.sketch_id=round.id;
            workspace::commit_profile(live,kernel,id,circular,workspace::ProfileEditMode::Create,round);
            const auto& perforated=state->session.calculated_boundaries().back();
            check(perforated.volume<before_round,"Conical circular cut removed no material");
            // Independent disk/normal-depth integration, including reversed
            // material side. Axis reversal cannot change the physical result.
            const double round_removed=taper<0?
                (reverse_profile?.38705053700104786:.34596017902529286):
                (reverse_profile?.26430770765918454:.2522314998647712);
            near_clearance_volume(before_round-perforated.volume,round_removed);
            kernel::CylinderRequest pin;pin.radius=.15;pin.height=200;pin.translation={30,8,-100};
            kernel::HistoryOperation probe;probe.owner_id="cone-clearance-pin";probe.primitive=pin;
            near(kernel.subtract_bodies(perforated,kernel.evaluate_history({probe}).back(),{},{},1e-7).volume,perforated.volume);
            if(taper<0&&!reverse_profile&&!reverse_axis) {
                // A conic profile uses the same normal-depth envelope contract
                // and must admit an independently constructed elliptical pin.
                const double before_ellipse=perforated.volume;
                auto ellipse=sketcher::Sketch::create_default();
                static_cast<void>(ellipse.add_ellipse(34,8,35,8,34,8.6));
                auto elliptic=document::PartDocument::create_extrusion_container(ellipse.id);
                elliptic.combine_mode=document::CombineMode::Subtract;elliptic.extrusion=cut.extrusion;
                elliptic.extrusion.sketch_id=ellipse.id;
                workspace::commit_profile(live,kernel,id,elliptic,workspace::ProfileEditMode::Create,ellipse);
                const auto& cleared=state->session.calculated_boundaries().back();
                check(cleared.calculation_errors.empty()&&cleared.volume<before_ellipse&&
                    before_ellipse-cleared.volume<10,"Elliptic clearance cut has an invalid material volume");
                near_clearance_volume(before_ellipse-cleared.volume,4.482372048838405);
                kernel::ExtrusionRequest ellipse_pin;
                ellipse_pin.outer_profile=kernel::ExtrusionRequest::EllipseProfile{{34,8,-100},{1,0,0},1,.6};
                ellipse_pin.direction={0,0,200};ellipse_pin.profile_region_id="ellipse-inspection-region";
                ellipse_pin.outer_boundary_id="ellipse-inspection-boundary";
                ellipse_pin.outer_edge_source_ids={"ellipse-inspection-curve"};
                kernel::HistoryOperation ellipse_probe;ellipse_probe.owner_id="ellipse-clearance-pin";
                ellipse_probe.primitive=ellipse_pin;
                near(kernel.subtract_bodies(cleared,kernel.evaluate_history({ellipse_probe}).back(),{},{},1e-7).volume,cleared.volume);
                for(const auto& face:cleared.mesh.triangle_references)
                    check(face.valid(),"Elliptic clearance cut has an anonymous face");
            }
        }
    }
}
void verify_sheet_cut_projection_extent(std::filesystem::path directory,bool clearance=false) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    const std::string name=clearance?"sheet-cut-clearance-projection-extents":"sheet-cut-projection-extents";
    run(host,"new",{{"type","part"},{"name",name}});
    const auto id=live.active_document_id();auto* state=live.open_part(id);
    std::vector<std::string> sheets;
    for(const double z:{-10.,10.,30.}) {
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(0,0,20,20));
        auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Flat;
        feature.flat.sketch_id=sketch.id;feature.flat.thickness=2;feature.flat.thickness_override=true;
        feature.placement.z=z;sketch.owner_container_id=feature.id;
        static_cast<void>(workspace::commit_flat(live,kernel,id,feature,sketch));sheets.push_back(feature.id);
    }
    near(state->session.calculated_boundaries().back().volume,2400.);
    const auto target=run(host,"construction.create",{{"kind","plane"},{"name","Cut limit"},{"base_plane","xy"},{"offset_mm",20.}});
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(2,2,6,6));
    auto cut=document::PartDocument::create_extrusion_container(sketch.id);
    cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;
    cut.extrusion.sheet_cut_clearance=clearance;
    cut.extrusion.length_forward=10.5;cut.extrusion.length_reverse=10.5;
    cut.extrusion.end_condition_forward=document::EndCondition::Length;
    cut.extrusion.end_condition_reverse=document::EndCondition::Length;
    workspace::commit_profile(live,kernel,id,cut,workspace::ProfileEditMode::Create,sketch);
    const auto verify=[&](std::set<std::string> expected) {
        const auto& result=state->session.calculated_boundaries().back();
        check(result.calculation_errors.empty(),"Bounded or directional Sheet Cut failed");
        // Projection reaches only 0.5 mm into the first 2 mm sheet, but Sheet
        // Cut removes its entire normal thickness after selecting its Side A.
        near(result.volume,2400-32*expected.size());
        std::set<std::string> owners;for(const auto& region:result.sheet_cuts) {
            check(region.cut_owner==cut.id,"One projection created unrelated Sheet Cut owners");
            owners.insert(region.source.owner_id);
        }
        check(owners==expected,"Sheet Cut projection ignored a side or selected an unreachable sheet");
    };
    verify({sheets[1]});
    run(host,"extrusion.set",{{"container",cut.id},{"direction","reverse"}});verify({sheets[0]});
    run(host,"extrusion.set",{{"container",cut.id},{"direction","forward"},{"extent","two_sides"}});verify({sheets[0],sheets[1]});
    run(host,"extrusion.set",{{"container",cut.id},{"extent","symmetric"}});verify({sheets[0],sheets[1]});
    run(host,"extrusion.set",{{"container",cut.id},{"extent","one_side"},{"end_forward","through_all"}});verify({sheets[1],sheets[2]});
    run(host,"extrusion.set",{{"container",cut.id},{"direction","reverse"}});verify({sheets[0]});
    run(host,"extrusion.set",{{"container",cut.id},{"direction","forward"},{"extent","two_sides"},{"end_reverse","through_all"}});verify({sheets[0],sheets[1],sheets[2]});
    run(host,"extrusion.set",{{"container",cut.id},{"extent","one_side"},{"end_forward","up_to"},
        {"targets_forward",Json::array({{{"owner",target.data.at("entity")},{"key","plane"}}})}});verify({sheets[1]});
    run(host,"save");std::vector<kernel::BodyResult> boundaries;
    const auto loaded=document::PartDocument::load(directory/(name+".prtz"),&boundaries);
    const auto& saved=loaded.find_container(cut.id)->extrusion;
    check(saved.sheet_cut&&saved.end_condition_forward==document::EndCondition::UpTo&&saved.end_targets_forward.size()==1,
        "Sheet Cut lost its directional projection parameters on reopen");
    check(saved.sheet_cut_clearance==clearance,"Sheet Cut lost its calculation mode on reopen");
    near(boundaries.back().volume,2368.);
    if(clearance) {
        // Approach from the opposite skin (z=12), ending at z=11.5 inside
        // the sheet, before reaching Side A at z=10. The whole wall is cut.
        run(host,"extrusion.set",{{"container",cut.id},{"end_forward","length"},{"targets_forward",Json::array()},
            {"direction","reverse"},{"profile_offset_mm",13.},{"length_forward_mm",1.5}});
        verify({sheets[1]});
    }
}
void verify_sheet_cut_clearance(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","sheet-cut-clearance-cylinder"}});
    auto id=live.active_document_id();auto* state=live.open_part(id);
    auto profile=sketcher::Sketch::create_default();auto source=document::PartDocument::create_revolution_container(profile.id);
    document::initialize_sheet_revolution(source,profile,document::sheet_metal_defaults(state->session.document()));
    workspace::commit_profile(live,kernel,id,source,workspace::ProfileEditMode::Create,profile);
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(10,2,20,5));
    auto cut=document::PartDocument::create_extrusion_container(sketch.id);
    cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;cut.extrusion.sheet_cut_clearance=true;
    cut.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
    cut.extrusion.end_condition_forward=cut.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;
    workspace::commit_profile(live,kernel,id,cut,workspace::ProfileEditMode::Create,sketch);
    auto result=state->session.calculated_boundaries().back();
    // Fibre r=[9,10] reaches y=[2,5] iff theta lies between acos(8/9)
    // and acos(1/2), so the inside opening admits the full requested prism.
    near_clearance_volume(result.volume,190*std::numbers::pi-95*(std::acos(.5)-std::acos(8./9)));
    verify_clearance_passage(kernel,result,10,2,10,3);
    for(const auto& face:result.mesh.triangle_references)check(face.valid(),"Cylinder clearance cut has an anonymous face");
    // The same feature identity must regenerate when only its calculation mode
    // changes; the cached normal and clearance bodies are different results.
    auto mode_value=*state->session.document().find_container(cut.id);
    const auto mode_profile=workspace::document_sketch(live,id,mode_value.extrusion.sketch_id);
    mode_value.extrusion.sheet_cut_clearance=false;
    workspace::commit_profile(live,kernel,id,mode_value,workspace::ProfileEditMode::Replace,mode_profile);
    near(state->session.calculated_boundaries().back().volume,190*std::numbers::pi-95*(std::acos(.5)-std::acos(.8)));
    mode_value.extrusion.sheet_cut_clearance=true;
    workspace::commit_profile(live,kernel,id,mode_value,workspace::ProfileEditMode::Replace,mode_profile);
    result=state->session.calculated_boundaries().back();
    near_clearance_volume(result.volume,190*std::numbers::pi-95*(std::acos(.5)-std::acos(8./9)));
    verify_clearance_passage(kernel,result,10,2,10,3);
    run(host,"save");std::vector<kernel::BodyResult> saved;
    const auto restored=document::PartDocument::load(directory/"sheet-cut-clearance-cylinder.prtz",&saved);
    check(restored.find_container(cut.id)->extrusion.sheet_cut_clearance&&!saved.back().sheet_cuts.empty(),
        "Clearance cut lost its mode or material domains on reopen");
    verify_clearance_passage(kernel,saved.back(),10,2,10,3);
    // A hole narrower than the projected normal depth also needs the interior
    // envelope between its two skin impressions, including curved boundaries.
    auto circle=sketcher::Sketch::create_default();static_cast<void>(circle.add_circle(30,3,.15));
    auto circular_cut=document::PartDocument::create_extrusion_container(circle.id);
    circular_cut.combine_mode=document::CombineMode::Subtract;circular_cut.extrusion=cut.extrusion;
    circular_cut.extrusion.sketch_id=circle.id;
    const double before_circle=result.volume;
    workspace::commit_profile(live,kernel,id,circular_cut,workspace::ProfileEditMode::Create,circle);
    result=state->session.calculated_boundaries().back();
    // Independent integration: 9.5 * integral[-.15,.15] of
    // acos((7-sqrt(.15^2-x^2))/10)-acos((7+sqrt(.15^2-x^2))/9).
    near_clearance_volume(before_circle-result.volume,.4366419320123114);
    kernel::CylinderRequest pin;pin.radius=.15;pin.height=200;pin.translation={30,3,-100};
    kernel::HistoryOperation pin_operation;pin_operation.owner_id="clearance-inspection-pin";pin_operation.primitive=pin;
    const auto pin_body=kernel.evaluate_history({pin_operation}).back();
    near(kernel.subtract_bodies(result,pin_body,{},{},1e-7).volume,result.volume);
    check(result.volume<saved.back().volume&&result.calculation_errors.empty(),"Circular clearance cut removed no material");
    for(const auto& face:result.mesh.triangle_references)check(face.valid(),"Circular clearance cut has an anonymous face");
    // A cylinder ellipse deliberately exercises the general UV envelope.
    const double before_ellipse=result.volume;
    auto ellipse=sketcher::Sketch::create_default();static_cast<void>(ellipse.add_ellipse(34,3,35,3,34,3.6));
    auto elliptic=document::PartDocument::create_extrusion_container(ellipse.id);
    elliptic.combine_mode=document::CombineMode::Subtract;elliptic.extrusion=cut.extrusion;
    elliptic.extrusion.sketch_id=ellipse.id;
    workspace::commit_profile(live,kernel,id,elliptic,workspace::ProfileEditMode::Create,ellipse);
    result=state->session.calculated_boundaries().back();
    // Independent integration of the ideal extreme-radius envelope. The
    // generic envelope can expand by at most the Sheet Cut deviation. Its four
    // monotone boundary branches have perimeter <= 4*a+2*R*angular_span;
    // dilation adds at most P*delta+pi*delta^2 of metric area, converted to
    // volume with (R-t/2)*t/R. Keep the pin inspection below at strict 1e-5.
    constexpr double ideal_ellipse_volume=5.098597738737086;
    const double accuracy=std::stod(state->session.document().document_precision.at("sheet_cut_tolerance"));
    const double perimeter_bound=4+20*(std::acos(6.4/10)-std::acos(7.6/9));
    const double volume_band=.95*(perimeter_bound*accuracy+std::numbers::pi*accuracy*accuracy);
    const double ellipse_removed=before_ellipse-result.volume;
    check(ellipse_removed>=ideal_ellipse_volume-1.e-5&&ellipse_removed<=ideal_ellipse_volume+volume_band,
        "Generic elliptic clearance exceeded its independently bounded Sheet Cut deviation");
    std::set<std::pair<std::string,std::string>> ellipse_boundary_identities;
    for(const auto& region:result.sheet_cuts)if(region.cut_owner==elliptic.id)
        for(const auto& loop:region.loops)for(const auto& curve:loop)
            check(ellipse_boundary_identities.emplace(curve.parent_owner,curve.parent_key).second,
                "Distinct generic Sheet Cut boundaries reused one persisted identity");
    check(!ellipse_boundary_identities.empty(),"Generic Sheet Cut has no persisted boundary identities");
    kernel::ExtrusionRequest ellipse_pin;
    ellipse_pin.outer_profile=kernel::ExtrusionRequest::EllipseProfile{{34,3,-100},{1,0,0},1,.6};
    ellipse_pin.direction={0,0,200};ellipse_pin.profile_region_id="cylinder-ellipse-inspection-region";
    ellipse_pin.outer_boundary_id="cylinder-ellipse-inspection-boundary";
    ellipse_pin.outer_edge_source_ids={"cylinder-ellipse-inspection-curve"};
    kernel::HistoryOperation ellipse_probe;ellipse_probe.owner_id="cylinder-ellipse-clearance-pin";
    ellipse_probe.primitive=ellipse_pin;
    near(kernel.subtract_bodies(result,kernel.evaluate_history({ellipse_probe}).back(),{},{},1e-7).volume,result.volume);
    const auto ellipse_volume=result.volume;
    run(host,"regenerate");
    result=state->session.calculated_boundaries().back();near(result.volume,ellipse_volume);
    std::set<std::pair<std::string,std::string>> regenerated_ellipse_identities;
    for(const auto& region:result.sheet_cuts)if(region.cut_owner==elliptic.id)
        for(const auto& loop:region.loops)for(const auto& curve:loop)
            check(regenerated_ellipse_identities.emplace(curve.parent_owner,curve.parent_key).second,
                "Regeneration aliased generic Sheet Cut boundary identities");
    check(regenerated_ellipse_identities==ellipse_boundary_identities,
        "Regeneration changed generic Sheet Cut boundary ancestry");
    for(const bool island:{false,true}) {
        run(host,"new",{{"type","part"},{"name",island?"sheet-cut-clearance-island":"sheet-cut-clearance-middle-depth"}});
        id=live.active_document_id();state=live.open_part(id);
        profile=sketcher::Sketch::create_default();static_cast<void>(profile.add_rectangle(-20,-20,20,20));
        source=document::PartDocument::create_sketch_container();source.feature_kind=document::FeatureKind::Flat;
        source.flat.sketch_id=profile.id;source.flat.thickness=2;source.flat.thickness_override=true;
        source.placement.absolute_rotation_y=source.placement.rotation_y=45.;profile.owner_container_id=source.id;
        static_cast<void>(workspace::commit_flat(live,kernel,id,source,profile));
        near(state->session.calculated_boundaries().back().volume,3200.);
        sketch=sketcher::Sketch::create_default();
        if(island) {
            static_cast<void>(sketch.add_rectangle(-5,-5,5,5));
            static_cast<void>(sketch.add_rectangle(-2,-2,2,2));
        } else static_cast<void>(sketch.add_rectangle(-.1,-2,.1,2));
        cut=document::PartDocument::create_extrusion_container(sketch.id);
        cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;cut.extrusion.sheet_cut_clearance=true;
        cut.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
        cut.extrusion.end_condition_forward=cut.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;
        workspace::commit_profile(live,kernel,id,cut,workspace::ProfileEditMode::Create,sketch);
        result=state->session.calculated_boundaries().back();
        check(result.calculation_errors.empty(),"Oblique sheet clearance cut failed");
        const double cosine=std::sqrt(.5),shift=2.; // t*tan(45 degrees) in material coordinates.
        const double area=island?10*(10/cosine+shift)-4*(4/cosine-shift):4*(.2/cosine+shift);
        near_clearance_volume(result.volume,3200-2*area);
        if(island) {
            kernel::BoxRequest outer(10,10,200),inner(4,4,202);
            outer.translation={-5,-5,-100};inner.translation={-2,-2,-101};
            const auto prism=kernel.subtract_bodies(kernel.make_box(outer),kernel.make_box(inner),{},{},1e-7);
            near(kernel.subtract_bodies(result,prism,{},{},1e-7).volume,result.volume);
            check(std::ranges::any_of(result.sheet_cuts,[](const auto& region){return region.loops.size()==2;}),
                "Clearance projection filled a surviving material island");
        } else verify_clearance_passage(kernel,result,-.1,-2,.2,4);
        for(const auto& face:result.mesh.triangle_references)check(face.valid(),"Oblique clearance cut has an anonymous face");
    }
}
void verify_sheet_cut_bounded_bend_thickness(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    for(const bool clearance:{false,true}) {
        run(host,"new",{{"type","part"},{"name",clearance?"bend-bounded-clearance-thickness":"bend-bounded-normal-thickness"}});
        run(host,"bend.create",{{"width_mm",40.},{"radius_mm",5.},{"angle_degrees",90.}});
        const auto id=live.active_document_id();auto* state=live.open_part(id);
        const double before=state->session.calculated_boundaries().back().volume;
        near(before,110*std::numbers::pi);
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(10,1,20,3));
        auto cut=document::PartDocument::create_extrusion_container(sketch.id);
        cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;cut.extrusion.sheet_cut_clearance=clearance;
        cut.extrusion.length_forward=cut.extrusion.height=3.5;
        cut.extrusion.extent_mode=document::ProfileExtentMode::OneSide;
        cut.extrusion.end_condition_forward=document::EndCondition::Length;
        workspace::commit_profile(live,kernel,id,cut,workspace::ProfileEditMode::Create,sketch);
        const auto& result=state->session.calculated_boundaries().back();
        check(result.calculation_errors.empty(),"Bounded Bend cut failed");
        // Side A obeys y=6-6*cos(theta), z=6*sin(theta). The old method
        // selects the reached skin patch. Clearance uses length to select a
        // connected wall passage, then admits the full profile through that
        // passage. Both remove the selected patch through normal thickness.
        const double begin=clearance?0.:std::acos(5./6);
        const double end=clearance?std::acos(.5):std::asin(3.5/6.);
        const double removed=55*(end-begin);
        if(clearance)near_clearance_volume(result.volume,before-removed);
        else near(result.volume,before-removed);
        // Build the selected through-thickness envelope independently from a
        // revolved closed section, rather than reusing the Sheet Cut algorithm.
        kernel::RevolutionRequest envelope;
        envelope.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{10,0,0},{20,0,0},{20,1,0},{10,1,0}}};
        envelope.profile_region_id="selected-bend-sector";envelope.outer_boundary_id="selected-bend-sector-boundary";
        envelope.outer_edge_source_ids={"sector-outer","sector-end","sector-inner","sector-start"};
        envelope.outer_vertex_source_ids={"sector-a","sector-b","sector-c","sector-d"};
        envelope.axis_point={0,6,0};envelope.axis_direction={-1,0,0};
        envelope.start_angle_degrees=begin*180/std::numbers::pi;
        envelope.angle_degrees=(end-begin)*180/std::numbers::pi;
        kernel::HistoryOperation operation;operation.owner_id="selected-bend-sector-inspection";operation.primitive=envelope;
        const auto sector=kernel.evaluate_history({operation}).back();near(sector.volume,removed);
        near(kernel.subtract_bodies(result,sector,{},{},1e-7).volume,result.volume);
        if(clearance) {
            // Extend only through the reached Bend wall. This checks that
            // finishing inside it does not retain a pocket or a depth step.
            kernel::BoxRequest prism(10,2,7);prism.translation={10,1,0};
            near(kernel.subtract_bodies(result,kernel.make_box(prism),{},{},1e-7).volume,result.volume);
        }
        for(const auto& face:result.mesh.triangle_references) {
            check(face.valid(),"Bounded Bend cut created an anonymous face");
            if(face.owner_id==cut.id)check(face.sheet_role==kernel::SheetFaceRole::ThicknessFace,
                "Bounded Bend cut left a skin instead of a through-thickness wall");
        }
    }
}
void verify_sheet_revolution(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","sheet-revolution"}});
    const auto id=live.active_document_id();auto* state=live.open_part(id);
    auto sketch=sketcher::Sketch::create_default();auto feature=document::PartDocument::create_revolution_container(sketch.id);
    document::initialize_sheet_revolution(feature,sketch,document::sheet_metal_defaults(state->session.document()));
    workspace::commit_profile(live,kernel,id,feature,workspace::ProfileEditMode::Create,sketch);
    const auto volume=[&]{return state->session.calculated_boundaries().back().volume;};
    const auto owner=feature.id;const double pi=std::numbers::pi;
    near(volume(),190*pi);
    check(state->session.document().sketches.size()==1,"Sheet Revolution created auxiliary Sketches");
    bool first=false,second=false,cap=false;
    for(const auto& face:state->session.calculated_boundaries().back().mesh.original_references.triangle_references) {
        first|=face.sheet_role==kernel::SheetFaceRole::SideA;second|=face.sheet_role==kernel::SheetFaceRole::SideB;cap|=face.sheet_role==kernel::SheetFaceRole::ThicknessFace;
        check(face.sheet_role!=kernel::SheetFaceRole::Unknown,"Sheet Revolution lost a sheet face role");
    }
    check(first&&second&&cap,"Sheet Revolution has no principal sides or caps");
    for(double angle:{180.,360.,90.}) {
        run(host,"revolution.set",{{"container",owner},{"angle_degrees",angle}});
        near(volume(),190*pi*angle/90.);
    }
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",2.}}}});near(volume(),360*pi);
    run(host,"revolution.set",{{"container",owner},{"thin_mode","other_side"}});near(volume(),440*pi);
    run(host,"revolution.set",{{"container",owner},{"thin_mode","symmetric"}});near(volume(),400*pi);
    run(host,"revolution.set",{{"container",owner},{"extent","two_sides"},{"angle_degrees",30.},{"angle_reverse_degrees",60.}});near(volume(),400*pi);
    run(host,"revolution.set",{{"container",owner},{"extent","symmetric"},{"angle_degrees",45.},{"direction","reverse"}});near(volume(),400*pi);
    run(host,"revolution.set",{{"container",owner},{"extent","one_side"},{"angle_degrees",90.},{"thin_mode","one_side"},{"direction","forward"}});
    feature=*state->session.document().find_container(owner);sketch=workspace::document_sketch(live,id,feature.revolution.sketch_id);
    const auto axis=*std::ranges::find(sketch.segments,feature.revolution.axis_segment_id,&sketcher::SketchSegment::id);
    sketch.find_point(axis.second_point_id)->y=20;
    workspace::commit_profile(live,kernel,id,feature,workspace::ProfileEditMode::Replace,sketch);
    near(volume(),pi/2*80*560/std::sqrt(1700.));
    run(host,"save");const auto saved=document::PartDocument::load(directory/"sheet-revolution.prtz");
    check(saved.history.front().revolution.sheet_metal&&saved.sketches.size()==1,"Sheet Revolution did not persist its single Sketch and sheet mode");
    const auto before=state->session.document().serialized();
    check(!host.execute({{"command","revolution.set"},{"arguments",{{"container",owner},{"combine","subtract"}}}}).ok,"Sheet Revolution accepted subtraction");
    check(state->session.document().serialized()==before,"Rejected Sheet Revolution changed the document");
    run(host,"new",{{"type","part"},{"name","sheet-revolution-attached"}});
    run(host,"flat.create",{{"width_mm",40.},{"height_mm",30.},{"thickness_mm",2.}});
    const auto attached_id=live.active_document_id();state=live.open_part(attached_id);
    const auto geometry=state->session.calculated_boundaries().back().mesh.original_references;
    const auto edge=std::ranges::find_if(geometry.edges,[](const auto& e){return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&
        std::ranges::all_of(e.points,[](const auto& p){return std::abs(p.y)<1e-7&&std::abs(p.z-2)<1e-7;});});
    check(edge!=geometry.edges.end(),"Sheet Revolution attachment edge missing");
    sketch=sketcher::Sketch::create_default();feature=document::PartDocument::create_revolution_container(sketch.id);
    document::initialize_sheet_revolution(feature,sketch,document::sheet_metal_defaults(state->session.document()));
    feature.revolution.sheet_attachment=true;feature.placement.references=document::bend_sheet_references(*edge);
    workspace::commit_profile(live,kernel,attached_id,feature,workspace::ProfileEditMode::Create,sketch);
    near(volume(),2400+360*pi);
    const auto attached=workspace::document_sketch(live,attached_id,sketch.id);
    check(attached.external_references.size()==2,"Sheet Revolution lost attached endpoint references");
    near(state->session.document().find_container(feature.id)->revolution.thin_thickness,2.);
    for(const auto& ref:attached.external_references)check(ref.source_document_id==attached_id,"Sheet Revolution references a temporary carrier document");
    run(host,"save");
    run(host,"undo");near(volume(),2400);
    run(host,"redo");near(volume(),2400+360*pi);
}
#include "sheet_cut_01_regression.inc"
#include "sheet_cut_actual_verification.inc"

int main(int argc,char** argv) {
    if(argc==3&&std::string_view(argv[1])=="--verify-sheet-cut") {
        try{verify_actual_sheet_cut_copy(std::filesystem::path(argv[2]));return 0;}
        catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    const auto directory=std::filesystem::temp_directory_path()/("zima-bend-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    if(argc==2&&std::string_view(argv[1])=="--verify-continuation-side") {
        try{verify_continuation_side_attachment(directory);std::filesystem::remove_all(directory);return 0;}
        catch(const std::exception& e){std::cerr<<e.what()<<"; fixture: "<<directory<<'\n';return 1;}
    }
    if(argc==2&&std::string_view(argv[1])=="--verify-generic-sheet-cut") {
        try{verify_sheet_cut_clearance(directory);std::filesystem::remove_all(directory);return 0;}
        catch(const std::exception& e){std::cerr<<e.what()<<"; fixture: "<<directory<<'\n';return 1;}
    }
    try{verify_sheet_cut_clearance(directory);verify_sheet_cut_tilted_cone_attachment(directory);verify_sheet_cut_cone_orientation(directory);verify_sheet_cut_projection_extent(directory);verify_sheet_cut_bounded_bend_thickness(directory);verify_sheet_cut_cone_orientation(directory,true);verify_sheet_cut_projection_extent(directory,true);verify_sheet_cut(directory);verify_sheet_cut_across_attachment(directory);verify_sheet_revolution(directory);verify_continuation(directory);verify_continuation_side_attachment(directory);verify_cross_branch_box(directory);verify_sheet_attachment(directory);verify_prepared_start();verify_attachment(directory);verify(directory);std::filesystem::remove_all(directory);std::cout<<"Bend geometry, identities, defaults, history and persistence passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"; fixture: "<<directory<<'\n';return 1;}
}

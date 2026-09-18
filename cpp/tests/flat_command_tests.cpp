#include <zima/command_host/host.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/bend.hpp>
#include <zima/workspace/flat_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
using namespace zima;
using commands::Json;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
commands::Result run(command_host::Host& host,const char* command,Json args=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",args}});
    if(!result.ok)throw std::runtime_error(std::string(command)+" "+args.dump()+": "+result.message);return result;
}
std::set<std::string> faces(const kernel::BodyResult& body,const std::string& owner) {
    std::set<std::string> result;for(const auto& f:body.mesh.original_references.triangle_references)
        if(f.owner_id==owner)result.insert(f.semantic_key);return result;
}
std::pair<double,double> span(const std::vector<kernel::Vec3>& vertices,const sketcher::Sketch& sketch) {
    double lo=INFINITY,hi=-INFINITY;for(const auto p:vertices) {
        const auto n=sketch.resolved_normal,o=sketch.resolved_origin;
        const double distance=(p.x-o.x)*n.x+(p.y-o.y)*n.y+(p.z-o.z)*n.z;
        lo=std::min(lo,distance);hi=std::max(hi,distance);
    }return {lo,hi};
}
void verify_bend_attachment(const std::filesystem::path& directory) {
    kernel::OcctKernel kernel;
    const auto close=[](kernel::Vec3 a,kernel::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<1e-6;};
    auto source=document::PartDocument::create_default();
    auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
    auto section=sketcher::Sketch::create_default();section.owner_container_id=bend.id;
    section.add_segment(0,0,40,0);bend.bend.sketch_id=section.id;bend.bend.thickness_override=true;
    source.history={bend};source.sketches={section};
    auto target=document::PartDocument::create_default();
    auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
    auto outline=sketcher::Sketch::create_default();outline.owner_container_id=flat.id;flat.flat.sketch_id=outline.id;
    flat.flat.sheet_attachment=true;flat.flat.direction=document::ExtrusionDirection::Reverse;
    target.history={flat};target.sketches={outline};
    std::string edge_key;std::vector<sketcher::SketchExternalReference> previous;
    for(double angle:{35.,90.,180.})for(double thickness:{1.,2.})for(bool flipped:{false,true}) {
        std::cout<<"Flat attachment angle="<<angle<<" thickness="<<thickness<<" flipped="<<flipped<<std::endl;
        auto& feature=source.history.front();feature.bend.angle_degrees=angle;
        feature.bend.thickness=thickness;feature.placement.absolute_rotation_x=flipped?180:0;
        feature.placement.absolute_rotation_z=23;feature.placement.x=17;feature.placement.y=-11;
        source.resolve_constructions();
        const auto body=kernel.evaluate_history(source.kernel_operations()).back();
        auto geometry=body.mesh.original_references;
        const auto end=sketcher::Sketch::from_serialized(source.history.front().bend.auxiliary_sketches[1]);
        const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e) {
            if(!edge_key.empty())return e.reference.semantic_key==edge_key;
            try {const auto refs=document::flat_sheet_references(e);
                return refs[1].semantic_key.starts_with("sweep:cap:end:from:");}
            catch(const std::exception&){return false;}
        });
        check(edge!=geometry.edges.end(),"Bend lost its stable outer End boundary");edge_key=edge->reference.semantic_key;
        if(target.history.front().placement.references.empty())target.history.front().placement.references=document::flat_sheet_references(*edge);
        const auto original_points=edge->points;
        for(bool reversed:{false,true}) {
            if(reversed){
                std::ranges::reverse(edge->points);std::ranges::reverse(edge->edge_treatment_endpoint_references);
                if(edge->exact_spline) {
                    std::ranges::reverse(edge->exact_spline->poles);
                    kernel::reverse_bspline_parameters(edge->exact_spline->knots,edge->exact_spline->weights);
                }
            }
            target.resolve_constructions(geometry);
            const auto& profile=target.sketches.front();
            check(target.history.front().placement.reference_valid,"Attached Flat placement is invalid");
            near(document::flat_thickness(target.history.front(),{}),thickness);
            check(close(profile.resolved_y_axis,end.resolved_normal),"Flat does not leave the Bend tangentially");
            const auto n=profile.resolved_normal;
            if(!close({-n.x,-n.y,-n.z},end.resolved_y_axis))std::cerr<<"reversed="<<reversed<<" inward "<<-n.x<<","<<-n.y<<","<<-n.z<<" expected "<<end.resolved_y_axis.x<<","<<end.resolved_y_axis.y<<","<<end.resolved_y_axis.z<<'\n';
            check(close({-n.x,-n.y,-n.z},end.resolved_y_axis),"Flat thickness points outside the Bend radius");
            check(profile.external_references.size()==2,"Flat did not publish both external endpoints");
            for(const auto& reference:profile.external_references) {
                check(!reference.broken&&reference.cached_points.size()==1,"Flat endpoint is broken");
                const auto p=reference.cached_points.front();const auto world=profile.world_point(p[0],p[1]);
                check(close(world,original_points.front())||close(world,original_points.back()),"External endpoint left its source edge");
            }
            if(!previous.empty())for(std::size_t i=0;i<2;++i) {
                check(previous[i].id==profile.external_references[i].id&&previous[i].source_semantic_key==profile.external_references[i].source_semantic_key,
                    "Bend change replaced Flat endpoint identity");
            }
            previous=profile.external_references;
            auto closed=profile;const auto a=closed.local_point(original_points.front()),b=closed.local_point(original_points.back());
            closed.points.clear();closed.segments.clear();closed.constraints.clear();closed.dimensions.clear();
            closed.add_rectangle(std::min(a[0],b[0]),0,std::max(a[0],b[0]),12);
            auto calculated=target;calculated.sketches.front()=closed;
            near(kernel.evaluate_history(calculated.kernel_operations()).back().volume,40*12*thickness);
            target.sketches.front()=closed;
        }
        for(const auto& e:geometry.edges) {
            const bool inner=std::ranges::any_of(e.edge_treatment_side_references,[](const auto& f){return f.sheet_role==kernel::SheetFaceRole::SideB;});
            if(!inner&&kernel::sheet_edge_role(e)!=kernel::SheetEdgeRole::Thickness)continue;
            bool rejected=false;try{document::flat_sheet_references(e);}catch(const std::exception&){rejected=true;}
            check(rejected,"Flat accepted inner or thickness edge");
        }
        target.save(directory/"attached-flat.prtz");target=document::PartDocument::load(directory/"attached-flat.prtz");
        check(target.history.front().flat.sheet_attachment,"Native reopen lost Flat attachment");
    }
    auto missing=target;missing.resolve_constructions();
    check(!missing.history.front().placement.reference_valid,"Missing source silently resolved Flat attachment");
    std::cout<<"Flat/Sheet Profile attachment: angles, flip, thickness, parameter reversal and persistence passed\n";
}
void verify_attached_history(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","attached-history"}});
    const auto id=live.active_document_id();
    const auto bend=run(host,"bend.create",{{"width_mm",40.},{"radius_mm",5.}}).data.at("container").get<std::string>();
    auto* state=live.open_part(id);std::string edge_key;
    for(const auto& edge:state->session.calculated_boundaries().back().mesh.original_references.edges) {
        try {auto refs=document::flat_sheet_references(edge);
            if(refs[1].semantic_key.starts_with("sweep:cap:end:from:")){edge_key=edge.reference.semantic_key;break;}}
        catch(const std::exception&){}
    }
    check(!edge_key.empty(),"No Bend End boundary for history test");
    const auto created=run(host,"flat.create",{{"edge_owner",bend},{"edge_key",edge_key},{"height_mm",12.}}).data;
    const auto flat=created.at("container").get<std::string>(),sketch_id=created.at("sketch").get<std::string>();
    check(created.at("sheet_attachment").get<bool>(),"Console Flat did not attach");
    check(workspace::mutate_document_sketch(live,id,sketch_id,[](auto& s) {
        s.points.clear();s.segments.clear();s.constraints.clear();s.dimensions.clear();
        const auto a=s.external_references[0].cached_points.front(),b=s.external_references[1].cached_points.front();
        s.add_rectangle(std::min(a[0],b[0]),0,std::max(a[0],b[0]),12);
    }),"Cannot create rectangle snapped to attached endpoints");
    run(host,"regenerate");
    for(double thickness:{1.,2.}) {
        run(host,"bend.set",{{"container",bend},{"thickness_mm",thickness}});
        const auto& profile=workspace::document_sketch(live,id,sketch_id);
        for(const auto& ref:profile.external_references)check(ref.source_document_id==id&&!ref.broken,"Body-local Flat reference lost its Part source");
        const auto defaults=document::sheet_metal_defaults(state->session.document());
        const auto expected=40*std::numbers::pi/2*(5+thickness*.5)*thickness+40*12*thickness;
        near(state->session.calculated_boundaries().back().volume,expected);
        near(state->session.document().find_container(flat)->flat.thickness,thickness);
    }
    run(host,"save");std::vector<kernel::BodyResult> cached;
    const auto loaded=document::PartDocument::load(directory/"attached-history.prtz",&cached);
    check(loaded.find_container(flat)->flat.sheet_attachment&&!cached.empty(),"Attached history did not reopen");
    const auto revision=state->session.revision();
    check(!host.execute({{"command","flat.set"},{"arguments",{{"container",flat},{"thickness_mm",3.}}}}).ok,"Attached Flat accepted independent thickness");
    check(state->session.revision()==revision,"Rejected Flat edit changed history");
    run(host,"undo");run(host,"redo");
    near(state->session.calculated_boundaries().back().volume,cached.back().volume);
    const auto bend_sketch=state->session.document().find_container(bend)->bend.sketch_id;
    check(workspace::mutate_document_sketch(live,id,bend_sketch,[&](auto& s) {
        check(s.set_dimension_value(s.id+":position:last",52.),"Cannot lengthen source Bend");
    }),"Bend width edit was ignored");
    run(host,"regenerate");
    near(state->session.calculated_boundaries().back().volume,52*std::numbers::pi/2*6*2+52*12*2);
    run(host,"new",{{"type","part"},{"name","opposite-bend-edge"}});
    const auto flip_id=live.active_document_id();
    const auto base=run(host,"flat.create",{{"width_mm",40.},{"height_mm",30.}}).data.at("container").get<std::string>();
    state=live.open_part(flip_id);std::vector<kernel::ViewerEdge> sides;
    for(const auto& e:state->session.calculated_boundaries().back().mesh.original_references.edges)
        if(kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&e.points.size()>=2&&
            std::abs(e.points.front().y)<1e-7&&std::abs(e.points.back().y)<1e-7)sides.push_back(e);
    check(sides.size()==2,"Cannot find opposite sheet boundary edges");
    const auto attached_bend=run(host,"bend.create",{{"edge_owner",base},{"edge_key",sides[0].reference.semantic_key}}).data.at("container").get<std::string>();
    edge_key.clear();
    for(const auto& e:state->session.calculated_boundaries().back().mesh.original_references.edges) {
        if(e.reference.owner_id!=attached_bend)continue;
        try {const auto refs=document::flat_sheet_references(e);if(refs[1].semantic_key.starts_with("sweep:cap:end:from:")){edge_key=e.reference.semantic_key;break;}}
        catch(const std::exception&){}
    }
    check(!edge_key.empty(),"Attached Bend lost its outer End");
    const auto attached_flat=run(host,"flat.create",{{"edge_owner",attached_bend},{"edge_key",edge_key},{"height_mm",12.}}).data;
    const auto attached_sketch=attached_flat.at("sketch").get<std::string>();
    const auto initial_refs=workspace::document_sketch(live,flip_id,attached_sketch).external_references;
    for(int side:{1,0,1}) {
        run(host,"bend.set",{{"container",attached_bend},{"edge_owner",base},{"edge_key",sides[side].reference.semantic_key}});
        check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Switching source Bend side broke Flat calculation");
        const auto& profile=workspace::document_sketch(live,flip_id,attached_sketch);
        const auto& bend_feature=*state->session.document().find_container(attached_bend);
        const auto end=sketcher::Sketch::from_serialized(bend_feature.bend.auxiliary_sketches[1]);
        near(profile.resolved_normal.x*end.resolved_y_axis.x+profile.resolved_normal.y*end.resolved_y_axis.y+profile.resolved_normal.z*end.resolved_y_axis.z,-1);
        for(std::size_t i=0;i<2;++i)check(profile.external_references[i].id==initial_refs[i].id&&
            profile.external_references[i].source_semantic_key==initial_refs[i].source_semantic_key,"Switching Bend side replaced Flat endpoint links");
    }
    const auto attached_flat_id=attached_flat.at("container").get<std::string>();
    run(host,"bend.set",{{"container",attached_bend},{"angle_degrees",0.}});
    check(!state->session.document().find_container(attached_flat_id)->placement.reference_valid,"Zero-angle Bend silently retained a valid solid-edge attachment");
    run(host,"bend.set",{{"container",attached_bend},{"angle_degrees",90.}});
    check(state->session.document().find_container(attached_flat_id)->placement.reference_valid,"Flat did not recover its restored Bend boundary");
}

void verify(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
    command_host::Host host(live,kernel,directory,options);run(host,"new",{{"type","part"},{"name","flat-test"}});
    const auto id=live.active_document_id();
    const auto created=run(host,"flat.create",{{"width_mm",40.},{"height_mm",30.}}).data;
    const auto owner=created.at("container").get<std::string>(),sketch_id=created.at("sketch").get<std::string>();
    auto* state=live.open_part(id);
    const auto volume=[&]{return state->session.calculated_boundaries().back().volume;};
    const auto sketch=[&]{return workspace::document_sketch(live,id,sketch_id);};
    near(volume(),1200);check(!created.at("thickness_override").get<bool>(),"Flat did not inherit Part thickness");
    {
        const auto unchanged=*state->session.document().find_container(owner);
        const auto unchanged_profile=sketch();
        state->session.update_calculated_boundaries({});
        static_cast<void>(workspace::commit_flat(live,kernel,id,unchanged,unchanged_profile));
        check(!state->session.calculated_boundaries().empty(),"Flat OK left its body uncalculated");
        near(volume(),1200);
    }
    const auto identities=faces(state->session.calculated_boundaries().back(),owner);
    check(identities.size()==6,"Flat must expose six rectangle faces");
    {
        const auto& geometry=state->session.calculated_boundaries().back().mesh.original_references;
        std::set<std::string> a,b,ends;
        for(const auto& face:geometry.triangle_references) {
            near(face.sheet_thickness,1);
            if(face.sheet_role==kernel::SheetFaceRole::SideA)a.insert(face.semantic_key);
            else if(face.sheet_role==kernel::SheetFaceRole::SideB)b.insert(face.semantic_key);
            else if(face.sheet_role==kernel::SheetFaceRole::ThicknessFace)ends.insert(face.semantic_key);
            else check(false,"Flat face has no sheet role");
        }
        check(a.size()==1&&b.size()==1&&ends.size()==4,"Flat sheet face roles are incorrect");
        check(std::ranges::count_if(geometry.edges,[](const auto& e){return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary;})==8,"Flat needs eight boundary edges");
        check(std::ranges::count_if(geometry.edges,[](const auto& e){return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Thickness;})==4,"Flat needs four thickness edges");
        std::map<std::string,unsigned> parent_counts;
        for(const auto& point:geometry.points)if(point.reference.owner_id==owner) {
            const auto& key=point.reference.semantic_key;
            check(key.starts_with("start:")||key.starts_with("end:"),"Sheet endpoint has no authored Start/End role");
            const auto parent=key.substr(key.find(':')+1);
            check(sketch().find_point(parent)!=nullptr,"Sheet endpoint lost its source Sketch point");++parent_counts[parent];
        }
        check(parent_counts.size()==4&&std::ranges::all_of(parent_counts,[](const auto& pair){return pair.second==2;}),
            "Opposite sheet endpoints do not share their source parents");
        auto assembly=assembly::AssemblyDocument::create_default();
        assembly.components.push_back(assembly::AssemblyDocument::create_part_occurrence("Sheet",id,{},state->session.calculated_boundaries().back()));
        const auto scene=assembly.build_scene();
        for(const auto& edge:scene.original_references.edges)if(kernel::sheet_edge_role(edge)!=kernel::SheetEdgeRole::Unknown) {
            for(const auto& side:edge.edge_treatment_side_references)check(side.instance_path==edge.reference.instance_path,"Assembly sheet side lost occurrence ownership");
            for(const auto& point:edge.edge_treatment_endpoint_references)check(point.instance_path==edge.reference.instance_path,"Assembly sheet endpoint lost occurrence ownership");
        }
    }
    for(const auto& segment:sketch().segments)
        check(identities.contains("generated:"+segment.id),"Flat side lost its source curve identity");
    check(std::ranges::count_if(identities,[](const auto& key){return key.starts_with("start:from:")||key.starts_with("end:from:");})==2,
        "Flat Start/End faces lost their profile region ancestry");
    for(const auto* direction:{"forward","reverse","symmetric"}) {
        run(host,"flat.set",{{"container",owner},{"direction",direction},{"thickness_mm",2.}});
        near(volume(),2400);
        const double start=std::string(direction)=="forward"?0:std::string(direction)=="reverse"?-2:-1;
        const auto bounds=span(state->session.calculated_boundaries().back().mesh.vertices,sketch());near(bounds.first,start);near(bounds.second,start+2);
        check(faces(state->session.calculated_boundaries().back(),owner)==identities,"Changing Flat side replaced topology identities");
        const auto preview=document::flat_preview(*state->session.document().find_container(owner),sketch(),document::sheet_metal_defaults(state->session.document()));
        std::vector<kernel::Vec3> points;for(const auto& e:preview.edges)points.insert(points.end(),e.points.begin(),e.points.end());
        const auto wire=span(points,sketch());near(wire.first,start);near(wire.second,start+2);
    }
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",3.}}}});run(host,"regenerate");near(volume(),2400);
    run(host,"flat.set",{{"container",owner},{"thickness_override",false}});near(volume(),3600);
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",4.}}}});near(volume(),4800);
    // A circle in the closed profile makes a through hole; it is not filled.
    check(workspace::mutate_document_sketch(live,id,sketch_id,[](auto& s){
        static_cast<void>(s.add_circle(20,15,3));static_cast<void>(s.add_ellipse(8,15,12,15,8,17));
    }),"Profile edit was not committed");
    run(host,"regenerate");
    const double area=1200-17*std::numbers::pi;near(volume(),area*4);
    const auto verify_axes=[&](const kernel::BodyResult& body) {
        std::set<std::string> keys;
        for(const auto& axis:body.mesh.original_references.axes)if(axis.reference.owner_id==owner) {
            keys.insert(axis.reference.semantic_key);
            check(std::ranges::any_of(body.mesh.axes,[&](const auto& visible){return visible.reference==axis.reference;}),"Flat axis is not visible in the View");
            const auto normal=sketch().normal();near(std::abs(axis.direction.x*normal.x+axis.direction.y*normal.y+axis.direction.z*normal.z),1);
        }
        check(keys.size()==2,"Circle and ellipse must produce two Flat axes");return keys;
    };
    const auto axis_keys=verify_axes(state->session.calculated_boundaries().back());
    for(const auto* side:{"forward","reverse","symmetric"}) {
        run(host,"flat.set",{{"container",owner},{"direction",side}});
        check(verify_axes(state->session.calculated_boundaries().back())==axis_keys,"Flat side change replaced hole axes");
    }
    std::string radius_dimension;
    check(workspace::mutate_document_sketch(live,id,sketch_id,[&](auto& s){
        auto dimension=s.create_circle_radius_dimension(s.circles.front().id);radius_dimension=dimension.id;s.apply_dimension(dimension);
    }),"Cannot dimension Flat profile opening");
    for(double radius:{5.,3.}) {
        check(workspace::mutate_document_sketch(live,id,sketch_id,[&](auto& s){
            check(s.set_dimension_value(radius_dimension,radius),"Cannot change Flat Sketch radius");
        }),"Flat profile dimension edit was ignored");
        run(host,"regenerate");near(volume(),(1200-(radius*radius+8)*std::numbers::pi)*4);
    }
    auto pending=*state->session.document().find_container(owner);auto profile=sketch();
    profile.plane=sketcher::SketchPlane::YZ;profile.plane_auto=false;
    check(workspace::commit_flat(live,kernel,id,pending,profile),"Changing Flat plane was ignored");near(volume(),area*4);
    const auto yz=span(state->session.calculated_boundaries().back().mesh.vertices,sketch());near(yz.first,-2);near(yz.second,2);
    run(host,"save");std::vector<kernel::BodyResult> saved;
    const auto reopened=document::PartDocument::load(directory/"flat-test.prtz",&saved);
    check(reopened.find_container(owner)->flat==state->session.document().find_container(owner)->flat,"Flat parameters changed after reopen");
    near(saved.back().volume,area*4);
    check(verify_axes(saved.back())==axis_keys,"Native reopen changed Flat hole axes");
    run(host,"flat.set",{{"container",owner},{"thickness_mm",5.}});near(volume(),area*5);
    check(verify_axes(state->session.calculated_boundaries().back())==axis_keys,"Flat thickness change replaced hole axes");
    run(host,"undo");near(volume(),area*4);run(host,"redo");near(volume(),area*5);
    for(const auto& bad:{Json{{"thickness_mm",0.}},Json{{"thickness_mm",-1.}},Json{{"direction","two_sides"}}}) {
        auto args=bad;args["container"]=owner;const auto revision=state->session.revision();
        check(!host.execute({{"command","flat.set"},{"arguments",args}}).ok,"Invalid Flat parameters were accepted");
        check(state->session.revision()==revision,"Rejected edit changed history");near(volume(),area*5);
    }
    pending=*state->session.document().find_container(owner);profile=sketch();profile.segments.pop_back();
    bool rejected=false;try{static_cast<void>(workspace::commit_flat(live,kernel,id,pending,profile));}catch(const std::exception&){rejected=true;}
    check(rejected,"Open sheet profile was accepted");near(volume(),area*5);
    pending=*state->session.document().find_container(owner);pending.combine_mode=document::CombineMode::Subtract;
    rejected=false;try{static_cast<void>(workspace::commit_flat(live,kernel,id,pending,sketch()));}catch(const std::exception&){rejected=true;}
    check(rejected,"Flat accepted subtraction");
    const auto references=workspace::family_references(live,id);
    check(std::ranges::any_of(references,[&](const auto& ref){return ref.binding.owner_id==owner&&ref.binding.semantic_key=="parameter:thickness";}),"Local Flat thickness is absent from Family Table");
    auto table=workspace::family_table(live,id);table.columns.push_back("Sheet thickness");
    table.bindings["Sheet thickness"]={"dimension",owner,"parameter:thickness"};
    table.instances.push_back({"Thin sheet",{{"Sheet thickness","1.5"}}});
    static_cast<void>(workspace::set_family_table(live,id,table));
    const auto variant=workspace::open_family_instance(live,kernel,id,"Thin sheet",false);
    near(live.open_part(variant)->session.calculated_boundaries().back().volume,area*1.5);
    run(host,"new",{{"type","assembly"},{"name","no-flat"}});
    check(!host.execute({{"command","flat.create"},{"arguments",Json::object()}}).ok,"Assembly accepted Flat");
}
void verify_ellipse_regions() {
    kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();
    auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Flat;
    feature.flat.thickness_override=true;feature.flat.thickness=2;
    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;feature.flat.sketch_id=sketch.id;
    static_cast<void>(sketch.add_ellipse(0,0,10,0,0,5));
    static_cast<void>(sketch.add_ellipse(0,0,2,0,0,1));
    static_cast<void>(sketch.add_ellipse(30,0,34,0,30,2));
    part.history={feature};part.sketches={sketch};part.resolve_constructions();
    for(auto direction:{document::ExtrusionDirection::Forward,document::ExtrusionDirection::Reverse,document::ExtrusionDirection::Symmetric}) {
        part.history.front().flat.direction=direction;
        const auto result=kernel.evaluate_history(part.kernel_operations());near(result.back().volume,112*std::numbers::pi);
    }
    // The same exact profile classifier serves ordinary Extrusion.
    auto extrusion=document::PartDocument::create_extrusion_container(sketch.id);
    part.sketches.front().owner_container_id=extrusion.id;extrusion.extrusion.height=extrusion.extrusion.length_forward=2;
    part.history={extrusion};part.resolve_constructions();near(kernel.evaluate_history(part.kernel_operations()).back().volume,112*std::numbers::pi);
    part.sketches.front().ellipses.clear();part.sketches.front().circles.clear();
    static_cast<void>(part.sketches.front().add_rectangle(-10,-10,10,10));
    static_cast<void>(part.sketches.front().add_ellipse(9,0,13,0,9,2));
    bool rejected=false;try{static_cast<void>(part.kernel_operations());}catch(const std::exception&){rejected=true;}
    check(rejected,"Intersecting ellipse and rectangle were accepted as closed regions");
    part.sketches.front().ellipses.clear();static_cast<void>(part.sketches.front().add_circle(0,0,3));
    for(auto direction:{document::ExtrusionDirection::Forward,document::ExtrusionDirection::Reverse,document::ExtrusionDirection::Symmetric}) {
        part.history.front().extrusion.direction=direction;
        near(kernel.evaluate_history(part.kernel_operations()).back().volume,2*(400-9*std::numbers::pi));
    }
    auto text=sketcher::Sketch::create_text();text.value="I";
    text.contours={{{20,0},{25,0},{25,10},{20,10}}};part.sketches.front().add_text(std::move(text));
    for(auto direction:{document::ExtrusionDirection::Forward,document::ExtrusionDirection::Reverse,document::ExtrusionDirection::Symmetric}) {
        part.history.front().extrusion.direction=direction;
        near(kernel.evaluate_history(part.kernel_operations()).back().volume,2*(450-9*std::numbers::pi));
    }
}
}
#include "flat_revolution_attachment.inc"

int main() {
    const auto directory=std::filesystem::temp_directory_path()/("zima-flat-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try{verify_revolution_attachment(directory);verify_bend_attachment(directory);verify_attached_history(directory);verify(directory);verify_ellipse_regions();std::filesystem::remove_all(directory);std::cout<<"Flat geometry, directions, defaults, topology and history passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"; fixture: "<<directory<<'\n';return 1;}
}

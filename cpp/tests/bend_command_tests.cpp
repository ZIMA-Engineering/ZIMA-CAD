#include <zima/command_host/host.hpp>
#include <zima/document/bend.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/workspace/bend_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/sketch_operations.hpp>
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
    check(created.at("thickness_mm")==1.&&!created.at("thickness_override").get<bool>(),"Bend did not inherit default 1 mm");
    run(host,"bend.set",{{"container",owner},{"radius_follows_thickness",true}});
    near(volume(),40*pi/2*(4-1)/2);
    check(run(host,"bend.get",{{"container",owner}}).data.at("radius_mm")==1.,"Linked radius did not inherit thickness");
    run(host,"document.settings.set",{{"sheet_metal",{{"thickness_mm",1.5}}}});
    run(host,"regenerate");near(volume(),40*pi/2*(9-2.25)/2);
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
    near(volume(),40*pi/2*(5+initial_k)); // Defaults alone preserve calculated geometry until explicit regeneration.
    run(host,"regenerate");near(volume(),40*2*pi/2*5.8);
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
    try{verify(directory);std::filesystem::remove_all(directory);std::cout<<"Bend geometry, identities, defaults, history and persistence passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"; fixture: "<<directory<<'\n';return 1;}
}

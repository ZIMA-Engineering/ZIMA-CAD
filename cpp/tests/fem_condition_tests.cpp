#include "profile_solid_fixture.hpp"
#include <zima/document/fem_presentation.hpp>
#include <zima/workspace/fem_condition_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/workspace/modeling_confirmation.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <filesystem>
#include <numbers>
#include <ranges>

using namespace zima;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Invalid FEM condition accepted");}
void serialization(){
    for(auto kind:{document::FemConditionKind::Constraint,document::FemConditionKind::Force,document::FemConditionKind::Pressure})
    for(auto region:{document::FemRegionKind::Point,document::FemRegionKind::Curve,document::FemRegionKind::Surface}){
        if(kind==document::FemConditionKind::Pressure&&region!=document::FemRegionKind::Surface)continue;
        document::FemCondition c;c.id="condition";c.name="FEM Unicode žluťoučký";c.kind=kind;c.region=region;
        c.references={{"source","native-key",{}}};c.placement.z=13.23456789123;c.placement.rotation_x=-27.25;
        c.placement.references={{"","source","face",-0.,true}};c.placement.references.front().flip=true;
        c.force_N=-5000.23456789123;c.pressure_MPa=2.3456789123;c.pressure_reversed=true;c.restrained={true,false,true};
        const nlohmann::json json=c;const auto restored=json.get<document::FemCondition>();require(restored==c,"Condition persistence changed authored fields");
        require(std::signbit(restored.placement.references.front().offset),"Condition placement lost signed zero");
        auto invalid=json;invalid["references"].push_back(invalid["references"][0]);rejects([&]{static_cast<void>(invalid.get<document::FemCondition>());});
        invalid=json;invalid["region"]="unknown";rejects([&]{static_cast<void>(invalid.get<document::FemCondition>());});
        invalid=json;invalid["restrained"]={true,false};rejects([&]{static_cast<void>(invalid.get<document::FemCondition>());});
        invalid=json;invalid["placement"]["x"]=nullptr;rejects([&]{static_cast<void>(invalid.get<document::FemCondition>());});
    }
    document::FemCondition c;c.id="c";c.name="Constraint";c.restrained={false,false,false};rejects([&]{document::validate_fem_condition(c);});
    c.kind=document::FemConditionKind::Force;c.force_N=std::numeric_limits<double>::quiet_NaN();rejects([&]{document::validate_fem_condition(c);});
    c.force_N=100;c.kind=document::FemConditionKind::Pressure;rejects([&]{document::validate_fem_condition(c);});
    c.region=document::FemRegionKind::Surface;rejects([&]{document::validate_fem_condition(c);});
    c.references={{"owner","face",{}}};c.pressure_MPa=-1;rejects([&]{document::validate_fem_condition(c);});
    c.kind=document::FemConditionKind::Force;c.pressure_MPa=1;c.restrained={true,true,true};
    auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
    sketch.owner_container_id=c.id;sketch.plane_offset=17.25;
    static_cast<void>(sketch.add_rectangle(-2,-1,2,1));
    c.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{{1.25,0,11}}};
    const nlohmann::json owned=c;
    require(owned.get<document::FemCondition>()==c,"Owned FEM Sketch or seed did not round-trip");
    auto missing_mode=owned;missing_mode["projected_sketch"].erase("project_to_body");
    rejects([&]{static_cast<void>(missing_mode.get<document::FemCondition>());});
    const auto request=document::fem_condition_projection_request(c);
    require(request.profiles.size()==4&&std::abs(request.sketch_origin.z-17.25)<1e-10,
        "Owned FEM Sketch lost its exact curves or plane offset");
    c.projected_sketch->surface_seeds.clear();
    rejects([&]{document::validate_fem_condition(c);});
    c.projected_sketch->project_to_body=false;c.references.clear();
    require(nlohmann::json(c).get<document::FemCondition>()==c,"Planar FEM mode did not round-trip");
    for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})for(bool automatic:{false,true}) {
        auto pressure=c;pressure.kind=document::FemConditionKind::Pressure;pressure.placement={};
        pressure.placement.reference_valid=true;pressure.placement.value_locks.insert("profile_offset");
        auto profile=sketcher::Sketch::from_serialized(pressure.projected_sketch->serialized);
        profile.plane=plane;profile.plane_auto=automatic;profile.refresh_default_frame();
        pressure.projected_sketch->serialized=profile.serialized();
        document::resolve_fem_condition_sketch_plane(pressure,{});
        require(nlohmann::json(pressure).get<document::FemCondition>()==pressure,"FEM pressure plane/automatic mode/lock did not persist");
        auto expected=profile;if(automatic)expected.plane=sketcher::SketchPlane::XY;expected.refresh_default_frame();
        const auto placed=document::fem_condition_sketch(pressure);
        require(placed.normal()==expected.normal()&&placed.world_point(0,0)==expected.world_point(0,0),"FEM work plane or offset equation failed");
        for(bool reverse:{false,true}) {
            pressure.pressure_reversed=reverse;
            const auto marks=document::fem_condition_presentation(pressure,{});
            const auto arrow=std::ranges::find_if(marks.edges,[](const auto& edge){return edge.reference.semantic_key.starts_with("fem:mark:");});
            require(arrow!=marks.edges.end(),"Sketch pressure has no normal arrow");
            const auto tail=arrow->points[0],tip=arrow->points[1],n=placed.normal(),origin=placed.world_point(0,0);
            const double direction=(tip.x-tail.x)*n.x+(tip.y-tail.y)*n.y+(tip.z-tail.z)*n.z;
            require((reverse?direction:-direction)>9.99&&std::abs((tip.x-origin.x)*n.x+(tip.y-origin.y)*n.y+(tip.z-origin.z)*n.z)<1e-7,
                "Sketch pressure arrow lost its plane, offset or normal side");
        }
        pressure.projected_sketch->project_to_body=true;
        pressure.references={{"source","face",{}}};pressure.projected_sketch->surface_seeds={{0,0,0}};
        require(nlohmann::json(pressure).get<document::FemCondition>()==pressure,"Projected Sketch pressure did not persist");
    }
}
void marks(){
    document::FemCondition c;c.id="condition";c.name="F1";c.kind=document::FemConditionKind::Force;c.force_N=5000;c.placement.rotation_y=90;
    const auto first=document::fem_condition_presentation(c,{});
    require(first.edges.size()==2&&first.constraint_markers.front().label=="F1 = 5000 N","Force symbol or label missing");
    const auto tail=first.edges.front().points[0],tip=first.edges.front().points[1];
    require(tip.x-tail.x>9.999&&std::abs(tip.z-tail.z)<1e-8,"Force did not follow the container normal");
    c.force_N=-5000;const auto reversed=document::fem_condition_presentation(c,{});
    require(reversed.edges.front().points[0].x>9.999,"Negative force failed to reverse its arrow");
    c.kind=document::FemConditionKind::Constraint;require(document::fem_condition_presentation(c,{}).edges.size()==18,"Fixed support lacks its restrained directions");
    c.restrained={true,false,false};require(document::fem_condition_presentation(c,{}).edges.size()==6,"Partial restraint looks like a fixed support");
    c.visible=false;require(document::fem_condition_presentation(c,{}).edges.empty(),"Hidden condition still paints its mark");
}
void transactions(){
    workspace::Workspace live;kernel::OcctKernel engine;auto doc=document::PartDocument::create_default();const auto id=doc.document_id;
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Stock"));doc.set_body_history(std::move(graph));
    live.add_part(doc);live.activate(id);auto base_sketch=sketcher::Sketch::create_default();base_sketch.plane_auto=false;
    static_cast<void>(base_sketch.add_rectangle(-10,-10,10,10));auto base=document::PartDocument::create_feature_container(base_sketch.id);base.feature.sides[0].length=11;
    workspace::commit_profile(live,engine,id,base,workspace::ProfileEditMode::Create,base_sketch);
    auto& session=live.open_part(id)->session;kernel::FaceReference target;
    for(const auto& face:session.calculated_boundaries().back().mesh.triangle_references)if(face.owner_id==base.id&&face.semantic_key.find("end:from:")!=std::string::npos)target=face;
    require(target.valid(),"Missing test top face");
    auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;static_cast<void>(sketch.add_circle(0,0,4));
    auto projection=document::PartDocument::create_feature_container(sketch.id);projection.placement.z=20;projection.feature.type=document::FeatureType::Projection;
    projection.feature.projection.output=document::ProjectionOutput::Surface;
    document::ExtrusionParameters::EndTarget selected;selected.reference=target;projection.feature.projection.targets={selected};projection.feature.projection.regions={{target,{0,0,11},{}}};
    workspace::commit_profile(live,engine,id,projection,workspace::ProfileEditMode::Create,sketch);
    const auto source_id=session.document().body_history.active_body_id();
    require(session.calculated_boundaries().back().calculation_errors.empty(),"Projection patch fixture calculation failed");
    const auto& computed=session.calculated_boundaries().back().body_outputs.at(source_id).get();
    require(computed.solid_present&&!computed.kernel_shape.empty()&&!computed.source_fingerprint.empty()&&computed.calculation_errors.empty(),"Projected source lost its valid solid snapshot");
    session.activate_body({});
    const auto creation=workspace::prepare_fem_edit(session.document());document::BodyHistory fem;fem.scope.id=creation.object_id;fem.name="Analysis";fem.fem=document::FemDefinition{source_id};
    require(workspace::commit_fem(live,creation,fem),"FEM creation failed");const auto source=workspace::read_fem_source(session,fem.scope.id);
    document::FemRegionReference patch;
    for(const auto& face:source->mesh.triangle_references)if(face.owner_id==projection.id){patch={face.owner_id,face.semantic_key,face.instance_path};break;}
    require(!patch.owner_id.empty(),"FEM source lost its bounded Projection surface");
    const auto count=kernel::body_calculation_count.load();const auto* boundaries=&session.calculated_boundaries();
    document::FemCondition force;force.id="force";force.name="F1";force.kind=document::FemConditionKind::Force;force.force_N=5000;
    force.region=document::FemRegionKind::Surface;force.references={patch};
    auto edit=workspace::prepare_fem_edit(session.document(),fem.scope.id);
    require(workspace::commit_fem_condition(live,edit,force),"Force on projected patch was not committed");
    const auto after=session.revision();edit=workspace::prepare_fem_edit(session.document(),fem.scope.id);
    force=*workspace::find_fem_condition(session.document(),fem.scope.id,force.id);
    require(!workspace::commit_fem_condition(live,edit,force)&&session.revision()==after,"Unchanged condition added Undo");
    auto bad=force;bad.references.front().semantic_key="missing";rejects([&]{static_cast<void>(workspace::commit_fem_condition(live,edit,bad));});
    require(session.revision()==after,"Invalid region modified the document");
    for(auto condition_kind:{document::FemConditionKind::Force,document::FemConditionKind::HeatSource})
    for(bool projecting:{false,true})for(auto region:{document::FemRegionKind::Curve,document::FemRegionKind::Surface})
    for(bool diameter:{false,true})for(bool reference:{false,true})for(bool locked:{false,true})for(bool fixed:{false,true}) {
        if(condition_kind==document::FemConditionKind::HeatSource&&region!=document::FemRegionKind::Surface)continue;
        auto value=force;value.kind=condition_kind;value.id="editable-fem-sketch-"+std::to_string(int(condition_kind))+"-"+std::to_string(projecting)+"-"+std::to_string(int(region));
        value.name="Editable FEM Sketch";value.region=region;
        value.references=projecting?std::vector<document::FemRegionReference>{patch}:
            std::vector<document::FemRegionReference>{};
        auto editable=sketcher::Sketch::create_default();editable.plane_auto=false;editable.owner_container_id=value.id;
        editable.plane_offset=projecting?20.:5.;
        const auto circle=editable.add_circle(0,0,1.5);
        auto dimension=diameter?editable.create_circle_diameter_dimension(circle):editable.create_circle_radius_dimension(circle);
        dimension.driving=!reference;dimension.locked=locked&&!reference;editable.apply_dimension(dimension);
        const auto center_id=editable.circles.front().center_point_id;editable.set_point_fixed(center_id,fixed);
        for(double radius:{1.,2.,1.5}) {
            const double old_radius=editable.circles.front().radius;
            require(editable.set_dimension_value(dimension.id,diameter?2*radius:radius),"FEM Sketch dimension edit failed");
            const double actual=editable.circles.front().radius;
            require(std::abs(actual-(reference?old_radius:radius))<1e-7,"FEM Sketch radius equation failed");
            const auto before=*editable.find_point(center_id);
            static_cast<void>(editable.move_point(center_id,before.x+.15,before.y-.1));
            const auto center=*editable.find_point(center_id);
            const double movement=std::hypot(center.x-before.x,center.y-before.y);
            require(fixed?movement<1e-8:movement>1e-6,"FEM Sketch drag escaped constraints or failed to move");
            require(std::abs(editable.circles.front().radius-actual)<1e-7,"FEM Sketch drag changed its radius");
            const auto keys=value.projected_sketch?value.projected_sketch->surface_region_keys:std::vector<std::string>{};
            value.projected_sketch=document::FemProjectedSketch{editable.serialized(),{},keys,projecting};
            if(projecting&&region==document::FemRegionKind::Surface)
                value.projected_sketch->surface_seeds={{center.x,center.y,11}};
            static_cast<void>(workspace::commit_fem_condition(live,workspace::prepare_fem_edit(session.document(),fem.scope.id),value));
            value=*workspace::find_fem_condition(session.document(),fem.scope.id,value.id);
            std::vector<TopoDS_Shape> shapes;
            const auto body=kernel::read_calculated_body_shape(source.get());
            if(projecting) {
                const auto topology=kernel::read_calculated_body_topology(source.get(),{kernel::FaceReference{patch.owner_id,patch.semantic_key,patch.instance_path}});
                const auto result=kernel::project_calculated_mesh_region(topology.shape,document::fem_condition_projection_request(value),
                    topology.referenced_shapes.front(),value.projected_sketch->surface_seeds,value.projected_sketch->surface_region_keys);
                shapes=result.faces;for(const auto& curve:result.curves)shapes.push_back(curve.edge);
            }else shapes=kernel::planar_calculated_mesh_region(body,document::fem_condition_planar_request(value));
            double measured=0;
            for(const auto& shape:shapes) {
                GProp_GProps properties;
                if(region==document::FemRegionKind::Surface)BRepGProp::SurfaceProperties(shape,properties);
                else BRepGProp::LinearProperties(shape,properties);
                measured+=properties.Mass();
                require(std::abs(properties.CentreOfMass().Z()-(projecting?11.:5.))<1e-7,"FEM region left its expected plane");
            }
            const double expected=region==document::FemRegionKind::Surface?std::numbers::pi*actual*actual:2*std::numbers::pi*actual;
            require(std::abs(measured-expected)<1e-7,"FEM edited region has incorrect exact area/length");
            const auto unchanged=session.revision();
            require(!workspace::commit_fem_condition(live,workspace::prepare_fem_edit(session.document(),fem.scope.id),value)&&
                session.revision()==unchanged,"Unchanged owned Sketch created an Undo step");
            require(session.undo()&&session.redo()&&workspace::find_fem_condition(session.document(),fem.scope.id,value.id)->projected_sketch==value.projected_sketch,
                "FEM Sketch Undo/Redo lost its mode or geometry");
        }
    }
    const auto* projected_surface=workspace::find_fem_condition(session.document(),fem.scope.id,
        "editable-fem-sketch-"+std::to_string(int(document::FemConditionKind::Force))+"-1-2");
    require(projected_surface,"Projected Force surface fixture is missing");
    auto outside=*projected_surface;
    outside.references.clear();outside.projected_sketch->project_to_body=false;
    outside.projected_sketch->surface_seeds.clear();outside.projected_sketch->surface_region_keys.clear();
    const auto before_rejection=session.revision();
    rejects([&]{static_cast<void>(workspace::commit_fem_condition(live,workspace::prepare_fem_edit(session.document(),fem.scope.id),outside));});
    require(session.revision()==before_rejection,"Off-body planar region rejection changed the document");
    for(auto rotation:{kernel::Vec3{0,0,0},kernel::Vec3{90,0,0},kernel::Vec3{-90,0,0},kernel::Vec3{0,90,0},kernel::Vec3{0,-90,0}}) {
        auto value=force;value.id="planar-holes";value.region=document::FemRegionKind::Surface;value.references.clear();
        value.placement={};value.placement.z=5.;
        value.placement.rotation_x=rotation.x;value.placement.rotation_y=rotation.y;
        auto owned=sketcher::Sketch::create_default();owned.plane_auto=false;owned.owner_container_id=value.id;
        static_cast<void>(owned.add_circle(0,0,2.));static_cast<void>(owned.add_circle(0,0,1.));
        value.projected_sketch=document::FemProjectedSketch{owned.serialized(),{},{},false};
        const auto shapes=kernel::planar_calculated_mesh_region(kernel::read_calculated_body_shape(source.get()),
            document::fem_condition_planar_request(value));
        double area=0;
        for(const auto& shape:shapes) {GProp_GProps properties;BRepGProp::SurfaceProperties(shape,properties);area+=properties.Mass();
            require(properties.CentreOfMass().Distance(gp_Pnt(0,0,5))<1e-7,"Rotated planar region lost its placed center");}
        require(std::abs(area-3*std::numbers::pi)<1e-7,"Planar FEM surface lost its inner hole or changed under rotation");
    }
    {
        auto value=force;value.id="external-fem-sketch";value.name="External FEM Sketch";
        value.region=document::FemRegionKind::Curve;value.references.clear();
        auto owned=sketcher::Sketch::create_default();owned.plane_auto=false;owned.owner_container_id=value.id;owned.plane_offset=5.;
        owned.refresh_default_frame();
        const auto edge=std::ranges::find_if(source->mesh.edges,[&](const auto& e){return e.reference.owner_id==base.id&&e.points.size()>=2&&
            std::hypot(e.points.back().x-e.points.front().x,e.points.back().y-e.points.front().y)>19.;});
        require(edge!=source->mesh.edges.end(),"External FEM source edge is missing");
        auto ref=workspace::prepare_sketch_external_reference(live,id,owned,sketcher::ExternalReferenceKind::Edge,
            edge->reference.owner_id,edge->reference.semantic_key,{},fem.scope.id,false,true);
        const auto ref_id=ref.id;
        owned.add_external_reference(ref);static_cast<void>(owned.add_external_profile_geometry(ref_id));
        value.projected_sketch=document::FemProjectedSketch{owned.serialized(),{},{},false};
        static_cast<void>(workspace::commit_fem_condition(live,workspace::prepare_fem_edit(session.document(),fem.scope.id),value));
        const auto* stored=workspace::find_fem_condition(session.document(),fem.scope.id,value.id);
        require(stored&&sketcher::Sketch::from_serialized(stored->projected_sketch->serialized).external_references.front().id==ref_id,
            "FEM lost its read-only external Sketch reference");
        require(workspace::read_fem_source(session,fem.scope.id).shares_with(source),"FEM external projection modified its source");
    }
    std::cout<<"FEM owned Sketch matrix: 96 Force/Heat modes/region/radius-diameter/reference/lock/fix combinations with three edits and drags each passed\n";
    for(auto region:{document::FemRegionKind::Point,document::FemRegionKind::Curve}) {
        auto value=force;value.id=region==document::FemRegionKind::Point?"point-force":"curve-force";value.region=region;value.references.clear();
        if(region==document::FemRegionKind::Point) {
            require(!source->mesh.original_references.points.empty(),"Source has no exact native points");
            const auto& ref=source->mesh.original_references.points.front().reference;value.references.push_back({ref.owner_id,ref.semantic_key,ref.instance_path});
        } else {
            require(!source->mesh.edges.empty(),"Source has no exact native curves");
            const auto& ref=source->mesh.edges.front().reference;value.references.push_back({ref.owner_id,ref.semantic_key,ref.instance_path});
        }
        require(workspace::commit_fem_condition(live,workspace::prepare_fem_edit(session.document(),fem.scope.id),value),"Point/Curve condition did not commit");
        require(document::fem_condition_presentation(value,source->mesh).constraint_markers.front().label.find('!')==std::string::npos,"Point/Curve reference did not resolve");
    }
    edit=workspace::prepare_fem_edit(session.document(),fem.scope.id);
    auto pressure=force;pressure.id="pressure";pressure.name="P1";pressure.kind=document::FemConditionKind::Pressure;pressure.pressure_MPa=2.5;
    require(workspace::commit_fem_condition(live,edit,pressure),"Pressure on projected patch failed");
    const auto symbols=document::fem_condition_presentation(pressure,source->mesh);
    require(symbols.edges.size()>=4,"Surface pressure did not show several arrows");
    for(const auto& e:symbols.edges)for(auto p:e.points)require(std::hypot(p.x,p.y)<6,"Pressure symbol escaped the bounded region");
    pressure.pressure_reversed=true;edit=workspace::prepare_fem_edit(session.document(),fem.scope.id);require(workspace::commit_fem_condition(live,edit,pressure),"Pressure normal edit failed");
    require(session.undo()&&!workspace::find_fem_condition(session.document(),fem.scope.id,pressure.id)->pressure_reversed,"Condition Undo failed");
    require(session.redo()&&workspace::find_fem_condition(session.document(),fem.scope.id,pressure.id)->pressure_reversed,"Condition Redo failed");
    auto encoded=nlohmann::json(*session.document().body_history.find(fem.scope.id)->fem);require(encoded.get<document::FemDefinition>()==*session.document().body_history.find(fem.scope.id)->fem,"FEM condition list failed serialization");
    const auto path=std::filesystem::temp_directory_path()/"zima-fem-conditions-test.prtz";session.document().save(path,session.calculated_boundaries());
    const auto reopened=document::PartDocument::load(path);require(reopened.body_history==session.document().body_history,"Native save/reopen lost FEM conditions");std::filesystem::remove(path);
    edit=workspace::prepare_fem_edit(session.document(),fem.scope.id);require(workspace::erase_fem_condition(live,edit,force.id),"Condition deletion failed");
    require(!workspace::find_fem_condition(session.document(),fem.scope.id,force.id)&&session.undo()&&workspace::find_fem_condition(session.document(),fem.scope.id,force.id),"Condition deletion Undo failed");
    require(kernel::body_calculation_count.load()==count&&boundaries==&session.calculated_boundaries()&&workspace::read_fem_source(session,fem.scope.id).shares_with(source),"Condition edits recalculated or duplicated the source");
    for(bool measurement:{false,true}) {
    auto damaged=session.document();auto changed=*damaged.body_history.find(fem.scope.id);
    auto& placed=changed.fem->conditions.front();placed.placement.x=3;placed.placement.y=4;placed.placement.z=5;
    if(measurement){placed.kind=document::FemConditionKind::MeasurementPoint;placed.region=document::FemRegionKind::Point;placed.references.clear();placed.projected_sketch.reset();}
    placed.placement.rotation_x=placed.placement.absolute_rotation_x=27;
    placed.placement.references={{"","removed-source","origin:point",0,false}};
    const auto last=placed.placement;const auto placed_id=placed.id;damaged.body_history.update_body(changed);
    for(int i=0;i<3;++i)damaged.resolve_constructions({});
    const auto* retained=workspace::find_fem_condition(damaged,fem.scope.id,placed_id);
    require(retained&&!retained->placement.reference_valid&&retained->placement.x==last.x&&retained->placement.y==last.y&&retained->placement.z==last.z&&retained->placement.rotation_x==last.rotation_x,"Lost FEM placement moved its last valid frame");
    session.commit_metadata(damaged);require(session.undo()&&session.redo(),"Broken condition placement Undo/Redo failed");
    session.document().save(path,session.calculated_boundaries());auto repaired=document::PartDocument::load(path);std::filesystem::remove(path);
    require(workspace::find_fem_condition(repaired,fem.scope.id,placed_id)->placement==retained->placement,"Reopen changed broken condition frame");
    changed=*repaired.body_history.find(fem.scope.id);changed.fem->conditions.front().placement.references={{"",source_id+":origin","origin:point",0,false}};repaired.body_history.update_body(changed);repaired.resolve_constructions({});
    const auto* fixed=workspace::find_fem_condition(repaired,fem.scope.id,placed_id);
    require(fixed->placement.reference_valid&&std::hypot(fixed->placement.x,fixed->placement.y,fixed->placement.z)<1e-8,"Replacing a lost FEM reference did not restore its frame");
    }
}
}
#include "fem_distance_transactions.inc"
#include "fem_confirmation_transactions.inc"
int main(){try{serialization();marks();transactions();distance_transactions();fem_confirmation_transactions();std::cout<<"FEM condition serialization, validation, partial Projection region, marks, transactions, unresolved confirmation/repair, Undo/Redo, native reopen and no implicit CAD recalculation passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "profile_solid_fixture.hpp"
#include <zima/document/fem_presentation.hpp>
#include <zima/workspace/fem_condition_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <filesystem>

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
    auto damaged=session.document();auto changed=*damaged.body_history.find(fem.scope.id);
    auto& placed=changed.fem->conditions.front();placed.placement.x=3;placed.placement.y=4;placed.placement.z=5;
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
int main(){try{serialization();marks();transactions();std::cout<<"FEM condition serialization, validation, partial Projection region, marks, transactions, Undo/Redo, native reopen and no CAD recalculation passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

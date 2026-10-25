#include <zima/document/loft.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/object_annotation_frames.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/workspace/loft_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <filesystem>
#include <iostream>
#include <numbers>
namespace d=zima::document;namespace w=zima::workspace;namespace s=zima::sketcher;namespace k=zima::kernel;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void near(double a,double b){if(std::abs(a-b)>1e-4)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
struct Fixture {d::PartDocument part;d::HistoryContainer loft;};
Fixture fixture(bool guided) {
 Fixture f;f.part=d::PartDocument::create_default();f.part.history.clear();f.part.sketches.clear();
 f.part.constructions.clear();f.part.history_order.clear();d::BodyHistoryGraph graph;
 static_cast<void>(graph.create_body("Loft Body"));f.loft=d::create_loft();f.loft.loft.profiles.clear();
 f.loft.loft.sketches.clear();
 for(unsigned i=0;i<(guided?5u:3u);++i) {
  auto owned=d::create_general_surface_sketch(f.loft);auto sketch=s::Sketch::from_serialized(owned.sketch_serialized);
  sketch.plane_auto=false;
  if(i<3){owned.sketch_feature->placement.z=5.*i;static_cast<void>(sketch.add_circle(0,0,10));f.loft.loft.profiles.push_back(owned.sketch_feature->id);}
  else {sketch.plane=s::SketchPlane::XZ;static_cast<void>(sketch.add_segment(i==3?10:-10,0,i==3?10:-10,-10));f.loft.loft.guides.push_back(owned.sketch_feature->id);}
  owned.sketch_serialized=sketch.serialized();f.loft.loft.sketches.push_back(std::move(owned));
 }
 d::resolve_loft_sketches(f.loft,{});
 f.part.set_body_history(graph);f.part.resolve_constructions();return f;
}
int main(){try {
 k::OcctKernel kernel;
 {
  const auto empty=d::create_loft();
  check(empty.loft.sketches.empty()&&empty.loft.profiles.empty()&&empty.loft.guides.empty(),"New Loft created unrequested Sketches");
  auto f=fixture(false);f.loft.placement.x=17;f.loft.placement.rotation_z=90;f.loft.placement.absolute_rotation_z=90;
  for(auto& owned:f.loft.loft.sketches) {
   auto sketch=s::Sketch::from_serialized(owned.sketch_serialized);sketch.circles.clear();sketch.points.clear();
   const auto a=sketch.add_segment(0,0,20,0),b=sketch.add_segment(20,0,20,10);
   static_cast<void>(sketch.add_segment(20,10,0,10));static_cast<void>(sketch.add_segment(0,10,0,0));
   const auto axis=sketch.add_segment(-5,-5,25,-5);sketch.set_geometry_construction(axis,true);
   static_cast<void>(sketch.add_corner_fillet(a,b,2));owned.sketch_serialized=sketch.serialized();
  }
  d::resolve_loft_sketches(f.loft,{});
  const auto before=f.loft.loft;
  const auto preview=d::loft_definition_mesh(f.part,f.loft);
  check(preview.edges.size()==15,"Loft preview omitted a rounded curve or included a helper/reference");
  std::size_t index{};
  for(const auto& owned:f.loft.loft.sketches) {
   const auto evaluated=d::general_surface_display_sketch(f.loft,owned).evaluated_profile_sketch().viewer_mesh();
   for(const auto& edge:evaluated.edges) {
    if(edge.construction)continue;
    const auto& key=edge.reference.semantic_key;
    if(!key.starts_with("segment:")&&!key.starts_with("arc:"))continue;
    const auto& actual=preview.edges.at(index++);
    check(actual.points==edge.points&&!actual.construction&&actual.reference.owner_id==f.loft.id&&
      actual.reference.semantic_key=="loft:input:from:"+edge.reference.owner_id+":"+key,
      "Loft preview differs from its evaluated curve/parent-local frame");
   }
  }
  check(index==preview.edges.size()&&f.loft.loft==before,"Loft preview changed the authored Sketches");
  std::cout<<"Loft empty creation and exact rounded native profile preview without construction curves passed"<<std::endl;
 }
 {
  auto f=fixture(false);
  auto& source=f.loft.loft.sketches.front();auto sketch=s::Sketch::from_serialized(source.sketch_serialized);
  const auto radius=sketch.create_circle_radius_dimension(sketch.circles.front().id);sketch.apply_dimension(radius);source.sketch_serialized=sketch.serialized();
  const auto sketch_owner=sketch.id;const auto radius_key="dimension:"+radius.id;
  f.part.insert_history_entry(d::PartHistoryKind::Feature,f.loft.id);f.part.history.push_back(f.loft);f.part.synchronize_dimension_identifiers();
  const auto owner=f.loft.loft.sketches.front().sketch_feature->id;
  const auto key=std::string("parameter:placement:x");
  const auto id=f.part.dimension_identifiers.identifier(owner,key);
  check(!id.empty(),"Zero owned placement has no stable identifier");
  const auto radius_id=f.part.dimension_identifiers.identifier(sketch_owner,radius_key);
  check(!radius_id.empty(),"Owned Sketch dimension lacks a stable identifier");
  for(double value:{11.,12.}) {
    check(w::assign_driving_dimension(f.part,{"dimension",sketch_owner,radius_key},value),"Owned Sketch dimension cannot be edited repeatedly");
    near(s::Sketch::from_serialized(f.part.history.back().loft.sketches.front().sketch_serialized).circles.front().radius,value);
    check(f.part.dimension_identifiers.identifier(sketch_owner,radius_key)==radius_id,"Owned Sketch edit changed its identifier");
  }
  auto inputs=w::relation_dimensions(f.part);
  check(inputs.contains(id)&&inputs.at(id).input.writable,"Zero owned placement is unavailable to Relations");
  near(std::get<double>(inputs.at(id).input.value.data),0);
  check(w::assign_driving_dimension(f.part,{"dimension",owner,key},3),"Owned placement assignment failed");
  near(f.part.history.back().loft.sketches.front().sketch_feature->placement.x,3);
  f.part.relations=id+" = 4";w::apply_relation_dimensions(f.part,{});f.part.relations.clear();
  near(f.part.history.back().loft.sketches.front().sketch_feature->placement.x,4);
  d::FamilyTable table;table.columns={"Position"};table.bindings["Position"]={"dimension",owner,key};
  d::FamilyInstance row;row.name="Loft variant";row.values["Position"]="3";
  auto variant=f.part;w::apply_family_variant(variant,table,row);
  near(variant.history.back().loft.sketches.front().sketch_feature->placement.x,3);
  check(variant.dimension_identifiers.identifier(owner,key)==id,"Family variant renumbered its dimension");
  check(w::assign_driving_dimension(f.part,{"dimension",owner,key},3),"Relations placement cannot be edited again");

  near(s::Sketch::from_serialized(f.part.history.back().loft.sketches.front().sketch_serialized).resolved_origin.x,3);
  const auto offset=std::string("parameter:profile_offset");
  check(!f.part.dimension_identifiers.identifier(owner,offset).empty(),"Zero owned profile offset lacks an identifier");
  check(w::assign_driving_dimension(f.part,{"dimension",owner,offset},2),"Owned profile offset assignment failed");
  near(s::Sketch::from_serialized(f.part.history.back().loft.sketches.front().sketch_serialized).resolved_origin.z,2);
  const auto annotations=d::loft_definition_mesh(f.part,f.part.history.back());
  check(std::ranges::any_of(annotations.dimensions,[&](const auto& value){return value.reference.owner_id==owner&&value.reference.semantic_key==offset&&value.value==2;}),"Owned profile offset annotation missing");
  auto reopened=d::PartDocument::from_serialized(f.part.serialized());
  check(reopened.dimension_identifiers.identifier(owner,key)==id,"Reopening renumbered owned placement");
  w::Workspace live;live.add_part(reopened);const auto references=w::family_references(live,reopened.document_id);
  check(std::ranges::any_of(references,[&](const auto& ref){return ref.name==id&&ref.binding.owner_id==owner;}),"Owned placement unavailable to Family Table");
  auto& owned=*reopened.history.back().loft.sketches.front().sketch_feature;
  owned.placement.value_locks.insert("x");
  check(!w::relation_dimensions(reopened).at(id).input.writable,"Locked owned placement is writable");
  const auto before=reopened.serialized();bool rejected=false;
  try{static_cast<void>(w::assign_driving_dimension(reopened,{"dimension",owner,key},8));}catch(const std::invalid_argument&){rejected=true;}
  check(rejected&&reopened.serialized()==before,"Locked assignment changed the document");
  auto parent=d::create_loft();parent.id=f.loft.id;parent.feature_id=f.loft.feature_id;parent.container_origin=f.loft.container_origin;parent.loft.sketches.clear();parent.loft.sketches.push_back(d::create_loft_sketch(parent));f.part.history.back()=parent;f.part.synchronize_dimension_identifiers();
  const auto attached=parent.loft.sketches.front().sketch_feature->id;
  const auto reference_key=std::string("parameter:placement:reference_offset:0");
  const auto reference_id=f.part.dimension_identifiers.identifier(attached,reference_key);
  check(!reference_id.empty()&&w::relation_dimensions(f.part).at(reference_id).input.writable,"Zero reference offset unavailable");
  check(w::assign_driving_dimension(f.part,{"dimension",attached,reference_key},4),"Reference offset assignment failed");
  near(f.part.history.back().loft.sketches.front().sketch_feature->placement.y,4);
 }

 {
  auto f=fixture(false);f.loft.loft.profiles.clear();f.loft.loft.sketches.clear();
  f.loft.placement.x=11;f.loft.placement.y=7;f.loft.placement.z=3;f.loft.placement.rotation_z=90;f.loft.placement.absolute_rotation_z=90;
  for(unsigned i=0;i<3;++i) {
   auto owned=d::create_loft_sketch(f.loft);
   check(d::placement_references_use_whole_origin(owned.sketch_feature->placement.references),"New Loft Sketch lacks complete parent Origin attachment");
   for(auto& ref:owned.sketch_feature->placement.references) {
    check(ref.owner_id==f.loft.container_origin.id,"Default Sketch uses Body Origin instead of Loft Origin");
    if(!ref.orientation_only)ref.offset=ref.semantic_key=="origin:plane:yz"?4:ref.semantic_key=="origin:plane:xz"?2:5.+5*i;
   }
   auto sketch=s::Sketch::from_serialized(owned.sketch_serialized);static_cast<void>(sketch.add_circle(0,0,10));owned.sketch_serialized=sketch.serialized();
   f.loft.loft.profiles.push_back(owned.sketch_feature->id);f.loft.loft.sketches.push_back(std::move(owned));
  }
  auto& body=f.part.body_history.bodies().front();
  // Change only the existing Body definition through its ordinary graph value.
  auto graph=f.part.body_history;auto placed_body=*graph.find(body.scope.id);
  placed_body.scope.placement.x=100;placed_body.scope.placement.y=-20;placed_body.scope.placement.rotation_z=90;placed_body.scope.placement.absolute_rotation_z=90;graph.update_body(std::move(placed_body));f.part.set_body_history(graph);
  d::resolve_loft_sketches(f.loft,{});
  for(unsigned i=0;i<3;++i) {
   const auto sketch=s::Sketch::from_serialized(f.loft.loft.sketches[i].sketch_serialized);
   near(sketch.resolved_origin.x,4);near(sketch.resolved_origin.y,2);near(sketch.resolved_origin.z,5.+5*i);
   const auto display=d::general_surface_display_sketch(f.loft,f.loft.loft.sketches[i]);
   near(display.resolved_origin.x,9);near(display.resolved_origin.y,11);near(display.resolved_origin.z,8.+5*i);
  }
  w::Workspace live;live.add_part(f.part,w::calculate_part_with_resolved_references(kernel,f.part));live.activate(f.part.document_id);
  check(w::commit_loft(live,kernel,f.part.document_id,f.loft),"Parent-Origin Loft failed to commit");
  auto* state=live.open_part(f.part.document_id);auto result=state->session.calculated_boundaries().back();kernel.ensure_body_properties(result);
  near(result.volume,1000*std::numbers::pi);near(result.volume_integrals->centroid.x,89);near(result.volume_integrals->centroid.y,-11);near(result.volume_integrals->centroid.z,13);
  const auto file=std::filesystem::temp_directory_path()/"zima-loft-parent-origin.prtz";
  state->session.document().save(file,state->session.calculated_boundaries());auto reopened=d::PartDocument::load(file);std::filesystem::remove(file);
  check(reopened.find_container(f.loft.id)->loft==state->session.document().find_container(f.loft.id)->loft,"Parent-Origin references changed on reopen");
  for(double offset:{3.,4.}) {
   auto changed=*state->session.document().find_container(f.loft.id);
   for(auto& owned:changed.loft.sketches)for(auto& ref:owned.sketch_feature->placement.references)
    if(!ref.orientation_only&&ref.semantic_key=="origin:plane:xz")ref.offset=offset;
   check(w::commit_loft(live,kernel,f.part.document_id,changed),"Parent-local correction did not commit");
   auto edited=state->session.calculated_boundaries().back();kernel.ensure_body_properties(edited);
   near(edited.volume,1000*std::numbers::pi);near(edited.volume_integrals->centroid.x,89);near(edited.volume_integrals->centroid.y,-9-offset);
  }
  check(w::step_part_document_history(live,f.part.document_id,false),"Parent-local correction Undo failed");
  auto undone=state->session.calculated_boundaries().back();kernel.ensure_body_properties(undone);near(undone.volume_integrals->centroid.y,-12);
  check(w::step_part_document_history(live,f.part.document_id,true),"Parent-local correction Redo failed");
  auto redone=state->session.calculated_boundaries().back();kernel.ensure_body_properties(redone);near(redone.volume_integrals->centroid.y,-13);
  std::cout<<"Loft parent-local defaults, rotated/transformed Body and Loft, native reopen and Undo/Redo passed"<<std::endl;
 }

 {
  auto f=fixture(false);auto parent=f.loft;parent.loft.profiles.clear();parent.loft.sketches.clear();
  parent.placement.x=11;parent.placement.y=7;parent.placement.z=3;parent.placement.rotation_z=90;parent.placement.absolute_rotation_z=90;
  auto anchor=d::PartDocument::create_sketch_container();anchor.placement.x=20;anchor.placement.y=30;anchor.placement.z=13;
  d::PartDocument external;external.history={anchor};auto geometry=external.history_origin_reference_geometry_before({});
  for(unsigned i=0;i<3;++i) {
   auto owned=d::create_loft_sketch(parent);for(auto& ref:owned.sketch_feature->placement.references)ref.owner_id=anchor.container_origin.id;
   auto sketch=s::Sketch::from_serialized(owned.sketch_serialized);sketch.plane_offset=5.*i;static_cast<void>(sketch.add_circle(0,0,10));owned.sketch_serialized=sketch.serialized();
   parent.loft.profiles.push_back(owned.sketch_feature->id);parent.loft.sketches.push_back(std::move(owned));
  }
  d::resolve_loft_sketches(parent,geometry);const auto valid=parent.loft;
  for(const auto& owned:valid.sketches)check(owned.sketch_feature->placement.reference_valid,"Other Origin did not resolve into Loft frame");
  d::resolve_loft_sketches(parent,{});
  for(unsigned i=0;i<3;++i) {
   const auto& frozen=parent.loft.sketches[i];check(!frozen.sketch_feature->placement.reference_valid,"Missing anchor was not marked");
   check(frozen.sketch_serialized==valid.sketches[i].sketch_serialized&&frozen.sketch_feature->placement.references==valid.sketches[i].sketch_feature->placement.references,"Missing anchor changed the stored local Sketch or reference identity");
  }
  const auto evaluate=[&](const auto& value,double x){auto body=kernel.evaluate_history({{value.id,d::loft_request(f.part,value)}}).back();kernel.ensure_body_properties(body);near(body.volume,1000*std::numbers::pi);near(body.volume_integrals->centroid.x,x);near(body.volume_integrals->centroid.y,30);near(body.volume_integrals->centroid.z,18);};
  evaluate(parent,20);
  auto shifted=parent;shifted.placement.x+=10;d::resolve_loft_sketches(shifted,{});evaluate(shifted,30);
  f.part.history.push_back(parent);auto graph=f.part.body_history;graph.insert({d::PartHistoryKind::Feature,parent.id});f.part.set_body_history(graph);
  const auto file=std::filesystem::temp_directory_path()/"zima-loft-missing-anchor.prtz";f.part.save(file);auto reopened=d::PartDocument::load(file);std::filesystem::remove(file);
  auto& restored=*reopened.find_container(parent.id);d::resolve_loft_sketches(restored,{});evaluate(restored,20);
  external.history.front().placement.x=25;geometry=external.history_origin_reference_geometry_before({});d::resolve_loft_sketches(restored,geometry);
  for(const auto& owned:restored.loft.sketches)check(owned.sketch_feature->placement.reference_valid,"Restored anchor did not reconnect");evaluate(restored,25);
  std::cout<<"Loft foreign Origin conversion, missing-anchor local snapshot, native reopen and reference restoration passed"<<std::endl;
 }
 for(bool guided:{false,true}) {
  auto f=fixture(guided);const auto source=f.part.serialized().at("sketches");
  const auto baseline=f.part.serialized();
  w::Workspace live;live.add_part(f.part,w::calculate_part_with_resolved_references(kernel,f.part));live.activate(f.part.document_id);
  check(w::commit_loft(live,kernel,f.part.document_id,f.loft),"Loft creation did not commit");
  auto* state=live.open_part(f.part.document_id);
  auto body=state->session.calculated_boundaries().back();
  for(const auto& [owner,error]:body.calculation_errors)std::cerr<<error<<std::endl;
  check(body.calculation_errors.empty(),"Loft document calculation failed");kernel.ensure_body_properties(body);near(body.volume,1000*std::numbers::pi);
  check(state->session.document().serialized().at("sketches")==source,"Loft modified read-only source Sketches");
  const auto revision=state->session.revision(),generation=state->session.data_generation();
  const auto archive=state->session.calculated_boundaries().back().kernel_shape;
  const auto provenance=state->session.calculated_boundaries().back().calculation_provenance;
  check(!w::commit_loft(live,kernel,f.part.document_id,*state->session.document().find_container(f.loft.id)),"Unchanged Loft OK committed");
  check(state->session.revision()==revision&&state->session.data_generation()==generation,"Unchanged Loft OK changed session state");
  check(archive.shares_with(state->session.calculated_boundaries().back().kernel_shape)&&provenance==state->session.calculated_boundaries().back().calculation_provenance,"Unchanged Loft OK replaced calculated geometry");
  const auto dependency_graph=w::part_history_dependency_graph(state->session.document());
  check(state->session.document().sketches.empty(),"Owned Loft leaked Sketches to document root");
  for(const auto& value:state->session.document().find_container(f.loft.id)->loft.sketches)
   for(const auto& alias:{value.sketch_feature->id,value.sketch_feature->feature_id,value.sketch_feature->container_origin.id,value.sketch_feature->feature.sketch_id})
    check(dependency_graph.owners.at(alias)==f.loft.id,"Owned Sketch reference escaped its Loft dependency owner");
  const auto file=std::filesystem::temp_directory_path()/(guided?"zima-loft-guided.prtz":"zima-loft-profiles.prtz");
  state->session.document().save(file,state->session.calculated_boundaries());std::vector<k::BodyResult> cached;
  const auto reopened=d::PartDocument::load(file,&cached);std::filesystem::remove(file);
  check(reopened.find_container(f.loft.id)->loft==f.loft.loft&&!cached.empty(),"Native Loft roundtrip lost inputs");
  check(k::history_fingerprint(reopened.kernel_operations(),reopened.kernel_operations().size())==
        k::history_fingerprint(state->session.document().kernel_operations(),state->session.document().kernel_operations().size()),"Loft reopening changed calculation identity");
  check(w::step_part_document_history(live,f.part.document_id,false),"Loft Undo failed");
  check(!state->session.document().find_container(f.loft.id),"Loft Undo retained the feature");
  check(w::step_part_document_history(live,f.part.document_id,true),"Loft Redo failed");
  check(state->session.document().find_container(f.loft.id)->loft==f.loft.loft,"Loft Redo changed source identities");
  auto moved=*state->session.document().find_container(f.loft.id);moved.placement.x=3;moved.placement.z=7;
  check(w::commit_loft(live,kernel,f.part.document_id,moved),"Loft placement did not commit");
  auto placed=state->session.calculated_boundaries().back();kernel.ensure_body_properties(placed);
  near(placed.volume_integrals->centroid.x,3);near(placed.volume_integrals->centroid.z,12);
  check(state->session.document().serialized().at("sketches")==source,"Placed Loft modified its source Sketches");
  const auto owned_sketch_id=moved.loft.sketches.front().sketch_feature->feature.sketch_id;
  const auto displayed=w::document_sketch(live,f.part.document_id,owned_sketch_id);
  near(displayed.resolved_origin.x,3);near(displayed.resolved_origin.z,7);
  const auto frames=d::part_annotation_frames(state->session.document());
  near(frames.at({owned_sketch_id,{}}).origin.x,3);near(frames.at({owned_sketch_id,{}}).origin.z,7);
  const auto sources=w::sketch_external_reference_source_geometry(state->session.document(),state->session.calculated_boundaries());
  check(std::ranges::any_of(sources.edges,[&](const auto& edge){return edge.reference.owner_id==owned_sketch_id;}),"Owned Loft Sketch is missing from exact source geometry");
  const auto allowed=w::sketch_external_reference_source_owners(state->session.document(),owned_sketch_id);
  check(!allowed.contains(f.loft.id)&&!allowed.contains(owned_sketch_id),"Owned Loft Sketch can reference its own result");
  const auto input=w::part_sketch_body_reference_geometry(state->session,displayed);
  check(std::ranges::none_of(input.edges,[&](const auto& edge){return edge.reference.owner_id==f.loft.id;}),"Owned Loft Sketch body reference escaped rollback");
  const auto before=state->session.document().serialized();auto invalid=moved;invalid.loft.profiles[1]=invalid.loft.profiles[0];
  bool rejected=false;try{static_cast<void>(w::commit_loft(live,kernel,f.part.document_id,invalid));}catch(const std::exception&){rejected=true;}
  check(rejected&&before==state->session.document().serialized(),"Invalid Loft edit changed the document");
  for(double radius:{11.,12.}) {
   auto changed=*state->session.document().find_container(f.loft.id);
   for(auto& value:changed.loft.sketches) {
    auto sketch=s::Sketch::from_serialized(value.sketch_serialized);
    if(!sketch.circles.empty())sketch.circles.front().radius=radius;
    else for(auto& point:sketch.points)point.x=point.x<0?-radius:radius;
    value.sketch_serialized=sketch.serialized();
   }
   const auto old_shape=state->session.calculated_boundaries().back().kernel_shape;
   const auto old_fingerprint=k::history_fingerprint(state->session.document().kernel_operations(),state->session.document().kernel_operations().size());
   check(w::commit_loft(live,kernel,f.part.document_id,changed),"Owned Sketch edit did not commit");
   auto current=state->session.calculated_boundaries().back();kernel.ensure_body_properties(current);
   near(current.volume,10*radius*radius*std::numbers::pi);
   check(!current.kernel_shape.shares_with(old_shape)&&old_fingerprint!=k::history_fingerprint(state->session.document().kernel_operations(),state->session.document().kernel_operations().size()),"Owned Sketch edit reused stale Loft geometry");
   for(std::size_t i=0;i<changed.loft.sketches.size();++i)
    check(state->session.document().find_container(f.loft.id)->loft.sketches[i].sketch_feature->id==changed.loft.sketches[i].sketch_feature->id,"Owned Sketch edit changed persistent identity");
  }
  check(w::step_part_document_history(live,f.part.document_id,false),"Owned Sketch Undo failed");
  auto undo_body=state->session.calculated_boundaries().back();kernel.ensure_body_properties(undo_body);near(undo_body.volume,1210*std::numbers::pi);
  check(w::step_part_document_history(live,f.part.document_id,true),"Owned Sketch Redo failed");
  auto redo_body=state->session.calculated_boundaries().back();kernel.ensure_body_properties(redo_body);near(redo_body.volume,1440*std::numbers::pi);
  std::cout<<"Loft document "<<guided<<" persistence, dependency, no-op, Undo/Redo and placement passed"<<std::endl;
 }
 return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}

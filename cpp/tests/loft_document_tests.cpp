#include <zima/document/loft.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/object_annotation_frames.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/workspace/loft_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/part_transactions.hpp>
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

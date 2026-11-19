#include <zima/document/loft.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/sketch_placement.hpp>
#include <zima/document/placement_reference_assignment.hpp>
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
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <filesystem>
#include <fstream>
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
void verify_numbered_profiles(k::OcctKernel& kernel) {
 auto f=fixture(false);f.loft.loft.profiles.resize(2);f.loft.loft.sketches.resize(2);
 for(unsigned i=0;i<2;++i) {
  auto& owned=f.loft.loft.sketches[i];auto sketch=s::Sketch::create_default();
  sketch.id=owned.sketch_feature->feature.sketch_id;sketch.owner_container_id=owned.sketch_feature->id;
  sketch.plane_reference_owner_id=owned.sketch_feature->container_origin.id;sketch.plane_auto=false;
  owned.sketch_feature->placement.z=20.*i;
  if(i==0)static_cast<void>(sketch.add_rectangle(-10,-10,10,10));
  else {
   const auto circle=sketch.add_circle(0,0,10);
   for(unsigned j=0;j<4;++j) {
    const double angle=(j+.5)*std::numbers::pi/2;
    const auto point=sketch.add_point(10*std::cos(angle),10*std::sin(angle));
    static_cast<void>(sketch.add_point_on_circle_constraint(point,circle));
   }
  }
  owned.sketch_serialized=sketch.serialized();
 }
 d::resolve_loft_sketches(f.loft,{});
 const auto second=f.loft.loft.profiles.back();
 const auto original=s::Sketch::from_serialized(f.loft.loft.sketches.back().sketch_serialized);
 const auto order=d::sweep3d_profile_correspondence(original);
 f.loft.loft.correspondence_starts[second]=order.point_ids[2];
 const auto request=d::loft_request(f.part,f.loft);
 check(request.match_profile_vertices&&request.profiles[1].outer_vertex_source_ids.front()==order.point_ids[2],"Loft discarded authored profile start");
 const auto anchors=d::loft_profile_anchor_mesh(f.loft,2);
 check(anchors.points.size()==8&&d::loft_profile_anchor_mesh(f.loft,1).points.size()==4,"Loft profile anchor visibility ignores preceding boundary");
 for(const auto& point:anchors.points)check(!point.label.empty()&&point.reference.semantic_key.starts_with("point:"),"Loft anchor has no native point identity or number");
 w::Workspace live;live.add_part(f.part,w::calculate_part_with_resolved_references(kernel,f.part));live.activate(f.part.document_id);
 check(w::commit_loft(live,kernel,f.part.document_id,f.loft),"Numbered square/circle Loft failed");
 auto* state=live.open_part(f.part.document_id);
 const auto verify_connections=[&](const d::HistoryContainer& feature,const k::BodyResult& body) {
  const auto points=d::loft_profile_anchor_mesh(feature,2).points;
  for(unsigned i=0;i<4;++i) {
   const auto key="loft:longitudinal:from:loft:point:from:"+feature.loft.profiles.front()+":"+points[i].reference.semantic_key.substr(6);
   const auto edge=std::ranges::find_if(body.mesh.original_references.edges,[&](const auto& edge){return edge.reference.semantic_key==key;});
   check(edge!=body.mesh.original_references.edges.end()&&edge->points.size()>1,"Loft correspondence has no authored longitudinal edge");
   const auto distance=[](auto a,auto b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);};
   check((distance(edge->points.front(),points[i].position)<1e-4&&distance(edge->points.back(),points[4+i].position)<1e-4)||
         (distance(edge->points.back(),points[i].position)<1e-4&&distance(edge->points.front(),points[4+i].position)<1e-4),"Loft did not connect equal numbered points");
  }
 };
 verify_connections(f.loft,state->session.calculated_boundaries().back());
 auto guided=request;
 for(unsigned i:{0u,2u}) {
  k::ExtrusionRequest guide;guide.direction={0,1,0};guide.open_profile_end_id="end";
  guide.outer_profile=k::ExtrusionRequest::CurvedProfile{{k::ExtrusionRequest::LineCurve{anchors.points[i].position,anchors.points[4+i].position}}};
  guide.outer_edge_source_ids={"guide-edge"};guide.outer_vertex_source_ids={"start"};
  guided.guides.push_back(guide);guided.guide_owners.push_back("guide-"+std::to_string(i));
 }
 for(unsigned guide_count:{1u,2u})for(bool curved:{false,true})for(unsigned orientation=0;orientation<(1u<<guide_count);++orientation) {
  auto network=guided;
  std::cout<<"Guide case "<<guide_count<<" curved "<<curved<<" orientation "<<orientation<<std::endl;
  network.guides.resize(guide_count);network.guide_owners.resize(guide_count);
  const auto point=[](auto p){return gp_Pnt(p.x,p.y,p.z);};
  std::vector<occ::handle<Geom_TrimmedCurve>> arcs;
  for(unsigned i=0;i<guide_count;++i) {
   auto& profile=std::get<k::ExtrusionRequest::CurvedProfile>(network.guides[i].outer_profile);
   auto line=std::get<k::ExtrusionRequest::LineCurve>(profile.curves.front());
   if(orientation&(1u<<i))std::swap(line.start,line.end);
   if(curved) {
    const k::Vec3 middle{(line.start.x+line.end.x)*.65,(line.start.y+line.end.y)*.65,10};
    profile.curves.front()=k::ExtrusionRequest::ArcCurve{line.start,middle,line.end};
    arcs.push_back(GC_MakeArcOfCircle(point(line.start),point(middle),point(line.end)).Value());
   }else profile.curves.front()=line;
  }
  auto body=kernel.evaluate_history({{f.loft.id,network}}).back();
  check(body.calculation_errors.empty(),"Numbered square/circle side guides failed");
  verify_connections(f.loft,body);
  const auto shape=k::read_calculated_body_shape(body);
  check(BRepCheck_Analyzer(shape).IsValid(),"Mixed guided Loft is not a valid B-Rep");
  TopoDS_Compound skin;BRep_Builder builder;builder.MakeCompound(skin);
  for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())builder.Add(skin,it.Current());
  const auto on_skin=[&](gp_Pnt p) {
   BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(),skin);
   if(!distance.IsDone()||distance.Value()>=1e-6)throw std::runtime_error("Mixed guided Loft boundary: curved="+std::to_string(curved)+" orientation="+std::to_string(orientation)+" distance="+std::to_string(distance.Value())+" point="+std::to_string(p.X())+","+std::to_string(p.Y())+","+std::to_string(p.Z()));
  };
  for(unsigned j=0;j<=32;++j) {
   const double t=j/32.;on_skin(gp_Pnt(10*std::cos(t*2*std::numbers::pi),10*std::sin(t*2*std::numbers::pi),20));
   for(unsigned i=0;i<guide_count;++i) {
    if(curved)on_skin(arcs[i]->Value(arcs[i]->FirstParameter()+t*(arcs[i]->LastParameter()-arcs[i]->FirstParameter())));
    else {const auto a=anchors.points[i*2].position,b=anchors.points[4+i*2].position;
     on_skin(gp_Pnt(a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z)));}
   }
   on_skin(gp_Pnt(-10+20*t,-10,0));on_skin(gp_Pnt(10,-10+20*t,0));
   on_skin(gp_Pnt(10-20*t,10,0));on_skin(gp_Pnt(-10,10-20*t,0));
  }
 }
 std::cout<<"Numbered square/circle: twelve one/two straight/curved guide cases, native endpoints and 2640 boundary samples passed"<<std::endl;
 auto unequal=f.loft;unequal.loft.correspondence_starts.clear();
 auto triangle=original;triangle.circles.clear();triangle.points.clear();triangle.constraints.clear();
 static_cast<void>(triangle.add_segment(-10,-10,10,-10));static_cast<void>(triangle.add_segment(10,-10,0,10));static_cast<void>(triangle.add_segment(0,10,-10,-10));
 unequal.loft.sketches.back().sketch_serialized=triangle.serialized();
 const auto automatic=d::loft_request(f.part,unequal);
 check(!automatic.match_profile_vertices&&kernel.evaluate_history({{unequal.id,automatic}}).back().calculation_errors.empty(),"Automatic unequal polygon correspondence regressed");
 const auto saved=f.loft.loft;auto changed=f.loft;
 changed.loft.correspondence_starts[second]=order.point_ids[1];
 check(w::commit_loft(live,kernel,f.part.document_id,changed),"Loft start point edit failed");
 verify_connections(changed,state->session.calculated_boundaries().back());
 check(w::step_part_document_history(live,f.part.document_id,false)&&state->session.document().find_container(f.loft.id)->loft==saved,"Loft correspondence Undo lost definition");
 check(w::step_part_document_history(live,f.part.document_id,true),"Loft correspondence Redo failed");
 const auto path=std::filesystem::temp_directory_path()/("zima-numbered-loft-"+f.loft.id+".prtz");
 state->session.document().save(path,state->session.calculated_boundaries());std::vector<k::BodyResult> cache;
 const auto reopened=d::PartDocument::load(path,&cache);std::filesystem::remove(path);
 check(reopened.find_container(f.loft.id)->loft==changed.loft&&!cache.empty(),"Loft correspondence native reopen failed");
 verify_connections(*reopened.find_container(f.loft.id),cache.back());
 auto broken=changed;broken.loft.correspondence_starts[second]="removed-native-point";
 check(w::commit_loft(live,kernel,f.part.document_id,broken,w::LoftFailurePolicy::KeepDefinition),"Broken correspondence definition was not retained");
 check(state->session.calculated_boundaries().back().calculation_errors.contains(f.loft.id),"Broken correspondence silently remapped");
 std::cout<<"Numbered square/circle Loft: actual 1-to-1 edges, native anchors, start edit, Undo/Redo, reopen and broken identity passed"<<std::endl;
}
void verify_owned_profile_references() {
 auto f=fixture(false);f.loft.loft.sketches.resize(2);f.loft.loft.profiles.resize(2);
 for(auto& owned:f.loft.loft.sketches) {
  auto sketch=s::Sketch::from_serialized(owned.sketch_serialized);sketch.circles.clear();sketch.points.clear();
  static_cast<void>(sketch.add_rectangle(-10,-10,10,10));owned.sketch_serialized=sketch.serialized();
 }
 f.loft.placement.x=13;f.loft.placement.rotation_z=f.loft.placement.absolute_rotation_z=90;
 auto owned=d::create_general_surface_sketch(f.loft);auto guide=s::Sketch::from_serialized(owned.sketch_serialized);
 s::SketchExternalReference reference;reference.id="loft-guide-profile-point";reference.kind=s::ExternalReferenceKind::Point;
 reference.source_document_id=f.part.document_id;
 d::resolve_loft_sketches(f.loft,{});const auto anchors=d::loft_profile_anchor_mesh(f.loft,2);
 reference.source_owner_id=anchors.points.front().reference.owner_id;reference.source_semantic_key=anchors.points.front().reference.semantic_key;
 reference.cached_points={{0,0}};guide.add_external_reference(reference);owned.sketch_serialized=guide.serialized();
 auto other=d::create_general_surface_sketch(f.loft);
 f.loft.loft.guides.push_back(other.sketch_feature->id);f.loft.loft.sketches.push_back(other);
 f.loft.loft.guides.push_back(owned.sketch_feature->id);f.loft.loft.sketches.push_back(owned);
 d::resolve_loft_sketches(f.loft,{});f.part.history={f.loft};auto graph=f.part.body_history;
 graph.insert({d::PartHistoryKind::Feature,f.loft.id});f.part.set_body_history(graph);
 const auto first=s::Sketch::from_serialized(f.loft.loft.sketches.front().sketch_serialized);
 const auto source_id=first.id;
 const auto allowed=w::sketch_external_reference_source_owners(f.part,guide.id);
 check(allowed.contains(source_id)&&!allowed.contains(guide.id)&&!allowed.contains(f.loft.id),"Owned guide source boundary lost its preceding profile or admitted a cycle");
 check(!w::sketch_external_reference_source_owners(f.part,first.id).contains(guide.id),"Profile can reference its downstream guide");
 for(double shift:{0.,7.,-3.}) {
  auto& parent=f.part.history.front();parent.loft.sketches.front().sketch_feature->placement.x=shift;
  d::resolve_loft_sketches(parent,{});const auto source_before=parent.loft.sketches.front().sketch_serialized;
  static_cast<void>(w::refresh_sketch_external_references(f.part,{}));
  const auto current=d::general_surface_display_sketch(parent,parent.loft.sketches.back());
  const auto anchor=d::loft_profile_anchor_mesh(parent,2).points.front();const auto expected=current.local_point(anchor.position);
  check(f.part.reference_errors.empty()&&!current.external_references.front().broken,"Owned guide point failed on native refresh");
  near(current.external_references.front().cached_points.front()[0],expected[0]);near(current.external_references.front().cached_points.front()[1],expected[1]);
  check(parent.loft.sketches.front().sketch_serialized==source_before,"Guide refresh changed read-only profile");
  check(!w::refresh_sketch_external_references(f.part,{}),"Unchanged guide point refresh drifted");
  f.part=d::PartDocument::from_serialized(f.part.serialized());
 }
 guide=s::Sketch::from_serialized(f.part.history.front().loft.sketches.back().sketch_serialized);
 unsigned cases{},edits{},drags{};
 for(auto kind:{s::DimensionKind::Distance,s::DimensionKind::DistanceX,s::DimensionKind::DistanceY})
 for(bool order:{false,true})for(double side:{-1.,1.})for(bool driving:{false,true})for(bool locked:{false,true})for(bool fixed:{false,true}) {
  auto sketch=guide;const auto source=sketch.external_references;const auto q=source.front().cached_points.front();
  const auto point=sketch.add_point(q[0]+4*side,q[1]+3*side);sketch.set_point_fixed(point,fixed);
  auto dimension=sketch.create_point_dimension(order?reference.id:point,order?point:reference.id,kind);dimension.driving=driving;dimension.locked=locked;
  const auto before=sketch.serialized();bool accepted=true;try{sketch.apply_dimension(dimension);}catch(const std::exception&){accepted=false;}
  if((!driving&&locked)||(driving&&fixed)){check(!accepted&&sketch.serialized()==before,"Invalid guide point dimension was not atomic");++cases;continue;}
  check(accepted,"Valid guide point dimension was rejected");
  const auto verify=[&] {
   const auto* p=sketch.find_point(point);const double dx=(p->x-q[0])*(order?1:-1),dy=(p->y-q[1])*(order?1:-1);
   near(kind==s::DimensionKind::Distance?std::hypot(dx,dy):kind==s::DimensionKind::DistanceX?dx:dy,sketch.dimensions.front().value);
   check(sketch.external_references==source,"Dimension/drag modified guide source identity or geometry");
  };verify();
  for(double factor:{1.05,.97,1.02}) {
   const auto prior=sketch.serialized();const auto p=*sketch.find_point(point);
   check(sketch.set_dimension_value(dimension.id,s::dimension_display_value(sketch.dimensions.front(),sketch.plane)*factor),"Guide dimension edit rejected");
   if(!driving)check(sketch.serialized()==prior,"Reference dimension moved guide geometry");
   else check(std::hypot(sketch.find_point(point)->x-p.x,sketch.find_point(point)->y-p.y)>.001,"Driving edit did not move available DOF");
   verify();sketch=s::Sketch::from_serialized(sketch.serialized());++edits;
  }
  for(unsigned i=0;i<3;++i) {
   const auto p=*sketch.find_point(point);const auto prior=sketch.serialized();double x=p.x+.2,y=p.y+.3;
   if(driving&&locked) {
    if(kind==s::DimensionKind::DistanceX)x=p.x;else if(kind==s::DimensionKind::DistanceY)y=p.y;
    else {const double dx=p.x-q[0],dy=p.y-q[1];x=q[0]+dx*std::cos(.05)-dy*std::sin(.05);y=q[1]+dx*std::sin(.05)+dy*std::cos(.05);}
   }
   const bool moved=sketch.move_point(point,x,y);
   if(fixed)check(!moved&&sketch.serialized()==prior,"Fixed guide point escaped on drag");
   else {check(moved&&std::hypot(sketch.find_point(point)->x-p.x,sketch.find_point(point)->y-p.y)>.001,"Guide point did not move along available DOF");++drags;}
   verify();
  }++cases;
 }
 std::cout<<"Owned Loft guide: preceding native point identity, three source moves, projection and reopen; "<<cases<<" dimension states, "<<edits<<" edits, "<<drags<<" DOF drags passed"<<std::endl;
}
int main(int argc,char** argv){try {
 k::OcctKernel kernel;
 if(argc==5&&(std::string_view(argv[1])=="--verify-native-copy"||std::string_view(argv[1])=="--verify-loft-prefix-copy")) {
  const auto input=std::filesystem::u8path(argv[2]),output=std::filesystem::u8path(argv[3]);
  check(std::filesystem::absolute(input).lexically_normal()!=std::filesystem::absolute(output).lexically_normal(),"Copy verification must preserve its input");
  const auto part=d::PartDocument::load(input);const auto definition=part.serialized();
  auto operations=part.kernel_operations();const auto complete_count=operations.size();
  if(std::string_view(argv[1])=="--verify-loft-prefix-copy") {
   std::size_t last{};for(std::size_t i=0;i<operations.size();++i)
    if(std::holds_alternative<k::LoftRequest>(operations[i].primitive))last=i+1;
   check(last>0,"Copy contains no Loft");operations.resize(last);
  }
  auto calculated=kernel.evaluate_history_recovering(operations);
  nlohmann::json errors=nlohmann::json::object();std::size_t lofts{};
  for(std::size_t i=0;i<operations.size();++i) {
   const auto& body=calculated.at(i);
   for(const auto& [owner,error]:body.calculation_errors)errors[owner]=error;
   if(std::holds_alternative<k::LoftRequest>(operations[i].primitive)) {
    check(body.calculation_errors.empty(),"Converted Loft calculation failed");++lofts;
   }
  }
  check(lofts>0,"Copy contains no Loft");
  const bool complete=calculated.size()==complete_count;
  // A partial calculation must never masquerade as the full document cache.
  // Preserve the entire authored copy, leaving its disposable cache absent.
  if(complete)part.save(output,calculated);else part.save(output);
  std::vector<k::BodyResult> reopened;
  const auto readback=d::PartDocument::load(output,&reopened);
  check(readback.serialized()==definition&&reopened.size()==(complete?calculated.size():0),"Copy readback changed authored history");
  if(complete)for(std::size_t i=0;i<calculated.size();++i)check(d::serialize_body_result(calculated[i])==d::serialize_body_result(reopened[i]),"Copy readback changed calculated geometry/references");
  const auto readback_operations=readback.kernel_operations();
  check(k::history_fingerprint(readback_operations,operations.size())==k::history_fingerprint(operations,operations.size()),"Native readback changed the verified Loft calculation inputs");
  std::ofstream report(std::filesystem::u8path(argv[4]));
  report<<nlohmann::json{{"authored_definition_unchanged",true},{"native_readback_exact",true},{"calculated_cache_saved",complete},{"calculated_boundaries",calculated.size()},{"total_operations",complete_count},{"verified_lofts",lofts},{"full_model_valid",complete&&errors.empty()},{"calculation_errors",errors}}.dump(2)<<'\n';
  return report?0:1;
 }
 verify_owned_profile_references();
 verify_numbered_profiles(kernel);
 for(unsigned incomplete=0;incomplete<4;++incomplete) {
  auto f=fixture(incomplete>=2);const auto complete=f.loft;
  if(incomplete<2) {
   f.loft.loft.profiles.resize(incomplete);f.loft.loft.sketches.resize(incomplete);
  }else if(incomplete==2) {
   f.loft.loft.guides.resize(1);f.loft.loft.sketches.resize(4);
  }else {
   f.loft.loft.sketches.back().sketch_feature->placement.x=50;
   d::resolve_loft_sketches(f.loft,{});
  }
  w::Workspace live;live.add_part(f.part,w::calculate_part_with_resolved_references(kernel,f.part));live.activate(f.part.document_id);
  auto* state=live.open_part(f.part.document_id);const auto before=state->session.document().serialized();
  bool rejected=false;try{static_cast<void>(w::commit_loft(live,kernel,f.part.document_id,f.loft));}catch(const std::exception&){rejected=true;}
  check(rejected&&state->session.document().serialized()==before,"Strict Loft calculation retained a failed draft");
  check(w::commit_loft(live,kernel,f.part.document_id,f.loft,w::LoftFailurePolicy::KeepDefinition),"Incomplete Loft was not retained");
  check(state->session.calculated_boundaries().back().calculation_errors.contains(f.loft.id),"Incomplete Loft has no calculation diagnostic");
  check(state->session.calculated_boundaries().back().mesh.triangles.empty(),"Incomplete Loft published invalid material");
  const auto retained=state->session.document().find_container(f.loft.id)->loft;
  const auto revision=state->session.revision(),generation=state->session.data_generation();
  check(!w::commit_loft(live,kernel,f.part.document_id,*state->session.document().find_container(f.loft.id),w::LoftFailurePolicy::KeepDefinition),"Unchanged incomplete Loft committed");
  check(state->session.revision()==revision&&state->session.data_generation()==generation,"Unchanged incomplete Loft recalculated");
  const auto file=std::filesystem::temp_directory_path()/("zima-loft-incomplete-"+std::to_string(incomplete)+".prtz");
  state->session.document().save(file,state->session.calculated_boundaries());std::vector<k::BodyResult> cached;
  const auto reopened=d::PartDocument::load(file,&cached);std::filesystem::remove(file);
  check(reopened.find_container(f.loft.id)->loft==retained&&!cached.empty()&&cached.back().calculation_errors.contains(f.loft.id),"Incomplete Loft reopen lost inputs or diagnostic");
  check(w::step_part_document_history(live,f.part.document_id,false)&&!state->session.document().find_container(f.loft.id),"Incomplete Loft Undo failed");
  check(w::step_part_document_history(live,f.part.document_id,true)&&state->session.document().find_container(f.loft.id)->loft==retained,"Incomplete Loft Redo failed");
  check(w::commit_loft(live,kernel,f.part.document_id,complete,w::LoftFailurePolicy::KeepDefinition),"Incomplete Loft could not be repaired");
  check(state->session.calculated_boundaries().back().calculation_errors.empty(),"Repaired Loft retained its diagnostic");
  auto invalid=complete;invalid.loft.sketches.front().sketch_parent_id="foreign-parent";
  const auto valid=state->session.document().serialized();rejected=false;
  try{static_cast<void>(w::commit_loft(live,kernel,f.part.document_id,invalid,w::LoftFailurePolicy::KeepDefinition));}catch(const std::exception&){rejected=true;}
  check(rejected&&state->session.document().serialized()==valid,"KeepDefinition bypassed owned Sketch validation");
 }
 std::cout<<"Incomplete/failed Loft: strict rejection, retained definition, diagnostics, no-op, native reopen, Undo/Redo and repair passed"<<std::endl;
 for(const auto& [key,normal]:std::array{
     std::pair{"xy",k::Vec3{0,0,1}},std::pair{"xz",k::Vec3{0,1,0}},std::pair{"yz",k::Vec3{1,0,0}}})for(double side:{-1.,1.}) {
  auto f=fixture(false);f.loft.loft.profiles.clear();f.loft.loft.sketches.clear();
  const auto geometry=f.part.body_origin_reference_geometry();
  d::ConstructionReference reference{{},f.part.body_history.active_body_id()+":origin",std::string("origin:plane:")+key,0.,true};
  d::prepare_profile_plane_reference(0,reference,{});reference.measured_offset=0.;
  std::vector<d::ConstructionReference> position,orientation;std::array<bool,3> locks{};
  check(d::assign_placement_reference({position,orientation,locks},true,0,reference).error==d::PlacementReferenceError::None,"Loft first XY-plane assignment failed");
  f.loft.placement.references=d::combined_placement_references(position,orientation);
  check(d::resolve_placement(f.loft.placement,geometry),"Loft first plane did not resolve");
  for(unsigned i=0;i<3;++i){
   auto owned=d::create_loft_sketch(f.loft);auto sketch=s::Sketch::from_serialized(owned.sketch_serialized);
   sketch.plane_offset=side*5*i;static_cast<void>(sketch.add_circle(0,0,10));owned.sketch_serialized=sketch.serialized();
   f.loft.loft.profiles.push_back(owned.sketch_feature->id);f.loft.loft.sketches.push_back(std::move(owned));
  }
  d::resolve_loft_sketches(f.loft,geometry);
  for(const auto& owned:f.loft.loft.sketches){
   const auto sketch=d::general_surface_display_sketch(f.loft,owned);
   check(sketch.plane==s::SketchPlane::XY&&sketch.plane_auto,"Inherited Loft Sketch did not use automatic XY");
   near(std::abs(sketch.resolved_normal.x*normal.x+sketch.resolved_normal.y*normal.y+sketch.resolved_normal.z*normal.z),1);
  }
  w::Workspace live;live.add_part(f.part,w::calculate_part_with_resolved_references(kernel,f.part));live.activate(f.part.document_id);
  check(w::commit_loft(live,kernel,f.part.document_id,f.loft),"Loft with picked first XY plane did not calculate");
  const auto* state=live.open_part(f.part.document_id);auto result=state->session.calculated_boundaries().back();
  check(result.calculation_errors.empty(),"Loft first-plane calculation failed");kernel.ensure_body_properties(result);near(result.volume,1000*std::numbers::pi);
  const auto persisted=state->session.document().find_container(f.loft.id)->loft;
  const auto reopened=d::PartDocument::from_serialized(state->session.document().serialized());
  check(reopened.find_container(f.loft.id)->loft==persisted,"First-plane Loft frame changed on reopen");
 }
 std::cout<<"Six first-plane Loft calculations passed: XY/XZ/YZ supports, inherited automatic XY, both offset sides and reopen"<<std::endl;
 for(const auto plane:{s::SketchPlane::XY,s::SketchPlane::XZ,s::SketchPlane::YZ})for(double side:{-1.,1.})for(bool rotated:{false,true}){
  auto f=fixture(false);f.loft.loft.profiles.clear();f.loft.loft.sketches.clear();
  f.loft.placement.x=17;f.loft.placement.y=23;f.loft.placement.z=11;
  f.loft.placement.rotation_z=f.loft.placement.absolute_rotation_z=rotated?90.:0.;
  const k::Vec3 local_normal=plane==s::SketchPlane::XY?k::Vec3{0,0,1}:plane==s::SketchPlane::XZ?k::Vec3{0,1,0}:k::Vec3{1,0,0};
  const k::Vec3 normal=rotated?k::Vec3{-local_normal.y,local_normal.x,local_normal.z}:local_normal;
  for(unsigned i=0;i<3;++i){
   auto owned=d::create_loft_sketch(f.loft);auto sketch=s::Sketch::from_serialized(owned.sketch_serialized);
   sketch.plane=plane;sketch.plane_auto=false;sketch.plane_offset=side*5*i;
   static_cast<void>(sketch.add_circle(0,0,10));owned.sketch_serialized=sketch.serialized();
   f.loft.loft.profiles.push_back(owned.sketch_feature->id);f.loft.loft.sketches.push_back(std::move(owned));
  }
  d::resolve_loft_sketches(f.loft,{});
  for(unsigned i=0;i<3;++i){
   const auto& owned=f.loft.loft.sketches[i];const auto sketch=d::general_surface_display_sketch(f.loft,owned);
   check(sketch.plane==plane&&!sketch.plane_auto,"Loft replaced the manual work-plane selection");
   near(sketch.resolved_normal.x,normal.x);near(sketch.resolved_normal.y,normal.y);near(sketch.resolved_normal.z,normal.z);
   near(sketch.resolved_origin.x,17+normal.x*side*5*i);near(sketch.resolved_origin.y,23+normal.y*side*5*i);near(sketch.resolved_origin.z,11+normal.z*side*5*i);
   for(const auto& ref:owned.sketch_feature->placement.references)check(ref.owner_id==f.loft.container_origin.id,"Manual plane changed parent attachment identity");
  }
  w::Workspace live;live.add_part(f.part,w::calculate_part_with_resolved_references(kernel,f.part));live.activate(f.part.document_id);
  check(w::commit_loft(live,kernel,f.part.document_id,f.loft),"Manual-plane Loft did not calculate");
  auto* state=live.open_part(f.part.document_id);auto result=state->session.calculated_boundaries().back();
  check(result.calculation_errors.empty(),"Manual-plane Loft calculation failed");kernel.ensure_body_properties(result);
  near(result.volume,1000*std::numbers::pi);
  near(result.volume_integrals->centroid.x,17+normal.x*side*5);near(result.volume_integrals->centroid.y,23+normal.y*side*5);near(result.volume_integrals->centroid.z,11+normal.z*side*5);
  const auto persisted=state->session.document().find_container(f.loft.id)->loft;
  const auto file=std::filesystem::temp_directory_path()/"zima-loft-manual-plane.prtz";
  state->session.document().save(file,state->session.calculated_boundaries());auto reopened=d::PartDocument::load(file);std::filesystem::remove(file);
  check(reopened.find_container(f.loft.id)->loft==persisted,"Manual-plane Loft lost its native frame or references on reopen");
  check(w::step_part_document_history(live,f.part.document_id,false)&&w::step_part_document_history(live,f.part.document_id,true),"Manual-plane Loft Undo/Redo failed");
  check(state->session.document().find_container(f.loft.id)->loft==persisted,"Manual-plane Loft Undo/Redo changed frames");
 }
 std::cout<<"Twelve manual XY/XZ/YZ Loft calculations passed: both offset sides, parent rotation, profile frames, volume/centroid, native reopen and Undo/Redo"<<std::endl;
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

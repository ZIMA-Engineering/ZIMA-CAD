#include <zima/document/part_document.hpp>
#include <zima/document/document_session.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>
using namespace zima;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void near(kernel::Vec3 a,kernel::Vec3 b){check(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<1e-8,"Axis endpoint moved incorrectly");}
int main(){try{
 auto part=document::PartDocument::create_default();
 auto axis=document::PartDocument::create_construction(document::ConstructionKind::Axis);
 axis.origin={11,12,13};axis.direction={.6,.8,0};axis.display_size=80;axis.axis_reverse_length=25;
 part.constructions={axis};
 document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Axis"));graph.insert({document::PartHistoryKind::Construction,axis.id});part.set_body_history(graph);
 std::vector<kernel::VertexReference> identities;
 for(auto mode:{document::AxisExtentMode::OneSide,document::AxisExtentMode::TwoSides,document::AxisExtentMode::Symmetric}){
  part.constructions[0].axis_extent_mode=mode;
  const auto mesh=part.construction_viewer_mesh();
  const auto& refs=mesh.original_references;
  const double first=mode==document::AxisExtentMode::OneSide?0:mode==document::AxisExtentMode::TwoSides?-25:-40;
  const double last=mode==document::AxisExtentMode::Symmetric?40:80;
  std::vector<kernel::VertexReference> current;
  for(const auto& point:refs.points)if(point.reference.semantic_key.starts_with("axis:point:")){
   current.push_back(point.reference);near(point.position,axis.axis_point(point.reference.semantic_key.ends_with("start")?first:last));
   auto follower=document::PartDocument::create_construction(document::ConstructionKind::Point);
   follower.references={{"",point.reference.owner_id,point.reference.semantic_key}};
   check(document::resolve_construction(follower,refs),"Cannot bind to Axis endpoint");near(follower.origin,point.position);
  }
  check(current.size()==2,"Axis must expose two endpoints");if(identities.empty())identities=current;else check(current==identities,"Extent mode changed endpoint identity");
  const auto shown=std::ranges::find_if(mesh.axes,[&](const auto& a){return a.reference.owner_id==axis.entity_id;});
  check(shown!=mesh.axes.end()&&std::abs(shown->display_length-(last-first))<1e-8,"Axis extent rendering is wrong");near(shown->point,axis.axis_point((first+last)*.5));
  const auto restored=document::PartDocument::from_serialized(part.serialized());
  check(restored.constructions[0].axis_extent_mode==mode&&restored.constructions[0].axis_reverse_length==25,"Native reopen lost Axis extent");
  auto changed=part;changed.constructions[0].display_size=120;
  document::DocumentSession session(part);session.commit(changed,{});check(session.undo()&&session.redo(),"Axis extent Undo/Redo failed");
  check(session.document().constructions[0].display_size==120,"Axis Redo lost length");
 }
 // End references are independent of placement and keep semantic endpoint identities.
 auto limited=document::PartDocument::create_construction(document::ConstructionKind::Axis);
 limited.direction={0,0,1};limited.axis_extent_mode=document::AxisExtentMode::TwoSides;
 auto front=document::PartDocument::create_construction(document::ConstructionKind::Plane);
 front.base_plane=document::LocalDatumPlane::XY;front.base_plane_auto=false;
 front.origin=front.entity_origin={0,0,60};front.direction={0,0,1};
 auto back=front;back.id="back";back.entity_id="back:entity";back.origin=back.entity_origin={0,0,-20};
 document::PartDocument source;source.constructions={front,back};
 auto geometry=source.construction_viewer_mesh().original_references;
 limited.axis_ends[0].up_to=true;limited.axis_ends[0].target={"",front.entity_id,"plane"};
 limited.axis_ends[1].up_to=true;limited.axis_ends[1].target={"",back.entity_id,"plane"};
 check(document::resolve_axis_extents(limited,geometry),"Up-to planes did not resolve");
 check(limited.axis_limits()==std::pair{-20.,60.},"Up-to plane limits wrong");near(limited.origin,{});
 const auto cached=limited.axis_limits();
 check(!document::resolve_axis_extents(limited,{}),"Missing target accepted");check(limited.axis_limits()==cached,"Missing target erased last valid bounds");
 limited.axis_ends[1].target=limited.axis_ends[0].target;
 check(!document::resolve_axis_extents(limited,geometry),"Wrong-side target accepted");
 limited.axis_extent_mode=document::AxisExtentMode::Symmetric;
 check(document::resolve_axis_extents(limited,geometry)&&limited.axis_limits()==std::pair{-60.,60.},"Symmetric up-to failed");
 source.constructions[0].origin=source.constructions[0].entity_origin={0,0,85};
 geometry=source.construction_viewer_mesh().original_references;
 check(document::resolve_axis_extents(limited,geometry)&&limited.axis_limits()==std::pair{-85.,85.},"Moved target not followed");
 const auto native=document::deserialize_construction_objects(document::serialize_construction_objects({limited}));
 check(native.front().axis_ends==limited.axis_ends,"Native Up-to lost target/cache");
 limited.direction={1,0,0};check(!document::resolve_axis_extents(limited,geometry),"Parallel target accepted");
 limited.direction={0,0,1};limited.axis_ends[0].target={"",limited.entity_id,"plane"};
 check(!document::resolve_axis_extents(limited,geometry),"Self-target accepted");
 kernel::ViewerReferenceGeometry targets;
 targets.points.push_back({{20,30,42},{"source","point",""}});
 limited.axis_ends[0].target={"","source","point"};
 check(document::resolve_axis_extents(limited,targets)&&limited.axis_limits()==std::pair{-42.,42.},"Point end station failed");
 targets.vertices={{-10,-10,30},{10,-10,30},{0,10,30}};targets.triangles={0,1,2};targets.triangle_references={{"face","source:face",""}};
 limited.axis_ends[0].target={"","face","source:face"};
 check(document::resolve_axis_extents(limited,targets)&&limited.axis_limits()==std::pair{-30.,30.},"Captured face intersection failed");
 limited.origin={100,0,0};check(!document::resolve_axis_extents(limited,targets),"Ray outside trimmed face accepted");
 std::cout<<"Axis extents, endpoints, binding, ancestry, native persistence and Undo/Redo passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

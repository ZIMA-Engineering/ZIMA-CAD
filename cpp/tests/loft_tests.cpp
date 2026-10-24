#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>
#include <zima/kernel/surface_results.hpp>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Builder.hxx>
#include <numbers>
#include <chrono>
#include <iostream>
#include <set>
#include <algorithm>
using namespace zima::kernel;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double actual,double expected,double tolerance=1e-5){
 if(std::abs(actual-expected)>tolerance)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
LoftRequest cylinder(bool guided,bool cone=false) {
 LoftRequest r;r.region_id="region";r.tolerance=1e-7;
 for(unsigned i=0;i<3;++i){
  ExtrusionRequest p;p.direction={0,0,1};p.outer_boundary_id="boundary"+std::to_string(i);
  p.outer_edge_source_ids={"circle"+std::to_string(i)};
  p.outer_profile=ExtrusionRequest::CircleProfile{{0,0,5.*i},10.+(cone?5.*i:0.)};
  r.profiles.push_back(p);r.profile_owners.push_back("profile"+std::to_string(i));
 }
 if(guided)for(unsigned i=0;i<3;++i){
  const auto angle=2*std::numbers::pi*i/3;const auto c=std::cos(angle),s=std::sin(angle);
  ExtrusionRequest g;g.direction={-s,c,0};g.open_profile_end_id="end";
  g.outer_profile=ExtrusionRequest::CurvedProfile{{ExtrusionRequest::LineCurve{{10*c,10*s,0},{(cone?20:10)*c,(cone?20:10)*s,10}}}};
  r.guides.push_back(g);r.guide_owners.push_back("guide"+std::to_string(i));
 }
 return r;
}
std::set<std::string> identities(const BodyResult& body) {
 std::set<std::string> result;
 for(const auto& face:body.mesh.original_references.triangle_references)if(face.owner_id=="loft")result.insert("f:"+face.semantic_key);
 for(const auto& edge:body.mesh.original_references.edges)if(edge.reference.owner_id=="loft")result.insert("e:"+edge.reference.semantic_key);
 for(const auto& vertex:body.mesh.original_references.points)if(vertex.reference.owner_id=="loft")result.insert("v:"+vertex.reference.semantic_key);
 return result;
}
LoftRequest rectangle(bool guided,bool open=false) {
 LoftRequest r;r.region_id="rect";r.tolerance=1e-7;r.make_solid=!open;
 for(unsigned i=0;i<3;++i) {
  ExtrusionRequest p;p.direction={0,0,1};p.outer_boundary_id="boundary";
  const double z=5.*i;
  if(open) {
   p.outer_profile=ExtrusionRequest::CurvedProfile{{ExtrusionRequest::LineCurve{{0,0,z},{10,0,z}}}};
   p.outer_edge_source_ids={"line"};p.outer_vertex_source_ids={"first"};p.open_profile_end_id="last";
  }else {
   p.outer_profile=ExtrusionRequest::PolygonProfile{{{0,0,z},{10,0,z},{10,20,z},{0,20,z}}};
   p.outer_edge_source_ids={"bottom","right","top","left"};p.outer_vertex_source_ids={"a","b","c","d"};
  }
  r.profiles.push_back(p);r.profile_owners.push_back("profile"+std::to_string(i));
 }
 if(guided)for(unsigned i=0;i<3;++i) {
  const double x=open?5.*i:(i==1?10.:0.),y=open?0.:(i==2?20.:0.);
  ExtrusionRequest g;g.direction={0,1,0};g.open_profile_end_id="end";
  g.outer_profile=ExtrusionRequest::CurvedProfile{{ExtrusionRequest::LineCurve{{x,y,0},{x,y,10}}}};
  r.guides.push_back(g);r.guide_owners.push_back("guide"+std::to_string(i));
 }
 return r;
}
int main(){try{
 OcctKernel kernel;
 for(bool guided:{false,true})for(bool cone:{false,true}) {
  auto request=cylinder(guided,cone);
  const auto started=std::chrono::steady_clock::now();
  auto body=kernel.evaluate_history({{"loft",request}}).back();
  const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
  for(const auto& error:body.calculation_errors)std::cerr<<error.second<<std::endl;
  require(body.calculation_errors.empty(),"Loft calculation failed");
  require(BRepCheck_Analyzer(read_calculated_body_shape(body)).IsValid(),"Invalid Loft B-Rep");
  kernel.ensure_body_properties(body);
  near(body.volume,cone?7000*std::numbers::pi/3:1000*std::numbers::pi,1e-4);
  const auto keys=identities(body);
  require(!keys.empty(),"Loft native topology has no authored identities");
  OcctKernel fresh;auto rebuilt=fresh.evaluate_history({{"loft",request}}).back();
  require(keys==identities(rebuilt),"Loft cold regeneration changed topology identity");
  const auto edge=std::ranges::find_if(body.mesh.original_references.edges,[](const auto& e){
   return e.reference.owner_id=="loft"&&e.reference.semantic_key.starts_with("loft:edge:from:profile0:");
  });
  require(edge!=body.mesh.original_references.edges.end(),"Loft end boundary has no native reference");
  const auto fillet=kernel.evaluate_history({{"loft",request},{"fillet",FilletRequest{{edge->reference},.25}}}).back();
  require(fillet.calculation_errors.empty(),"Dependent Loft edge Fillet failed");
  const auto fingerprint=history_fingerprint({{"loft",request}},1);
  request.profiles[1].outer_boundary_id+="-changed";
  require(fingerprint!=history_fingerprint({{"loft",request}},1),"Profile identity missing from Loft fingerprint");
  std::cout<<"closed "<<guided<<" cone "<<cone<<" passed; first evaluation ms "<<milliseconds<<std::endl;
 }
 for(bool guided:{false,true})for(bool open:{false,true}) {
  auto request=rectangle(guided,open);
  auto result=kernel.evaluate_history({{"loft",request}}).back();
  for(const auto& error:result.calculation_errors)std::cerr<<error.second<<std::endl;
  require(result.calculation_errors.empty(),"Polygon/open Loft failed");
  kernel.ensure_body_properties(result);
  if(open)near(result.surface_area,100.);else near(result.volume,2000.);
  OcctKernel fresh;
  require(identities(result)==identities(fresh.evaluate_history({{"loft",request}}).back()),"Polygon/open Loft identity changed");
  std::cout<<"rectangle "<<guided<<" open "<<open<<" passed"<<std::endl;
 }
 for(bool guided:{false,true})for(unsigned side=0;side<3;++side) {
  auto request=cylinder(guided);request.thin=true;request.thickness=2;
  request.thickness_side=static_cast<SurfaceThicknessSide>(side);
  auto result=kernel.evaluate_history({{"loft",request}}).back();
  for(const auto& error:result.calculation_errors)std::cerr<<error.second<<std::endl;
  require(result.calculation_errors.empty(),"Thin cylindrical Loft failed");
  kernel.ensure_body_properties(result);
  near(result.volume,(side==0?440:side==1?360:400)*std::numbers::pi,1e-4);
  const auto fingerprint=history_fingerprint({{"loft",request}},1);
  request.thickness_side=static_cast<SurfaceThicknessSide>((side+1)%3);
  require(fingerprint!=history_fingerprint({{"loft",request}},1),"Thin Loft side missing from fingerprint");
  std::cout<<"thin cylinder "<<guided<<" side "<<side<<" passed"<<std::endl;
 }
 std::array<double,3> side_centers{};
 for(unsigned side=0;side<3;++side) {
  auto request=rectangle(true,true);request.make_solid=true;request.thin=true;request.thickness=2;
  request.thickness_side=static_cast<SurfaceThicknessSide>(side);
  auto result=kernel.evaluate_history({{"loft",request}}).back();
  require(result.calculation_errors.empty(),"Thin open-profile Loft failed");
  kernel.ensure_body_properties(result);near(result.volume,200.);
  side_centers[side]=result.volume_integrals->centroid.y;
 }
 near(side_centers[0]*side_centers[1],-1.);near(side_centers[2],0.);
 for(unsigned orientation=0;orientation<4;++orientation) {
  auto request=cylinder(true);request.profiles.erase(request.profiles.begin()+1);request.profile_owners.erase(request.profile_owners.begin()+1);
  request.guides.resize(2);request.guide_owners.resize(2);request.tolerance=.001;
  for(unsigned i=0;i<2;++i) {
   Vec3 start{i==0?13.:-7.,0,0},end{i==0?13.:-7.,0,10};
   if(orientation&(1u<<i))std::swap(start,end);
   request.guides[i].outer_profile=ExtrusionRequest::CurvedProfile{{ExtrusionRequest::LineCurve{start,end}}};
  }
  for(auto& profile:request.profiles)std::get<ExtrusionRequest::CircleProfile>(profile.outer_profile).center.x=3;
  request.thin=true;request.thickness=2;request.thickness_side=SurfaceThicknessSide::Symmetric;
  auto result=kernel.evaluate_history({{"loft",request}}).back();
  require(result.calculation_errors.empty(),"Two-guide oriented Thin Loft failed");
  kernel.ensure_body_properties(result);near(result.volume,400*std::numbers::pi,1e-4);
  std::cout<<"two-profile two-guide Thin orientation "<<orientation<<" passed"<<std::endl;
 }
 for(bool guided:{false,true}) {
  auto ellipse=cylinder(guided);
  for(unsigned i=0;i<3;++i)ellipse.profiles[i].outer_profile=ExtrusionRequest::EllipseProfile{{0,0,5.*i},{1,0,0},10,5};
  if(guided)for(unsigned i=0;i<3;++i) {
   const double angle=2*std::numbers::pi*i/3,x=10*std::cos(angle),y=5*std::sin(angle);
   ellipse.guides[i].outer_profile=ExtrusionRequest::CurvedProfile{{ExtrusionRequest::LineCurve{{x,y,0},{x,y,10}}}};
  }
  auto result=kernel.evaluate_history({{"ellipse-loft",ellipse}}).back();
  require(result.calculation_errors.empty(),"Elliptical Loft failed");kernel.ensure_body_properties(result);near(result.volume,500*std::numbers::pi,1e-4);
  std::cout<<"ellipse "<<guided<<" passed"<<std::endl;
 }
 auto surface=cylinder(true);surface.make_solid=false;
 auto body=kernel.evaluate_history({{"surface",surface}}).back();
 require(body.calculation_errors.empty()&&has_surface_results(body.mesh),"Loft surface classification failed");
 kernel.ensure_body_properties(body);near(body.surface_area,200*std::numbers::pi,1e-4);
 auto closed=cylinder(false);closed.guides.clear();closed.guide_owners={"xz-guide","yz-guide"};
 for(unsigned i=0;i<3;++i)std::get<ExtrusionRequest::CircleProfile>(closed.profiles[i].outer_profile).radius=std::sqrt(100-std::pow(5.*i-5,2));
 for(unsigned i=0;i<2;++i) {
  ExtrusionRequest guide;guide.direction=i==0?Vec3{0,1,0}:Vec3{1,0,0};
  guide.outer_profile=ExtrusionRequest::CircleProfile{{0,0,5},10};closed.guides.push_back(guide);
 }
 // OCCT can report Done for a rational network whose measured profile
 // deviation exceeds tolerance. This network must be rejected, never published.
 bool incompatible_rejected=false;
 try {incompatible_rejected=!kernel.evaluate_history({{"closed-guides",closed}}).back().calculation_errors.empty();}
 catch(const std::exception&){incompatible_rejected=true;}
 require(incompatible_rejected,"Out-of-tolerance rational network was accepted");
 closed=cylinder(false);closed.guide_owners={"xz-guide","yz-guide"};
 for(unsigned i=0;i<2;++i) {
  ExtrusionRequest guide;guide.direction=i==0?Vec3{0,1,0}:Vec3{1,0,0};
  guide.outer_profile=ExtrusionRequest::PolygonProfile{i==0?std::vector<Vec3>{{-10,0,-5},{10,0,-5},{10,0,15},{-10,0,15}}:std::vector<Vec3>{{0,-10,-5},{0,10,-5},{0,10,15},{0,-10,15}}};
  closed.guides.push_back(guide);
 }
 auto closed_body=kernel.evaluate_history({{"closed-guides",closed}}).back();
 for(const auto& error:closed_body.calculation_errors)std::cerr<<error.second<<std::endl;
 require(closed_body.calculation_errors.empty(),"Closed two-branch guide network failed");
 kernel.ensure_body_properties(closed_body);near(closed_body.volume,1000*std::numbers::pi,1e-4);
 const auto closed_shape=read_calculated_body_shape(closed_body);
 TopoDS_Compound boundary;BRep_Builder boundary_builder;boundary_builder.MakeCompound(boundary);
 for(TopExp_Explorer it(closed_shape,TopAbs_FACE);it.More();it.Next())boundary_builder.Add(boundary,it.Current());
 for(unsigned guide=0;guide<2;++guide)for(double z:{0.,2.,5.,8.,10.})for(double side:{-1.,1.}) {
  BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(gp_Pnt(guide==0?10*side:0,guide==0?0:10*side,z)).Vertex(),boundary);
  require(distance.IsDone()&&distance.Value()<1e-6,"Loft discarded a closed guide branch");
 }
 std::cout<<"closed guides: all four branches lie on native boundary"<<std::endl;
 auto invalid=cylinder(true);invalid.guides.resize(1);invalid.guide_owners.resize(1);
 bool rejected=false;
 try {rejected=!kernel.evaluate_history({{"invalid",invalid}}).back().calculation_errors.empty();}
 catch(const std::exception&){rejected=true;}
 require(rejected,"Single guide accepted without a compatible network");
 std::cout<<"Loft finite native kernel matrix passed"<<std::endl;
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}

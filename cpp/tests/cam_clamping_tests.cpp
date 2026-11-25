#include "../modules/cam/src/native_simulation.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>

namespace {
using namespace zima::cam;
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void close(double a,double b){if(std::abs(a-b)>1e-5)std::cerr<<a<<" != "<<b<<'\n';check(std::abs(a-b)<1e-5,"Clamping geometric equation failed");}
TopoDS_Wire rectangle(double y,double x0,double x1,double z0,double z1){
    BRepBuilderAPI_MakePolygon p;p.Add({x0,y,z0});p.Add({x1,y,z0});p.Add({x1,y,z1});p.Add({x0,y,z1});p.Close();return p.Wire();
}
TopoDS_Shape contact(double y,double height=10,bool hole=false){
    BRepBuilderAPI_MakeFace f(rectangle(y,-5,15,0,height));
    if(hole)f.Add(TopoDS::Wire(rectangle(y,-2,12,1,24).Reversed()));return f.Face();
}
std::string bytes(const TopoDS_Shape& s){std::ostringstream out;BRepTools::Write(s,out);return out.str();}
CompiledProgram path(double y=0,double z=5,bool rapid=false){
    CompiledProgram p;p.motions.push_back({"block","operation","tool",rapid?MotionKind::Rapid:MotionKind::Linear,{0,y,z},{10,y,z}});return p;
}
bool hit(const native::SimulationResult& r,CollisionKind k,const std::string& id){return std::ranges::any_of(r.collisions,[&](const auto& c){return c.kind==k&&c.obstacle_id==id&&c.block_id=="block";});}
std::size_t warnings(const native::SimulationResult& r,ClampingWarningKind k){return std::ranges::count(r.warnings,k,&ClampingWarning::kind);}
}
int main(){try{
    const std::vector<Tool> tools{{"tool","Tool",{{-5,1},{0,1}},{{0,2},{10,2}}}};
    const auto stock=BRepPrimAPI_MakeBox(gp_Pnt(-5,-5,0),20,10,10).Shape();
    const auto target=BRepPrimAPI_MakeBox(gp_Pnt(-5,-5,0),20,10,3).Shape();
    native::Clamping c{"jaw",contact(-5),{0,-5,0},{0,-1,0},{0,0,8},{0,0,1}};
    const auto original=bytes(c.contact),original_stock=bytes(stock);
    auto r=native::simulate(path(),tools,stock,target,{},1e-7,{}, {c});
    close(r.removed_mm3,5*(20+std::numbers::pi));check(r.collisions.empty(),"Outward side collided with internal path");
    check(warnings(r,ClampingWarningKind::BelowJawTop)==1&&warnings(r,ClampingWarningKind::ContactLoss)==0,"Upper boundary warning wrong");
    close(r.warnings[0].removed_mm3,3*(20+std::numbers::pi));close(r.contacts[0].initial_mm2,200);close(r.contacts[0].remaining_mm2,200);
    check(r.warnings[0].operation_id=="operation"&&r.warnings[0].block_id=="block"&&r.warnings[0].tool_id=="tool"&&r.warnings[0].clamping_id=="jaw","Warning identities lost");
    auto inward=c;inward.id="inward";inward.outward_normal.y=1;
    auto reversed=native::simulate(path(),tools,stock,target,{},1e-7,{}, {inward});
    check(hit(reversed,CollisionKind::CuttingClamping,"inward"),"Opposite outward side ignored");
    close(reversed.removed_mm3,r.removed_mm3);
    for(double height:{4.,5.,12.}){
        auto upper=c;upper.upper_origin.z=height;const auto q=native::simulate(path(),tools,stock,target,{},1e-7,{}, {upper});
        check(warnings(q,ClampingWarningKind::BelowJawTop)==(height>5?1:0),"Above/on/below jaw-top classification wrong");
        if(height>5)close(q.warnings[0].removed_mm3,r.removed_mm3);
    }
    auto upper=c;upper.upper_normal.z=-1;
    auto q=native::simulate(path(),tools,stock,target,{},1e-7,{}, {upper});close(q.warnings[0].removed_mm3,2*(20+std::numbers::pi));
    q=native::simulate(path(),tools,r.remaining,target,{},1e-7,{}, {c});check(q.warnings.empty()&&q.counts.stock_cuts==0,"Repeated empty pass warned about grip loss");
    q=native::simulate(path(0,30),tools,stock,target,{},1e-7,{}, {c});check(q.warnings.empty(),"Air cut warned below a clamp");
    auto loss=native::simulate(path(-4.5),tools,stock,target,{},1e-7,{}, {c});
    check(warnings(loss,ClampingWarningKind::ContactLoss)==1&&hit(loss,CollisionKind::CuttingClamping,"jaw"),"Contact loss/exclusion collision not distinguished");
    close(loss.contacts[0].remaining_mm2,200-5*(10+std::sqrt(3.)));
    q=native::simulate(path(-4.5),tools,loss.remaining,target,{},1e-7,{}, {c});
    check(q.warnings.empty()&&hit(q,CollisionKind::CuttingClamping,"jaw"),"Stock removal shrank fixed clamping exclusion");
    auto tall=c;tall.id="tall";tall.contact=contact(-5,25);
    q=native::simulate(path(-4.5,5,true),tools,stock,target,{},1e-7,{}, {tall});
    check(hit(q,CollisionKind::CuttingClamping,"tall")&&hit(q,CollisionKind::NonCuttingClamping,"tall")&&q.warnings.empty(),"Rapid cutter/shank checks or no-removal rule failed");close(q.removed_mm3,0);
    auto perforated=c;perforated.contact=contact(0,25,true);perforated.contact_origin.y=0;perforated.outward_normal.y=1;
    q=native::simulate(path(),tools,stock,target,{},1e-7,{}, {perforated});
    check(!hit(q,CollisionKind::CuttingClamping,"jaw")&&!hit(q,CollisionKind::NonCuttingClamping,"jaw"),"Hole in clamping contact was filled");
    q=native::simulate(path(),tools,stock,target,{},1e-7,{}, {c,inward});
    check(q.contacts.size()==2&&q.warnings.size()==2&&hit(q,CollisionKind::CuttingClamping,"inward")&&!hit(q,CollisionKind::CuttingClamping,"jaw"),"Independent clampings merged");
    gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),std::numbers::pi/2);transform.SetTranslationPart({25,-17,3});
    const auto moved_point=[&](Point p){auto v=gp_Pnt(p.x,p.y,p.z).Transformed(transform);return Point{v.X(),v.Y(),v.Z()};};
    auto moved=c;moved.contact=BRepBuilderAPI_Transform(c.contact,transform,true).Shape();moved.contact_origin=moved_point(c.contact_origin);moved.upper_origin=moved_point(c.upper_origin);moved.outward_normal={1,0,0};
    auto motion=path();motion.motions[0].start=moved_point(motion.motions[0].start);motion.motions[0].end=moved_point(motion.motions[0].end);
    q=native::simulate(motion,tools,BRepBuilderAPI_Transform(stock,transform,true).Shape(),BRepBuilderAPI_Transform(target,transform,true).Shape(),{},1e-7,{}, {moved});
    close(q.removed_mm3,r.removed_mm3);close(q.warnings[0].removed_mm3,r.warnings[0].removed_mm3);check(q.collisions.empty(),"Translated/rotated clamping changed side");
    check(bytes(c.contact)==original&&bytes(stock)==original_stock,"Clamping simulation mutated input shapes");
    check(r.counts.swept_volumes==2&&loss.counts.swept_volumes==2,"Clamping checks recalculated tool sweeps");
    std::cout<<"CAM clamping: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"CAM clamping after "<<checks<<": "<<e.what()<<'\n';return 1;}}

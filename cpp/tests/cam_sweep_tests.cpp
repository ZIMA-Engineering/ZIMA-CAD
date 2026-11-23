#include "../modules/cam/src/native_sweep.hpp"
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace zima::cam;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
bool contains(const TopoDS_Shape& shape,Point point) {
    for(TopExp_Explorer i(shape,TopAbs_SOLID);i.More();i.Next()) {
        BRepClass3d_SolidClassifier classifier(i.Current(),gp_Pnt(point.x,point.y,point.z),1e-7);
        if(classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON)return true;
    }
    return false;
}
void volume_is(const TopoDS_Shape& shape,double expected) {
    check(BRepCheck_Analyzer(shape).IsValid(),"Invalid swept solid");
    const auto actual=native::volume(shape);
    if(std::abs(actual-expected)>1e-5*std::max(1.,expected))
        throw std::runtime_error("Sweep volume: actual="+std::to_string(actual)+" expected="+std::to_string(expected));
}
void arc_membership(const TopoDS_Shape& shape,const Motion& motion,double radius0,double radius1) {
    const double path_radius=std::hypot(motion.start.x-motion.center.x,motion.start.y-motion.center.y);
    const double start=std::atan2(motion.start.y-motion.center.y,motion.start.x-motion.center.x);
    for(double z:{1.,9.,19.})for(double x=-16;x<=16;x+=4)for(double y=-16;y<=16;y+=4) {
        double angle=std::atan2(y,x)-start;
        if(motion.sweep_radians<0)angle=-angle;
        angle=std::fmod(angle+4*std::numbers::pi,2*std::numbers::pi);
        double distance=std::abs(std::hypot(x,y)-path_radius);
        if(angle>std::abs(motion.sweep_radians))distance=std::min(
            std::hypot(x+motion.center.x-motion.start.x,y+motion.center.y-motion.start.y),
            std::hypot(x+motion.center.x-motion.end.x,y+motion.center.y-motion.end.y));
        const double radius=radius0+(radius1-radius0)*z/20;
        if(std::abs(distance-radius)<1e-6)continue;
        check(contains(shape,{x+motion.center.x,y+motion.center.y,z+motion.start.z})==(distance<radius),
            "Circular sweep disagrees with independent distance-to-arc classification");
    }
}
}
int main() {
    try {
        Tool tool{"T1","Cutter",{{-20,3},{0,3}},{{0,4},{40,4}}};
        constexpr double pi=std::numbers::pi;
        const double base=pi*9*20;
        volume_is(native::tool_shape(tool,{},true),base);
        for(const Point delta:{Point{10,0,0},Point{-10,0,0},Point{0,10,0},Point{0,0,10},Point{0,0,-10},
                Point{10,0,10},Point{10,4,-7},Point{}}) {
            Motion motion;motion.kind=MotionKind::Linear;motion.end=delta;
            const auto swept=native::swept_tool(tool,motion,true);
            volume_is(swept,base+120*std::hypot(delta.x,delta.y)+pi*9*std::abs(delta.z));
            for(unsigned i=0;i<=10;++i) {
                const auto t=i/10.;
                check(contains(swept,{delta.x*t,delta.y*t,delta.z*t+10}),"Sweep lost an intermediate tool position");
            }
            std::cout<<"Cylinder sweep "<<delta.x<<','<<delta.y<<','<<delta.z<<" passed\n"<<std::flush;
        }
        for(const auto radii:{std::pair{1.,4.},std::pair{4.,1.},std::pair{0.,4.},std::pair{4.,0.5}})
        for(const Point delta:{Point{10,0,0},Point{0,0,10},Point{0,0,-10},Point{10,0,10},Point{10,7,-40},Point{1,0,40}}) {
            tool.cutting={{-20,radii.first},{0,radii.second}};
            Motion motion;motion.kind=MotionKind::Linear;motion.end=delta;
            const auto swept=native::swept_tool(tool,motion,true);
            // Independent convex-body projection formula: V(K+[0,v]) is
            // V(K) plus half the surface integral of |normal dot v|.
            const double a=std::hypot(delta.x,delta.y),c=(radii.second-radii.first)*delta.z/20;
            const double side=std::abs(c)>=a?2*pi*std::abs(c):
                4*(std::sqrt(a*a-c*c)+c*std::asin(c/a));
            const double expected=pi*20*(radii.first*radii.first+radii.first*radii.second+radii.second*radii.second)/3+
                pi*(radii.first*radii.first+radii.second*radii.second)*std::abs(delta.z)/2+
                20*(radii.first+radii.second)*side/4;
            volume_is(swept,expected);
            std::cout<<"Cone sweep "<<delta.x<<','<<delta.y<<','<<delta.z<<" passed\n"<<std::flush;
        }
        tool.cutting={{-20,2},{-15,2},{-15,4},{-10,4},{0,1}};
        Motion stepped;stepped.kind=MotionKind::Linear;stepped.end={7,0,0};
        volume_is(native::swept_tool(tool,stepped,true),pi*(5*4+5*16+10*(16+4+1)/3)+
            2*7*(5*2+5*4+10*(4+1)/2));
        tool.cutting={{-20,3},{0,3}};
        for(bool clockwise:{false,true})for(bool closed:{false,true}) {
            Motion motion;motion.kind=clockwise?MotionKind::ArcClockwise:MotionKind::ArcCounterclockwise;
            motion.start={10,0,0};motion.end=closed?motion.start:Point{0,clockwise?-10.:10.,0};
            motion.sweep_radians=(clockwise?-1:1)*(closed?2*pi:pi/2);
            const auto swept=native::swept_tool(tool,motion,true);
            volume_is(swept,closed?20*4*pi*10*3:20*(2*10*3*pi/2+pi*9));
            check(contains(swept,{std::sqrt(50.),(clockwise?-1:1)*std::sqrt(50.),10}),"Arc sweep lost its midpoint");
            check(!contains(swept,{0,0,10}),"Arc sweep filled its central opening");
            std::cout<<"Arc sweep clockwise="<<clockwise<<" closed="<<closed<<" passed\n"<<std::flush;
        }
        for(const auto radii:{std::pair{1.,4.},std::pair{4.,1.},std::pair{3.,3.}})
        for(double path_radius:{2.,10.})for(double sweep:{pi/2,-pi/2,3*pi/2,-2*pi}) {
            tool.cutting={{-20,radii.first},{0,radii.second}};
            Motion motion;motion.kind=sweep>0?MotionKind::ArcCounterclockwise:MotionKind::ArcClockwise;
            motion.center={7,-11,5};motion.start={7+path_radius,-11,5};
            motion.end={7+path_radius*std::cos(sweep),-11+path_radius*std::sin(sweep),5};
            motion.sweep_radians=sweep;
            const auto swept=native::swept_tool(tool,motion,true);
            check(BRepCheck_Analyzer(swept).IsValid(),"Invalid profiled circular sweep");
            arc_membership(swept,motion,radii.first,radii.second);
            if(path_radius==10&&std::abs(sweep)<=pi/2)volume_is(swept,
                20*path_radius*std::abs(sweep)*(radii.first+radii.second)+
                pi*20*(radii.first*radii.first+radii.first*radii.second+radii.second*radii.second)/3);
            if(std::abs(sweep)==2*pi&&path_radius==10)
                volume_is(swept,4*pi*path_radius*20*(radii.first+radii.second)/2);
            if(std::abs(sweep)==2*pi&&path_radius==2&&radii.first==radii.second)
                volume_is(swept,pi*25*20);
            std::cout<<"Profile arc "<<radii.first<<','<<radii.second<<" path="<<path_radius<<" sweep="<<sweep<<" passed\n"<<std::flush;
        }
        std::cout<<"CAM exact swept-volume checks passed\n";return 0;
    } catch(const Standard_Failure& error) {std::cerr<<"OCCT: "<<error.GetMessageString()<<'\n';return 1;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

#include <zima/sketcher/sketch.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool near(double a,double b){return std::abs(a-b)<1e-7;}
}
int main(){try{
    unsigned cases=0;
    for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(const bool y:{false,true})for(const int support:{0,1,2,3})for(const double sign:{-1.,1.})
    for(const bool driving:{false,true})for(const bool locked:{false,true}) {
        if(!driving&&locked)continue;
        auto s=sketcher::Sketch::create_default();s.plane=plane;s.refresh_default_frame();
        const auto p=s.add_point(sign*12,sign*9);const auto axis=y?"sketch_axis:x":"sketch_axis:y";
        auto d=support==0?s.create_point_dimension("sketch_origin",p,y?sketcher::DimensionKind::DistanceY:sketcher::DimensionKind::DistanceX):
            support==1?s.create_point_dimension(p,"sketch_origin",y?sketcher::DimensionKind::DistanceY:sketcher::DimensionKind::DistanceX):
            support==2?s.create_axis_dimension(p,axis):s.create_point_line_dimension(p,axis);
        d.driving=driving;d.locked=locked;s.apply_dimension(d);
        const auto equation=[&] {
            const auto* point=s.find_point(p);const auto world=s.world_point(point->x,point->y);
            const double actual=plane==sketcher::SketchPlane::XY?(y?world.y:world.x):
                plane==sketcher::SketchPlane::XZ?(y?world.z:world.x):(y?world.z:world.y);
            check(near(sketcher::dimension_display_value(s.dimensions.front(),s.plane),actual),"Dimension disagrees with the named Feature Origin axis");
            check(s.viewer_mesh().dimensions.size()==1&&near(s.viewer_mesh().dimensions.front().value,actual),"Viewer and native parameter disagree");
        };
        equation();
        for(const double input:{150.,-35.,0.,-0.,27.}) {
            const auto before=s.serialized();const bool changed=s.set_dimension_value(d.id,input);
            if(driving)check(changed&&near(sketcher::dimension_display_value(s.dimensions.front(),s.plane),input),"Repeated coordinate edit failed");
            else check(s.serialized()==before,"Reference coordinate drove geometry");
            equation();
        }
        for(const double position:{18.,-25.,31.}) {
            const auto before=s.serialized();const auto old=*s.find_point(p);
            static_cast<void>(s.move_point(p,y?old.x:position,y?position:old.y));
            if(driving&&locked)check(s.serialized()==before,"Locked coordinate escaped while dragged");
            else check(near(y?s.find_point(p)->y:s.find_point(p)->x,position),"Editable coordinate failed successive dragging");
            equation();
        }
        const auto saved=s.serialized();s=sketcher::Sketch::from_serialized(saved);
        check(s.serialized()==saved,"Coordinate save/reopen changed geometry, side or identity");equation();
        if(driving){s.set_point_fixed(p,true);const auto fixed=s.serialized();
            check(!s.set_dimension_value(d.id,1000)&&s.serialized()==fixed,"Impossible coordinate edit partially changed the Sketch");}
        ++cases;
    }
    // An ordinary point at (0,0) has its own identity; it is not the semantic Origin.
    for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ}) {
        auto s=sketcher::Sketch::create_default();s.plane=plane;s.refresh_default_frame();
        const auto a=s.add_point(0,0),b=s.add_point(3,8);
        auto d=s.create_point_dimension(a,b,sketcher::DimensionKind::DistanceY);s.apply_dimension(d);
        check(!sketcher::is_coordinate_dimension(d)&&near(sketcher::dimension_display_value(d,plane),8),"Generic distance became an Origin coordinate");
    }
    std::cout<<cases<<" coordinate frame edit/drag/persistence cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

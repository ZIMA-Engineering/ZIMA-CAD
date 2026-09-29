#include "transition_half.hpp"
#include "transition_pattern.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace zima::research::transition;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
double distance(Vec3 a,Vec3 b){return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);}
void valid_strip(const HalfResult& result) {
    if(!result.valid())throw std::runtime_error("Triangulated transition failure "+std::to_string(static_cast<int>(result.failure)));
    check(!result.faces.empty()&&result.folds.size()+1==result.faces.size(),"Strip is disconnected");
    check(result.maximum_metric_error<1e-7&&result.maximum_planarity_error<1e-7,"Triangulation changed metric or left warped panels");
    check(std::abs(result.folded_area-result.unfolded_area)<1e-7,"Triangulated development changed area");
    for(const auto& face:result.faces)check(face.folded.size()==3||face.folded.size()==4,"Invalid panel boundary");
    for(std::size_t i=0;i<result.folds.size();++i) {
        const auto& fold=result.folds[i];
        for(const auto panel:{i,i+1})for(const auto end:{fold.first,fold.second})
            check(std::ranges::any_of(result.faces[panel].folded,[&](Vec3 p){return distance(p,end)<1e-7;}),"Fold is detached from a neighboring panel");
    }
}
Frame tilted(double x,double y,double z) {
    const double cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    return {{0,0,150},{cz*cy,sz*cy,-sy},{cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx},{cz*sy*cx+sz*sx,sz*sy*cx-cz*sx,cy*cx}};
}
void valid(const HalfResult& result,std::size_t count) {
    if(!result.valid())throw std::runtime_error("Half transition failure "+std::to_string(static_cast<int>(result.failure)));
    check(result.faces.size()==count&&result.folds.size()==count-1,"Panel connectivity count");
    check(result.maximum_metric_error<1e-7&&result.maximum_planarity_error<1e-7,"Distortion or warped faces");
    check(std::abs(result.folded_area-result.unfolded_area)<1e-7,"Area changed");
    std::size_t triangles=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& face=result.faces[i];triangles+=face.folded.size()==3;
        if(i) {
            const auto& previous=result.faces[i-1];
            const auto end_a=previous.folded.size()==3?previous.folded[0]:previous.folded[3];
            const auto flat_a=previous.unfolded.size()==3?previous.unfolded[0]:previous.unfolded[3];
            check(distance(end_a,face.folded[0])<1e-7&&distance(previous.folded[2],face.folded[1])<1e-7,"Spatial seam gap");
            check(distance(flat_a,face.unfolded[0])<1e-7&&distance(previous.unfolded[2],face.unfolded[1])<1e-7,"Unfolded seam gap");
        }
    }
    check(triangles==3,"Missing straight-section panels");
}
int main()try {
    const auto start=std::chrono::steady_clock::now();HalfModel model;
    const auto original=calculate(model);valid(original,11);
    const auto drawing=pattern(original);
    check(std::count_if(drawing.begin(),drawing.end(),[](const PatternLine& line){return line.role==PatternRole::BendAxis;})==6,"Coplanar joins incorrectly marked as bends");
    for(const auto& line:drawing)if(line.role==PatternRole::BendAxis) {
        int matches=0;
        for(const auto& face:original.faces)for(std::size_t i=0;i<face.unfolded.size();++i){const auto a=face.unfolded[i],b=face.unfolded[(i+1)%face.unfolded.size()];if((distance(a,line.first)<1e-7&&distance(b,line.second)<1e-7)||(distance(b,line.first)<1e-7&&distance(a,line.second)<1e-7))++matches;}
        check(matches==2,"Bend axis detached from its unfolded shared edge");
    }
    // Endpoint tangency makes bridge-to-corner boundaries coplanar, not extra bends.
    for(auto index:{0u,4u,5u,9u})check(std::abs(original.folds[index].signed_angle_radians)<1e-8,"Unexpected bend at tangent join");
    model.corner_facets={6,9};valid(calculate(model),18);
    model.first_origin={{12,34,56},{0,1,0},{-1,0,0},{0,0,1}};
    const auto moved=calculate(model);valid(moved,18);
    const auto regenerated=pattern(moved);
    check(std::count_if(regenerated.begin(),regenerated.end(),[](const PatternLine& line){return line.role==PatternRole::BendAxis;})==13,"Axes did not regenerate with asymmetric segmentation");
    model.first_origin={};const auto local=calculate(model);
    check(std::abs(moved.folded_area-local.folded_area)<1e-7,"Root frame changed area");
    for(std::size_t i=0;i<local.faces.size();++i)for(std::size_t j=0;j<local.faces[i].folded.size();++j)
        check(distance(local.faces[i].unfolded[j],moved.faces[i].unfolded[j])<1e-7,"Root frame changed unfolding");
    for(double radius:{10.,30.,55.}){model.corner_radius=radius;valid(calculate(model),18);}
    model.corner_radius=20;model.second_relative.origin={25,-15,210};valid(calculate(model),18);
    model.second_relative.x={std::cos(.3),0,-std::sin(.3)};model.second_relative.z={std::sin(.3),0,std::cos(.3)};
    valid_strip(calculate(model));
    model={};model.corner_radius=80;check(!calculate(model).valid(),"Degenerate side accepted");
    model={};model.corner_facets[1]=1;check(!calculate(model).valid(),"Invalid segment count accepted");
    // Tilt is compatible here because the rectangle top tangent lies at y=R.
    for(double angle:{-.5,.2,.5}) {
        model={};model.second_relative.x={std::cos(angle),0,-std::sin(angle)};model.second_relative.z={std::sin(angle),0,std::cos(angle)};
        valid(calculate(model),11);
    }
    check(mesh(original).triangles.size()==57,"Mesh contains missing panels or artificial solids");
    for(const auto rotation:std::array<Frame,4>{tilted(.15,0,0),tilted(.15,-.2,0),tilted(-.15,.2,0),tilted(.1,.15,.2)}) {
        model={};model.second_relative=rotation;
        const auto half=calculate(model);valid_strip(half);
        if(std::abs(rotation.x.z)<1e-12 && std::abs(rotation.x.y)<1e-12)valid(half,11);
        for(unsigned sides:{2u,3u}) {
            RectangularModel rectangular;rectangular.sides=sides;rectangular.second_relative=rotation;
            const auto result=calculate(rectangular);valid_strip(result);
            check(result.faces.size()>sides&&result.faces.size()<=2*sides,"Twisted rectangular walls did not receive explicit diagonals");
        }
    }
    for(unsigned sides:{2u,3u}) {
        RectangularModel rectangular;rectangular.sides=sides;
        const auto result=calculate(rectangular);valid_strip(result);
        check(result.faces.size()==sides,"Planar rectangular walls were unnecessarily split");
        rectangular.first_origin={{12,34,56},{0,1,0},{-1,0,0},{0,0,1}};
        const auto moved_rectangle=calculate(rectangular);valid_strip(moved_rectangle);
        check(std::abs(result.folded_area-moved_rectangle.folded_area)<1e-7,"Rectangular root frame changed area");
    }
    RectangularModel closed;closed.sides=4;check(!calculate(closed).valid(),"Closed four-wall transition accepted");
    std::cout<<"Half transition geometry and shared unfolding passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

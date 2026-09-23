#include "transition_half.hpp"
#include "transition_pattern.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace zima::research::transition;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
double distance(Vec3 a,Vec3 b){return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);}
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
    const auto invalid=calculate(model);check(!invalid.valid()&&mesh(invalid).vertices.empty(),"Unsupported tilt produced model");
    model={};model.corner_radius=80;check(!calculate(model).valid(),"Degenerate side accepted");
    model={};model.corner_facets[1]=1;check(!calculate(model).valid(),"Invalid segment count accepted");
    // Tilt is compatible here because the rectangle top tangent lies at y=R.
    for(double angle:{-.5,.2,.5}) {
        model={};model.second_relative.x={std::cos(angle),0,-std::sin(angle)};model.second_relative.z={std::sin(angle),0,std::cos(angle)};
        valid(calculate(model),11);
    }
    check(mesh(original).triangles.size()==57,"Mesh contains missing panels or artificial solids");
    std::cout<<"Half transition geometry and shared unfolding passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

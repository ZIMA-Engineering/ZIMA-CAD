#include <zima/kernel/curve_plane_intersection.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace zima::kernel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1e-7){if(!std::isfinite(actual)||std::abs(actual-expected)>tolerance)
    throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
BSplineGeometry bezier(const std::vector<double>& heights,double first=2,double last=5) {
    BSplineGeometry curve;curve.degree=static_cast<unsigned>(heights.size()-1);
    for(std::size_t i=0;i<heights.size();++i){curve.poles.push_back({double(i)/curve.degree,0,heights[i]});curve.weights.push_back(1);}
    curve.knots.insert(curve.knots.end(),heights.size(),first);curve.knots.insert(curve.knots.end(),heights.size(),last);return curve;
}
void contacts() {
    unsigned cases{};
    for(bool rotated:{false,true})for(bool opposite:{false,true}) {
        const auto transform=[&](Vec3 p){return rotated?Vec3{100+p.z,-70+p.y,20-p.x}:Vec3{p.x,p.y,p.z};};
        const auto plane=transform({0,0,0});const Vec3 normal=rotated?Vec3{opposite?-1.:1.,0,0}:Vec3{0,0,opposite?-1.:1.};
        const auto query=[&](BSplineGeometry curve) {
            for(auto& pole:curve.poles)pole=transform(pole);const auto before=curve;
            auto result=intersect_curve_plane({curve,plane,normal,1e-7});check(curve==before,"Intersection query modified its exact source");
            for(const auto& point:result.points) {
                const auto evaluated=bspline_value(curve,(point.parameter-result.first_parameter)/(result.last_parameter-result.first_parameter));
                near(std::hypot(point.position.x-evaluated.x,point.position.y-evaluated.y,point.position.z-evaluated.z),0);
            }
            ++cases;return result;
        };
        const auto cubic=query(bezier({-.08,.14,-.14,.08}));check(cubic.points.size()==3,"Exact cubic did not expose all three plane intersections");
        for(std::size_t i=0;i<3;++i) {near(cubic.points[i].parameter,2+3*(.2+.3*i));check(cubic.points[i].kind==CurvePlaneContactKind::Crossing,"Cubic crossing was misclassified");
            check(cubic.points[i].orientation==(i==1?-1:1)*(opposite?-1:1),"Crossing direction certificate lost plane orientation");}
        auto folded=bezier({-.08,.14,-.14,.08});for(auto& pole:folded.poles)pole.x=0;
        const auto overlapping=query(folded);check(overlapping.points.size()==3,"Distinct curve parameters at equal XYZ were merged");
        for(std::size_t i=0;i<3;++i){near(overlapping.points[i].parameter,2+3*(.2+.3*i));near(std::hypot(
            overlapping.points[i].position.x-plane.x,overlapping.points[i].position.y-plane.y,overlapping.points[i].position.z-plane.z),0);}
        const auto tangent=query(bezier({.16,-.24,.36}));check(tangent.points.size()==1,"A quadratic tangency did not produce exactly one contact");
        near(tangent.points.front().parameter,3.2);check(tangent.points.front().kind==CurvePlaneContactKind::Tangent,"Tangency was misclassified");
        check(tangent.points.front().orientation==(opposite?-1:1),"Tangency support side certificate is incorrect");
        const auto endpoint=query(bezier({0,1}));check(endpoint.points.size()==1&&endpoint.points.front().kind==CurvePlaneContactKind::Endpoint,"First endpoint contact was lost or duplicated");near(endpoint.points.front().parameter,2);
        const auto last=query(bezier({-1,0}));check(last.points.size()==1&&last.points.front().kind==CurvePlaneContactKind::Endpoint,"Last endpoint contact was lost or duplicated");near(last.points.front().parameter,5);
        const auto missing=query(bezier({1,2}));check(missing.points.empty()&&missing.coincident_intervals.empty(),"A disjoint curve invented a contact");
        const auto coplanar=query(bezier({0,0}));check(coplanar.points.empty()&&coplanar.coincident_intervals.size()==1,"A coincident curve interval invented an isolated point");
        near(coplanar.coincident_intervals.front().first,2);near(coplanar.coincident_intervals.front().second,5);
        auto partial=bezier({-1,0,0,1});partial.degree=1;partial.knots={0,0,1,2,3,3};
        const auto interval=query(partial);check(interval.points.empty()&&interval.coincident_intervals.size()==1,"A partial coplanar span was mistaken for isolated endpoint contacts");
        near(interval.coincident_intervals.front().first,1);near(interval.coincident_intervals.front().second,2);
    }
    std::cout<<cases<<" exact curve/plane query cases: crossings, tangency, endpoints, empty/coincident domains and rigid frames passed\n";
}
void rational_and_path() {
    BSplineGeometry circle{2,{{1,0,0},{1,1,0},{0,1,0},{-1,1,0},{-1,0,0},{-1,-1,0},{0,-1,0},{1,-1,0},{1,0,0}},
        {0,0,0,1,1,2,2,3,3,4,4,4},{1,std::sqrt(.5),1,std::sqrt(.5),1,std::sqrt(.5),1,std::sqrt(.5),1}};
    const auto cut=intersect_curve_plane({circle,{0,0,0},{0,1,0},1e-7,true});check(cut.points.size()==2,"Closed rational circle seam was duplicated or a root omitted");
    near(cut.points[0].position.x,1);near(cut.points[1].position.x,-1);
    for(const auto& root:cut.points)check(root.kind==CurvePlaneContactKind::Crossing,"Closed circle seam was mistaken for an open endpoint or tangency");
    const auto tangent=intersect_curve_plane({circle,{1,0,0},{1,0,0},1e-7,true});check(tangent.points.size()==1&&tangent.points.front().kind==CurvePlaneContactKind::Tangent,"Rational circle seam tangency was lost");
    Sweep3DRequest::PathSegment line;line.source_id="line";line.start={-2,3,-5};line.end={4,9,7};
    const auto native=exact_path_segment_geometry(line);const auto point=intersect_curve_plane({native,{0,0,0},{0,0,1},1e-7});
    check(point.points.size()==1,"Authored 3D route line did not create a point");near(point.points.front().position.x,.5);near(point.points.front().position.y,5.5);
    std::cout<<"Exact rational closed-curve seam and authored 3D path adapter checks passed\n";
}
}
int main(){try{contacts();rational_and_path();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

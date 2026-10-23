#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>

namespace zima::tests {
// Independent rectangle oracle: material perimeter length and all eight rails.
// It deliberately does not use production preview generation or ancestry filters.
inline void verify_rectangular_thin_start(const document::PartDocument& doc,
        const document::HistoryContainer& feature,const std::vector<kernel::ViewerEdge>& edges) {
    const auto found=std::ranges::find(doc.sketches,feature.feature.sketch_id,&sketcher::Sketch::id);
    if(found==doc.sketches.end()||found->segments.size()!=4||!found->corner_radii.empty())
        throw std::runtime_error("Rectangle oracle requires an unrounded four-segment profile");
    double xmin=1e100,xmax=-1e100,ymin=1e100,ymax=-1e100;
    for(const auto& p:found->points){xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);}
    kernel::ViewerMesh frame;frame.edges.push_back({{found->world_point(0,0),found->world_point(1,0),found->world_point(0,1)},{}});
    if(const auto* body=doc.body_owner_for_object(feature.id))frame=doc.place_body_mesh(std::move(frame),body->scope.id);
    const auto origin=frame.edges.front().points[0],px=frame.edges.front().points[1],py=frame.edges.front().points[2];
    const kernel::Vec3 x{px.x-origin.x,px.y-origin.y,px.z-origin.z},y{py.x-origin.x,py.y-origin.y,py.z-origin.z};
    const kernel::Vec3 normal{x.y*y.z-x.z*y.y,x.z*y.x-x.x*y.z,x.x*y.y-x.y*y.x};
    const auto local=[&](kernel::Vec3 p){const kernel::Vec3 v{p.x-origin.x,p.y-origin.y,p.z-origin.z};
        return std::array{v.x*x.x+v.y*x.y+v.z*x.z,v.x*y.x+v.y*y.y+v.z*y.z,v.x*normal.x+v.y*normal.y+v.z*normal.z};};
    const auto mode=feature.feature.thin_mode;const auto t=feature.feature.thin_thickness;
    const double outside=mode==document::ThinMode::OneSide?0:mode==document::ThinMode::OtherSide?t:t*.5;
    const double inside=mode==document::ThinMode::OneSide?t:mode==document::ThinMode::OtherSide?0:t*.5;
    constexpr double eps=1e-5;
    const auto quantize=[](double a,double b){return std::array{std::llround(a*1e5),std::llround(b*1e5)};};
    std::set<std::array<long long,2>> expected,actual;
    for(double d:{-outside,inside})for(double a:{xmin+d,xmax-d})for(double b:{ymin+d,ymax-d})expected.insert(quantize(a,b));
    const auto on_perimeter=[&](const auto& p){
        for(double d:{-outside,inside})if(p[0]>=xmin+d-eps&&p[0]<=xmax-d+eps&&p[1]>=ymin+d-eps&&p[1]<=ymax-d+eps&&
            (std::abs(p[0]-xmin-d)<eps||std::abs(p[0]-xmax+d)<eps||std::abs(p[1]-ymin-d)<eps||std::abs(p[1]-ymax+d)<eps))return true;
        return false;
    };
    double perimeter{};unsigned rails{};
    for(const auto& edge:edges) {
        if(edge.reference.owner_id!=feature.id)throw std::runtime_error("Thin preview contains another object's edge");
        for(std::size_t i=1;i<edge.points.size();++i) {
            const auto a=local(edge.points[i-1]),b=local(edge.points[i]);
            if(std::abs(a[2])<eps&&std::abs(b[2])<eps) {
                if(!on_perimeter(a)||!on_perimeter(b))throw std::runtime_error("Thin preview contains an extra stationary contour");
                perimeter+=std::hypot(b[0]-a[0],b[1]-a[1]);
            }
            if(std::hypot(b[0]-a[0],b[1]-a[1])<eps&&std::abs(b[2]-a[2])>eps&&
                    (std::abs(a[2])<eps||std::abs(b[2])<eps)) {
                ++rails;actual.insert(quantize(a[0],a[1]));
            }
        }
    }
    const double expected_perimeter=4*((xmax-xmin)+(ymax-ymin))+8*(outside-inside);
    if(std::abs(perimeter-expected_perimeter)>eps)
        throw std::runtime_error("Thin start perimeter contains missing/duplicate strokes: "+std::to_string(perimeter)+" expected "+std::to_string(expected_perimeter));
    if(rails!=8||actual!=expected)throw std::runtime_error("Thin start must expose exactly eight correctly placed longitudinal rails; got "+std::to_string(rails));
}
} // namespace zima::tests

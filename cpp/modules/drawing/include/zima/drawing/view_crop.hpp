#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <cmath>
#include <stdexcept>
namespace zima::drawing {
// Exact axis bounds of the same periodic cubic boundary used by the renderer.
inline std::array<Point2,2> crop_bounds(const ViewCrop& crop) {
    if(crop.shape!=ViewCropShape::Spline) {
        const auto r=crop.points.at(0);
        return {{{crop.anchor.x-r.x,crop.anchor.y-r.y},{crop.anchor.x+r.x,crop.anchor.y+r.y}}};
    }
    auto low=crop.points.at(0),high=low;
    const auto include=[&](Point2 p){low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);};
    const auto n=crop.points.size();
    for(std::size_t i=0;i<n;++i) {
        const auto a=crop.points[(i+n-1)%n],b=crop.points[i],c=crop.points[(i+1)%n],d=crop.points[(i+2)%n];
        const Point2 u{b.x+(c.x-a.x)/6,b.y+(c.y-a.y)/6},v{c.x-(d.x-b.x)/6,c.y-(d.y-b.y)/6};
        include(b);include(c);
        const auto at=[&](double t){const auto s=1-t;return Point2{s*s*s*b.x+3*s*s*t*u.x+3*s*t*t*v.x+t*t*t*c.x,s*s*s*b.y+3*s*s*t*u.y+3*s*t*t*v.y+t*t*t*c.y};};
        for(bool y:{false,true}) {
            const auto coord=[&](Point2 p){return y?p.y:p.x;};
            const double p=coord(b),q=coord(u),r=coord(v),s=coord(c);
            const double aa=-p+3*q-3*r+s,bb=2*(p-2*q+r),cc=q-p;
            const auto root=[&](double t){if(t>0&&t<1)include(at(t));};
            if(std::abs(aa)<1e-14){if(std::abs(bb)>1e-14)root(-cc/bb);}
            else if(const double discriminant=bb*bb-4*aa*cc;discriminant>=0) {
                const auto delta=std::sqrt(discriminant);root((-bb-delta)/(2*aa));root((-bb+delta)/(2*aa));
            }
        }
    }
    return {low,high};
}
inline void validate_view_crop(const DrawingView& view) {
    if(!view.crop)return;
    const auto& c=*view.crop;
    const auto finite=[](Point2 p){return std::isfinite(p.x)&&std::isfinite(p.y);};
    bool valid=finite(c.anchor)&&int(c.shape)>=0&&int(c.shape)<=2;
    for(auto p:c.points)valid=valid&&finite(p);
    if(c.shape==ViewCropShape::Spline){
        valid=valid&&c.points.size()>=3&&c.points.size()<=256;
        double area=0;for(std::size_t i=0;i<c.points.size();++i){const auto a=c.points[i],b=c.points[(i+1)%c.points.size()];area+=a.x*b.y-a.y*b.x;}
        valid=valid&&std::abs(area)>1e-8;
    }
    else valid=valid&&c.points.size()==1&&c.points.front().x>1e-6&&c.points.front().y>1e-6;
    if(!valid)throw std::runtime_error("Invalid drawing crop boundary.");
}
}

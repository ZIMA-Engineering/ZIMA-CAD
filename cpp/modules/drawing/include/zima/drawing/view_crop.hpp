#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <cmath>
#include <stdexcept>
namespace zima::drawing {
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

#pragma once
#include <zima/drawing/view_crop.hpp>
#include <QPainterPath>
namespace zima::drawing_render {
inline QPainterPath crop_path(const drawing::ViewCrop& crop) {
    QPainterPath path;
    if(crop.shape!=drawing::ViewCropShape::Spline){
        if(crop.points.size()!=1)return path;
        const auto r=crop.points.front();path.addEllipse(QPointF(crop.anchor.x,crop.anchor.y),r.x,r.y);return path;
    }
    const auto& p=crop.points;const auto n=p.size();if(n<3)return path;
    const auto point=[&](std::size_t i){return QPointF(p[i%n].x,p[i%n].y);};
    path.moveTo(point(0));
    for(std::size_t i=0;i<n;++i){const auto a=point((i+n-1)%n),b=point(i),c=point(i+1),d=point(i+2);path.cubicTo(b+(c-a)/6,c-(d-b)/6,c);}
    path.closeSubpath();return path;
}
inline QPainterPath crop_screen_path(const drawing::DrawingView& view,QPointF origin,double scale) {
    if(!view.crop)return {};
    QTransform transform;transform.translate(origin.x(),origin.y());transform.scale(scale,-scale);return transform.map(crop_path(*view.crop));
}
}

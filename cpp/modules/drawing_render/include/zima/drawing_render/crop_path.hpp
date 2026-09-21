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
    QTransform transform;transform.translate(origin.x(),origin.y());transform.scale(scale,-scale);
    auto path=crop_path(*view.crop);
    for(const auto& inherited:view.inherited_crops)path=path.intersected(crop_path(inherited));
    return transform.map(path);
}
// The crop definition is a closed loop, but its printable boundary only
// crosses the projected solid. Winding-filled triangles include overlapping
// components without turning their overlap into an artificial hole.
inline QPainterPath projected_body_path(const drawing::DrawingView& view,QPointF origin,double scale) {
    QPainterPath path;path.setFillRule(Qt::WindingFill);
    for(const auto& triangle:view.projected_triangles) {
        const auto& p=triangle.points;
        const double area=(p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);
        if(std::abs(area)<1e-12)continue;
        const auto point=[&](int i){return origin+QPointF(p[i].x*scale,-p[i].y*scale);};
        path.moveTo(point(0));path.lineTo(point(area>0?1:2));path.lineTo(point(area>0?2:1));path.closeSubpath();
    }
    return path;
}
}

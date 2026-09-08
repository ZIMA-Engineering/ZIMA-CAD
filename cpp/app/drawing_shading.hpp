#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <QImage>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <limits>

namespace zima::app {
// Rasterize only the shaded fill; all drawing strokes/text remain vector.
// Per-vertex depth avoids the painter-order failure on overlapping faces.
inline QImage drawing_shaded_fill(const zima::drawing::DrawingView& view,
    const QRectF& bounds,double requested_pixels_per_unit) {
    if(bounds.width()<=0||bounds.height()<=0)return {};
    const double requested_width=bounds.width()*requested_pixels_per_unit;
    const double requested_height=bounds.height()*requested_pixels_per_unit;
    const double reduction=std::min({1.0,8192/std::max(requested_width,requested_height),
        std::sqrt(16'000'000.0/std::max(1.0,requested_width*requested_height))});
    const int width=std::max(1,static_cast<int>(std::ceil(requested_width*reduction)));
    const int height=std::max(1,static_cast<int>(std::ceil(requested_height*reduction)));
    QImage image(width,height,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    std::vector<double> depth(static_cast<std::size_t>(width)*height,-std::numeric_limits<double>::infinity());
    const auto screen=[&](const auto& point){return QPointF((point.x-bounds.left())/bounds.width()*width,(bounds.bottom()-point.y)/bounds.height()*height);};
    for(const auto& triangle:view.projected_triangles) {
        const auto a=screen(triangle.points[0]),b=screen(triangle.points[1]),c=screen(triangle.points[2]);
        const double determinant=(b.x()-a.x())*(c.y()-a.y())-(b.y()-a.y())*(c.x()-a.x());
        if(std::abs(determinant)<1e-12)continue;
        const int xmin=std::clamp(static_cast<int>(std::floor(std::min({a.x(),b.x(),c.x()}))),0,width-1);
        const int xmax=std::clamp(static_cast<int>(std::ceil(std::max({a.x(),b.x(),c.x()}))),0,width-1);
        const int ymin=std::clamp(static_cast<int>(std::floor(std::min({a.y(),b.y(),c.y()}))),0,height-1);
        const int ymax=std::clamp(static_cast<int>(std::ceil(std::max({a.y(),b.y(),c.y()}))),0,height-1);
        const double light=std::clamp(triangle.light,0.0,1.0);
        const QRgb color=qRgb(static_cast<int>(185*light),static_cast<int>(194*light),static_cast<int>(204*light));
        for(int y=ymin;y<=ymax;++y)for(int x=xmin;x<=xmax;++x) {
            const double wb=((x+.5-a.x())*(c.y()-a.y())-(y+.5-a.y())*(c.x()-a.x()))/determinant;
            const double wc=((b.x()-a.x())*(y+.5-a.y())-(b.y()-a.y())*(x+.5-a.x()))/determinant;
            const double wa=1-wb-wc;
            if(wa<-1e-10||wb<-1e-10||wc<-1e-10)continue;
            const double z=wa*triangle.vertex_depths[0]+wb*triangle.vertex_depths[1]+wc*triangle.vertex_depths[2];
            auto& previous=depth[static_cast<std::size_t>(y)*width+x];
            auto& pixel=reinterpret_cast<QRgb*>(image.scanLine(y))[x];
            // At exactly coincident surfaces, choose a stable colour rather
            // than whichever triangle happened to be visited last.
            if(z>previous+1e-9||(std::abs(z-previous)<=1e-9&&color<pixel)){previous=z;pixel=color;}
        }
    }
    return image;
}
} // namespace zima::app

#pragma once
#include <QImage>
#include <algorithm>

namespace zima::app {
// Remove only a uniform exterior background. Preserve aspect ratio and every
// foreground pixel, with a two-pixel border; never crop into model geometry.
inline QImage compact_thumbnail_image(const QImage& source) {
    if(source.isNull())return source;
    const auto image=source.convertToFormat(QImage::Format_ARGB32);
    const QRgb background=image.pixel(0,0);
    const auto matches=[&](QRgb color) {
        if(qAlpha(background)==0)return qAlpha(color)==0;
        return color==background;
    };
    if(!matches(image.pixel(image.width()-1,0))||!matches(image.pixel(0,image.height()-1))||
        !matches(image.pixel(image.width()-1,image.height()-1)))return source;
    int left=image.width(),right=-1,top=image.height(),bottom=-1;
    for(int y=0;y<image.height();++y) {
        const auto* row=reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for(int x=0;x<image.width();++x)if(!matches(row[x])) {
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
        }
    }
    if(right<left)return source;
    const QRect content=QRect(QPoint(left,top),QPoint(right,bottom)).adjusted(-2,-2,2,2).intersected(image.rect());
    return content==image.rect()?source:image.copy(content);
}
}

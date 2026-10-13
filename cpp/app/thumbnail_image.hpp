#pragma once
#include <QImage>
#include <algorithm>

namespace zima::app {
// Remove an exact uniform or vertical-gradient exterior background. Preserve
// every differing foreground pixel, with a two-pixel border. Gradient detection
// requires matching side borders, monotone channels and complete end rows.
inline QImage compact_thumbnail_image(const QImage& source) {
    if(source.isNull())return source;
    const auto image=source.convertToFormat(QImage::Format_ARGB32);
    const QRgb background=image.pixel(0,0);
    const auto uniform_matches=[&](QRgb color) {
        if(qAlpha(background)==0)return qAlpha(color)==0;
        return color==background;
    };
    const bool uniform=uniform_matches(image.pixel(image.width()-1,0))&&
        uniform_matches(image.pixel(0,image.height()-1))&&uniform_matches(image.pixel(image.width()-1,image.height()-1));
    if(!uniform) {
        const QRgb last=image.pixel(0,image.height()-1);
        const auto monotone=[](int first,int previous,int current,int last) {
            return last>=first?current>=previous&&current<=last:current<=previous&&current>=last;
        };
        QRgb previous=background;
        for(int y=0;y<image.height();++y) {
            const QRgb color=image.pixel(0,y);
            if(qAlpha(color)!=255||color!=image.pixel(image.width()-1,y)||
                !monotone(qRed(background),qRed(previous),qRed(color),qRed(last))||
                !monotone(qGreen(background),qGreen(previous),qGreen(color),qGreen(last))||
                !monotone(qBlue(background),qBlue(previous),qBlue(color),qBlue(last)))return source;
            previous=color;
        }
        for(int x=0;x<image.width();++x)
            if(image.pixel(x,0)!=background||image.pixel(x,image.height()-1)!=last)return source;
    }
    int left=image.width(),right=-1,top=image.height(),bottom=-1;
    for(int y=0;y<image.height();++y) {
        const auto* row=reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for(int x=0;x<image.width();++x)if(!(uniform?uniform_matches(row[x]):row[x]==row[0])) {
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
        }
    }
    if(right<left)return source;
    const QRect content=QRect(QPoint(left,top),QPoint(right,bottom)).adjusted(-2,-2,2,2).intersected(image.rect());
    return content==image.rect()?source:image.copy(content);
}
}

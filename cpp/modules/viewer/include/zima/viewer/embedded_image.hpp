#pragma once
#include <QByteArray>
#include <QCache>
#include <QCryptographicHash>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QSvgRenderer>
#include <algorithm>
#include <string>
namespace zima::viewer {
// Shared, bounded GUI-thread cache; panning and hover never re-decode a logo.
inline QImage embedded_png(const std::string& encoded) {
    static QCache<QByteArray,QImage> cache(32768); // KiB
    const auto bytes=QByteArray::fromRawData(encoded.data(),static_cast<qsizetype>(encoded.size()));
    const auto key=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
    if(const auto* found=cache.object(key))return *found;
    auto image=QImage::fromData(QByteArray::fromBase64(bytes),"PNG");
    if(!image.isNull())cache.insert(key,new QImage(image),static_cast<int>(std::max<qsizetype>(1,image.sizeInBytes()/1024)));
    return image;
}
inline void paint_embedded_image(QPainter& painter,const std::string& encoded,
        const std::string& format,const QPolygonF& target) {
    QTransform transform;
    if(!QTransform::quadToQuad(QPolygonF{QPointF(0,0),QPointF(1,0),QPointF(1,1),QPointF(0,1)},target,transform))return;
    painter.save();painter.setWorldTransform(transform,true);
    if(format=="svg") {
        static QCache<QByteArray,QSvgRenderer> cache(32768);
        const auto bytes=QByteArray::fromRawData(encoded.data(),static_cast<qsizetype>(encoded.size()));
        const auto key=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
        auto* renderer=cache.object(key);
        if(!renderer) {
            renderer=new QSvgRenderer(QByteArray::fromBase64(bytes));renderer->setAspectRatioMode(Qt::IgnoreAspectRatio);
            renderer->setAnimationEnabled(false);
            if(!cache.insert(key,renderer,static_cast<int>(std::max<qsizetype>(1,bytes.size()/1024))))renderer=nullptr;
        }
        if(renderer&&renderer->isValid())renderer->render(&painter,QRectF(0,0,1,1));
    } else {
        const auto image=embedded_png(encoded);
        if(!image.isNull()){painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(QRectF(0,0,1,1),image);}
    }
    painter.restore();
}
} // namespace zima::viewer

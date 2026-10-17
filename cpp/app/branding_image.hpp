#pragma once
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QIcon>
#include <QPalette>
#include <QPainter>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QGuiApplication>

namespace zima::app {
// Adapt the existing ZIMA artwork, including previously imported copies.
// Keep its green field and white lettering unchanged. Other artwork retains
// its own authored colors; this is not an image-wide inversion filter.
inline QPixmap branding_pixmap(const QString& path,const QSize& size,const QPalette& palette,qreal ratio=0) {
    if(ratio<=0)ratio=qGuiApp?qGuiApp->devicePixelRatio():1;
    const QFileInfo info(path);
    const bool dark=palette.color(QPalette::Window).lightness()<128;
    const QString key=QStringLiteral("zima-brand:%1:%2:%3:%4:%5:%6:%7")
        .arg(info.absoluteFilePath()).arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size())
        .arg(size.width()).arg(size.height()).arg(dark).arg(ratio);
    QPixmap pixmap;if(QPixmapCache::find(key,&pixmap))return pixmap;
    QByteArray bytes;
    if(info.suffix().compare("svg",Qt::CaseInsensitive)==0&&info.size()<4*1024*1024) {
        QFile file(path);if(file.open(QIODevice::ReadOnly))bytes=file.readAll();
    }
    if(bytes.contains("157.2 336.6 254.7 47.4")&&bytes.contains("#80AA1A")&&bytes.contains(".st1{fill:#1C1B17;}")) {
        QString svg=QString::fromUtf8(bytes);
        // Qt SVG has no application color-scheme context. Resolve the same
        // ink rule explicitly instead of relying on its CSS media support.
        svg.remove(QRegularExpression(QStringLiteral("@media\\s*(?:\\(prefers-color-scheme:\\s*dark\\)|print)\\s*\\{\\s*\\.st1\\s*\\{[^}]*\\}\\s*\\}")));
        if(dark)svg.replace(".st1{fill:#1C1B17;}",".st1{fill:#FEFEFE;}");
        QSvgRenderer renderer(svg.toUtf8());
        if(renderer.isValid()) {
            const QSize fitted=renderer.defaultSize().scaled(size,Qt::KeepAspectRatio);
            pixmap=QPixmap(fitted*ratio);pixmap.setDevicePixelRatio(ratio);pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);renderer.render(&painter,QRectF(QPointF(0,0),QSizeF(fitted)));painter.end();
        }
    }
    if(pixmap.isNull())pixmap=QIcon(path).pixmap(size);
    if(!pixmap.isNull())QPixmapCache::insert(key,pixmap);
    return pixmap;
}
}

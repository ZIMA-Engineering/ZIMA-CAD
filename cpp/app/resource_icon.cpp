#include "resource_icon.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSize>
#include <QSvgRenderer>

namespace zima::app {
namespace {

QIcon svg_icon(const QString& path, bool palette_color) {
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) return {};
    QByteArray svg = source.readAll();
    QString color;
    if (palette_color && qApp != nullptr) {
        color = qApp->palette().color(QPalette::WindowText).name();
        svg.replace("currentColor", color.toUtf8());
    }
    const QString cache_key = path + QLatin1Char('|') + color;
    static QHash<QString, QIcon> cache;
    if (const auto found = cache.constFind(cache_key); found != cache.constEnd()) {
        return *found;
    }
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return {};
    QIcon icon;
    for (const int size : {16, 18, 20, 24, 32, 48}) {
        QPixmap pixmap(QSize(size, size));
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        icon.addPixmap(pixmap);
    }
    cache.insert(cache_key, icon);
    return icon;
}

}  // namespace

QIcon resource_icon(const QString& name) {
    return svg_icon(QStringLiteral(":/zima/icons/") + name + QStringLiteral(".svg"),
                    true);
}

QIcon application_icon() {
    return svg_icon(QStringLiteral(":/zima/branding/app-icon.svg"), false);
}

}  // namespace zima::app

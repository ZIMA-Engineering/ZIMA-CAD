#include "resource_icon.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSize>
#include <QSvgRenderer>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QEvent>

namespace zima::app {
namespace {

QIcon svg_icon(const QString& path, bool palette_color, bool neutral_origin = false) {
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) return {};
    QByteArray svg = source.readAll();
    if (neutral_origin) svg.replace("#FF0000", "currentColor");
    const QByteArray original = svg;
    QString color;
    if (palette_color && qApp != nullptr) {
        color = qApp->palette().color(QPalette::WindowText).name();
        svg.replace("currentColor", color.toUtf8());
    }
    const QString cache_key = path + QLatin1Char('|') + color +
        QString::number(neutral_origin) + QString::number(qApp ? qApp->palette().cacheKey() : 0);
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
        painter.end();
        icon.addPixmap(pixmap);
        // Checked tool controls use the bright azure surface. Keep semantic
        // colours (confirmation green, Origin red) and contrast neutral marks.
        auto checked_svg=original;
        checked_svg.replace("currentColor", "#102027");
        checked_svg.replace("#00D1FF", "#102027");
        QSvgRenderer checked_renderer(checked_svg);
        QPixmap checked(QSize(size,size));checked.fill(Qt::transparent);
        QPainter checked_painter(&checked);checked_renderer.render(&checked_painter);checked_painter.end();
        icon.addPixmap(checked,QIcon::Normal,QIcon::On);
        icon.addPixmap(checked,QIcon::Active,QIcon::On);
        if((path.endsWith("/origin.svg") && !neutral_origin) || path.endsWith("/active-check.svg")) {
            icon.addPixmap(pixmap,QIcon::Active);
            icon.addPixmap(pixmap,QIcon::Selected);
        } else if (palette_color && original.contains("currentColor") && qApp) {
            auto selected_svg = original;
            selected_svg.replace("currentColor", qApp->palette().color(QPalette::HighlightedText).name().toUtf8());
            QSvgRenderer selected_renderer(selected_svg);
            QPixmap selected(QSize(size,size)); selected.fill(Qt::transparent);
            QPainter selected_painter(&selected); selected_renderer.render(&selected_painter); selected_painter.end();
            icon.addPixmap(selected,QIcon::Selected);
        }
    }
    cache.insert(cache_key, icon);
    return icon;
}

}  // namespace

void install_dialog_button_icons() {
    if (!qApp || qApp->property("zimaDialogButtonIcons").toBool()) return;
    class ButtonIcons final : public QObject {
    public:
        explicit ButtonIcons(QObject* parent) : QObject(parent) {}
        bool eventFilter(QObject* object, QEvent* event) override {
            const auto type = event->type();
            if (type != QEvent::Show && type != QEvent::Polish)
                return false;
            auto* button = qobject_cast<QAbstractButton*>(object);
            if (!button) return false;
            bool ok = false, cancel = false;
            if (auto* box = qobject_cast<QDialogButtonBox*>(button->parentWidget())) {
                ok = box->standardButton(button) == QDialogButtonBox::Ok;
                cancel = box->standardButton(button) == QDialogButtonBox::Cancel;
            }
            if (auto* message = qobject_cast<QMessageBox*>(button->window())) {
                ok = message->standardButton(button) == QMessageBox::Ok;
                cancel = message->standardButton(button) == QMessageBox::Cancel;
            }
            if (ok || cancel) {
                button->setIcon(resource_icon(ok ? "active-check" : "dialog-cancel"));
                button->setIconSize(QSize(18,18));
            }
            return false;
        }
    };
    qApp->setProperty("zimaDialogButtonIcons", true);
    qApp->installEventFilter(new ButtonIcons(qApp));
}

static QIcon raster_resource_icon(const QString& name, bool surface) {
    if (name == "origin-document") return svg_icon(QStringLiteral(":/zima/icons/origin.svg"),true,true);
    if (name == "origin-feature") {
        static QHash<QString,QIcon> origins;
        if (origins.contains(name)) return origins.value(name);
        QIcon icon;
        const auto source = resource_icon("origin");
        const QColor color("#4DD811");
        for (const int size : {16,18,20,24,32,48}) {
            auto pixmap = source.pixmap(size,size);
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(),color);
            painter.end();
            icon.addPixmap(pixmap);
            icon.addPixmap(pixmap,QIcon::Active);
            icon.addPixmap(pixmap,QIcon::Selected);
        }
        origins.insert(name,icon);
        return icon;
    }
    if(name=="protrusion-revolve") {
        static QHash<QString,QIcon> combined;
        const auto key=QString::number(surface)+QString::number(qApp ? qApp->palette().cacheKey() : 0);
        if(combined.contains(key))return combined.value(key);
        QIcon icon;const auto first=resource_icon("protrusion",surface),second=resource_icon("revolve",surface);
        for(const int size:{16,18,20,24,32,48}) {
            QPixmap pixmap(2*size,size);pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.drawPixmap(0,0,first.pixmap(size,size));
            painter.drawPixmap(size,0,second.pixmap(size,size));painter.end();icon.addPixmap(pixmap);
        }
        combined.insert(key,icon);return icon;
    }
    if(surface) {
        QIcon icon;
        const auto base=resource_icon(name);
        const auto badge=resource_icon("surface");
        for(const int size:{16,18,20,24,32,48}) {
            auto pixmap=base.pixmap(size,size);QPainter painter(&pixmap);
            painter.drawPixmap(size/2,size/2,badge.pixmap(size/2,size/2));painter.end();
            icon.addPixmap(pixmap);

        }
        return icon;
    }
    return svg_icon(QStringLiteral(":/zima/icons/") + name + QStringLiteral(".svg"),
                    true);
}

// Existing QAction/Tree icons follow palette changes without rebuilding the model.
// Rasterization remains cached; repainting with an unchanged palette reuses it.
class PaletteIconEngine final : public QIconEngine {
public:
    PaletteIconEngine(QString name, bool surface) : name_(std::move(name)),surface_(surface) {}
    QIconEngine* clone() const override { return new PaletteIconEngine(name_,surface_); }
    bool isNull() override { return current().isNull(); }
    QSize actualSize(const QSize& size,QIcon::Mode mode,QIcon::State state) override {
        return current().actualSize(size,mode,state);
    }
    QList<QSize> availableSizes(QIcon::Mode mode,QIcon::State state) override {
        return current().availableSizes(mode,state);
    }
    QPixmap pixmap(const QSize& size,QIcon::Mode mode,QIcon::State state) override {
        return current().pixmap(size,mode,state);
    }
    QPixmap scaledPixmap(const QSize& size,QIcon::Mode mode,QIcon::State state,qreal scale) override {
        return current().pixmap(size,scale,mode,state);
    }
    void paint(QPainter* painter,const QRect& rect,QIcon::Mode mode,QIcon::State state) override {
        current().paint(painter,rect,Qt::AlignCenter,mode,state);
    }
private:
    const QIcon& current() {
        const auto key=qApp ? qApp->palette().cacheKey() : 0;
        if(icon_.isNull() || palette_key_!=key) {
            icon_=raster_resource_icon(name_,surface_);palette_key_=key;
        }
        return icon_;
    }
    QString name_;
    bool surface_;
    qint64 palette_key_=-1;
    QIcon icon_;
};

QIcon resource_icon(const QString& name,bool surface) {
    static QHash<QString,QIcon> icons;
    const auto key=name+QLatin1Char('|')+QString::number(surface);
    if (!icons.contains(key)) icons.insert(key,QIcon(new PaletteIconEngine(name,surface)));
    return icons.value(key);
}

QIcon application_icon() {
    return svg_icon(QStringLiteral(":/zima/branding/app-icon.svg"), false);
}

}  // namespace zima::app

#include "resource_icon.hpp"
#include "window_title_icon.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QImage>
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
#include <QSettings>
#include <QStandardPaths>

namespace zima::app {
namespace {

QIcon svg_icon(const QString& path, bool palette_color, bool neutral_origin = false) {
    QString color;
    if (palette_color && qApp != nullptr) {
        color = qApp->palette().color(QPalette::WindowText).name();
    }
    const QString cache_key = path + QLatin1Char('|') + color +
        QString::number(neutral_origin) + QString::number(qApp ? qApp->palette().cacheKey() : 0);
    static QHash<QString, QIcon> cache;
    if (const auto found = cache.constFind(cache_key); found != cache.constEnd()) {
        return *found;
    }
    // Qt resources are immutable; only a cache miss needs their SVG bytes.
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) return {};
    QByteArray svg = source.readAll();
    if (neutral_origin) svg.replace("#FF0000", "currentColor");
    const QByteArray original = svg;
    if (palette_color && qApp != nullptr) svg.replace("currentColor", color.toUtf8());
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
            if(button->objectName()=="containerOriginSelectionButton") {
                button->setIcon(resource_icon("origin-document"));
                button->setIconSize(QSize(18,18));
                return false;
            }
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

static QPixmap branding_pixmap(const QString& path, const QSize& size, qreal scale);

static QIcon raster_resource_icon(const QString& name, bool surface) {
    if (name == "application-logo") {
        const bool dark = qApp && qApp->palette().color(QPalette::Window).lightness() < 128;
        const auto path = dark ? QStringLiteral(":/zima/branding/zc-dark.svg")
                               : QStringLiteral(":/zima/branding/zc-light.svg");
        QIcon icon;
        for (const int size : {16, 24, 32, 48, 64, 128, 256})
            icon.addPixmap(branding_pixmap(path, QSize(size, size), 1));
        return icon;
    }
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
    PaletteIconEngine(QString name, bool surface, bool tree = false, bool foreground = false) : name_(std::move(name)),surface_(surface),tree_(tree),foreground_(foreground) {}
    QIconEngine* clone() const override { return new PaletteIconEngine(name_,surface_,tree_,foreground_); }
    bool isNull() override { return current().isNull(); }
    QSize actualSize(const QSize& size,QIcon::Mode mode,QIcon::State state) override {
        return current().actualSize(size,icon_mode(mode),tree_?QIcon::Off:state);
    }
    QList<QSize> availableSizes(QIcon::Mode mode,QIcon::State state) override {
        return current().availableSizes(icon_mode(mode),tree_?QIcon::Off:state);
    }
    QPixmap pixmap(const QSize& size,QIcon::Mode mode,QIcon::State state) override {
        return current().pixmap(size,icon_mode(mode),tree_?QIcon::Off:state);
    }
    QPixmap scaledPixmap(const QSize& size,QIcon::Mode mode,QIcon::State state,qreal scale) override {
        return current().pixmap(size,scale,icon_mode(mode),tree_?QIcon::Off:state);
    }
    void paint(QPainter* painter,const QRect& rect,QIcon::Mode mode,QIcon::State state) override {
        current().paint(painter,rect,Qt::AlignCenter,icon_mode(mode),tree_?QIcon::Off:state);
    }
private:
    QIcon::Mode icon_mode(QIcon::Mode mode) const {
        return foreground_&&mode!=QIcon::Disabled?QIcon::Normal:mode;
    }
    const QIcon& current() {
        const auto key=qApp ? qApp->palette().cacheKey() : 0;
        if(icon_.isNull() || palette_key_!=key) {
            icon_=raster_resource_icon(name_,surface_);palette_key_=key;
        }
        return icon_;
    }
    QString name_;
    bool surface_;
    // Tree expansion is not a checked command with an azure background.
    bool tree_;
    bool foreground_;
    qint64 palette_key_=-1;
    QIcon icon_;
};

QIcon resource_icon(const QString& name,bool surface) {
    static QHash<QString,QIcon> icons;
    const auto key=name+QLatin1Char('|')+QString::number(surface);
    if (!icons.contains(key)) icons.insert(key,QIcon(new PaletteIconEngine(name,surface)));
    return icons.value(key);
}

QIcon resource_foreground_icon(const QString& name) {
    return QIcon(new PaletteIconEngine(name,false,true,true));
}

QIcon application_icon() {
    static QIcon icon(new PaletteIconEngine(
        QStringLiteral("application-logo"),false));
    return icon;
}

static bool system_uses_dark_colors() {
    bool dark = !qApp || qApp->palette().color(QPalette::Window).lightness() < 128;
#ifdef Q_OS_WIN
    const QSettings personalization(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    dark = !personalization.value(QStringLiteral("SystemUsesLightTheme"), 0).toBool();
#elif defined(Q_OS_LINUX)
    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QStringLiteral("KDE"), Qt::CaseInsensitive)) {
        const auto config = QStandardPaths::locate(QStandardPaths::GenericConfigLocation,
                                                   QStringLiteral("kdeglobals"));
        if (!config.isEmpty()) {
            const QSettings kde(config, QSettings::IniFormat);
            const auto rgb = kde.value(QStringLiteral("Colors:Window/BackgroundNormal")).toStringList();
            if (rgb.size() >= 3)
                dark = QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt()).lightness() < 128;
        }
    }
#endif
    return dark;
}

QIcon system_application_icon() {
    const auto make_icon = [](const QString& path) {
        QIcon icon;
        for (const int size : {16, 24, 32, 48, 64, 128, 256})
            icon.addPixmap(branding_pixmap(path, QSize(size, size), 1));
        return icon;
    };
    static const auto dark_icon = make_icon(QStringLiteral(":/zima/branding/zc-dark.svg"));
    static const auto light_icon = make_icon(QStringLiteral(":/zima/branding/zc-light.svg"));
    return system_uses_dark_colors() ? dark_icon : light_icon;
}

QIcon window_title_icon() {
#ifdef Q_OS_LINUX
    if (qApp && qApp->platformName().startsWith(QStringLiteral("wayland"))) {
        const auto make_icon = [](bool dark) {
            QFile svg(QStringLiteral(":/zima/branding/zc-title.svg"));
            if (!svg.open(QIODevice::ReadOnly)) return QIcon{};
            auto bytes = svg.readAll();
            if (!dark) bytes.replace("#14263A", "#071729");
            QSvgRenderer renderer(bytes);
            QIcon icon;
            for (const int size : {16, 20, 24, 32, 48, 64}) {
                QPixmap image(size, size);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                renderer.render(&painter);
                painter.end();
                icon.addPixmap(image);
            }
            return icon;
        };
        static const auto dark_icon = make_icon(true);
        static const auto light_icon = make_icon(false);
        const auto& icon = system_uses_dark_colors() ? dark_icon : light_icon;
        if (!icon.isNull()) return icon;
    }
#endif
    return system_application_icon();
}

QImage application_wordmark(const QColor& color) {
    // The supplied design includes ZC at the left. Keep just the wordmark
    // and preserve its transparent edges when applying the display color.
    static const QImage wordmark = QImage(QStringLiteral(":/zima/branding/startup-wordmark.png"))
        .copy(QRect(775, 229, 1310, 275))
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (wordmark.isNull()) return {};
    auto tinted = wordmark;
    QPainter painter(&tinted);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();
    return tinted;
}

static QPixmap branding_pixmap(const QString& path, const QSize& size, qreal scale) {
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) return {};
    QPixmap pixmap((QSizeF(size) * scale).toSize());
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QSizeF bounds = renderer.viewBoxF().size();
    bounds.scale(size, Qt::KeepAspectRatio);
    const QRectF target(QPointF((size.width() - bounds.width()) / 2,
                               (size.height() - bounds.height()) / 2), bounds);
    QPainter painter(&pixmap);
    renderer.render(&painter, target);
    return pixmap;
}

QIcon resource_tree_icon(const QString& name, bool surface) {
    static QHash<QString,QIcon> icons;
    const auto key=name+QLatin1Char('|')+QString::number(surface);
    if(!icons.contains(key))icons.insert(key,QIcon(new PaletteIconEngine(name,surface,true)));
    return icons.value(key);
}

}  // namespace zima::app

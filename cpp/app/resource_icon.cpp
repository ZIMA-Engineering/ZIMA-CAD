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
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QEvent>

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
        painter.end();
        icon.addPixmap(pixmap);
        if(path.endsWith("/origin.svg")) {
            icon.addPixmap(pixmap,QIcon::Active);
            icon.addPixmap(pixmap,QIcon::Selected);
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
            if (type != QEvent::Show && type != QEvent::Polish &&
                type != QEvent::Enter && type != QEvent::Leave &&
                type != QEvent::EnabledChange)
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
            const auto normal = resource_icon("active-check");
            static const QIcon hovered = [normal] {
                QIcon icon;
                for (const int size : {16,18,20,24,32,48}) {
                    auto pixmap = normal.pixmap(size,size,QIcon::Normal);
                    QPainter tint(&pixmap);
                    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    tint.fillRect(pixmap.rect(),Qt::black);
                    tint.end();
                    icon.addPixmap(pixmap);
                    icon.addPixmap(pixmap,QIcon::Active);
                }
                return icon;
            }();
            if (button->icon().cacheKey() == normal.cacheKey() ||
                button->icon().cacheKey() == hovered.cacheKey()) {
                // Qt styles do not consistently request QIcon::Active for a
                // hovered QPushButton. Select the icon explicitly on enter/leave.
                const bool hover = button->isEnabled() && type != QEvent::Leave &&
                    (type == QEvent::Enter || button->underMouse());
                button->setIcon(hover ? hovered : normal);
            }
            return false;
        }
    };
    qApp->setProperty("zimaDialogButtonIcons", true);
    qApp->installEventFilter(new ButtonIcons(qApp));
}

QIcon resource_icon(const QString& name, bool surface) {
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

QIcon application_icon() {
    return svg_icon(QStringLiteral(":/zima/branding/app-icon.svg"), false);
}

}  // namespace zima::app

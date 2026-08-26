#include "zima/ui/reference_cell.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QToolButton>
#include <QWidget>

namespace zima::ui {

QWidget* centered_cell_widget(QWidget* inner) {
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(inner);
    return container;
}

QWidget* build_reference_row_indicator(std::function<void()> remove_callback) {
    auto* container = new QWidget();
    auto* layout = new QStackedLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setStackingMode(QStackedLayout::StackOne);
    container->setFixedSize(30, 30);

    auto* arrow_label = new QLabel(QStringLiteral("\u2192"), container);
    arrow_label->setAlignment(Qt::AlignCenter);
    arrow_label->setToolTip(QObject::tr("Zadejte referenci"));
    arrow_label->setStyleSheet(
        "QLabel{color:#3fbf3f;font-size:16px;font-weight:700}");
    arrow_label->setFixedSize(30, 30);

    auto* remove_button = new QPushButton(QStringLiteral("\u00d7"), container);
    remove_button->setFixedSize(30, 30);
    remove_button->setToolTip(QObject::tr("Odstranit referenci"));
    remove_button->setStyleSheet(
        "QPushButton{color:#ffffff;background:#8b2424;"
        "border:1px solid #b94a4a;border-radius:4px;"
        "font-size:16px;font-weight:700;padding:0}"
        "QPushButton:hover{background:#b83232;border-color:#ed7777}"
        "QPushButton:pressed{background:#6f1d1d}");
    if (remove_callback) {
        QObject::connect(remove_button, &QPushButton::clicked, container,
            [callback = std::move(remove_callback)] { callback(); });
    }

    layout->addWidget(arrow_label);
    layout->addWidget(remove_button);
    layout->setCurrentWidget(arrow_label);
    container->setProperty("_arrowWidget", QVariant::fromValue(
        static_cast<QObject*>(arrow_label)));
    container->setProperty("_removeWidget", QVariant::fromValue(
        static_cast<QObject*>(remove_button)));
    return container;
}

void set_reference_row_populated(QWidget* indicator, bool populated) {
    if (indicator == nullptr) return;
    auto* layout = qobject_cast<QStackedLayout*>(indicator->layout());
    if (layout == nullptr) return;
    const auto target = indicator->property(
        populated ? "_removeWidget" : "_arrowWidget").value<QObject*>();
    if (auto* widget = qobject_cast<QWidget*>(target)) {
        layout->setCurrentWidget(widget);
    }
}

QWidget* build_reference_row_flip_button(
    bool enabled, bool checked, std::function<void(bool)> toggled_callback) {
    auto* button = new QToolButton();
    button->setObjectName("referenceRowFlipButton");
    button->setText(QStringLiteral("\u21c4"));  // ⇄, flip/reverse glyph.
    button->setCheckable(true);
    button->setChecked(checked && enabled);
    button->setEnabled(enabled);
    button->setFixedSize(30, 30);
    button->setToolTip(enabled
        ? QObject::tr("Obrátit směr reference")
        : QObject::tr("Obrácení směru není pro tuto referenci k dispozici"));
    button->setStyleSheet(
        "QToolButton{color:#dddddd;background:#2f3339;"
        "border:1px solid #4a4f57;border-radius:4px;"
        "font-size:14px;font-weight:700;padding:0}"
        "QToolButton:hover{background:#3c414a;border-color:#6a7078}"
        "QToolButton:checked{color:#102027;background:#00d1ff;"
        "border-color:#00a9d1}"
        "QToolButton:disabled{color:#666666;background:#26282c;"
        "border-color:#35383e}");
    if (toggled_callback) {
        QObject::connect(button, &QToolButton::toggled, button,
            [callback = std::move(toggled_callback)](bool value) { callback(value); });
    }
    return button;
}

ReferenceCellItem::ReferenceCellItem(const QString& text)
    : QTableWidgetItem(text) {}

void ReferenceCellItem::set_reference(const QString& value) {
    reference_ = value;
    has_reference_ = true;
}

void ReferenceCellItem::clear_reference() {
    reference_.clear();
    has_reference_ = false;
}

void ReferenceCellItem::set_placeholder_style(const QColor& muted) {
    setForeground(QBrush(muted));
}

}  // namespace zima::ui

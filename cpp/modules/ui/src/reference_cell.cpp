#include "zima/ui/reference_cell.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QToolButton>
#include <QWidget>

namespace zima::ui {

namespace {

constexpr int active_input_role = Qt::UserRole + 131;
constexpr int inspected_role = Qt::UserRole + 132;
constexpr int populated_reference_role = Qt::UserRole + 133;

class ReferenceCellDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem clean(option);
        initStyleOption(&clean, index);
        // Reference state is explicit. Native table selection/focus must not
        // colour this or any neighbouring parameter cell.
        clean.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
        const bool inspected = index.data(inspected_role).toBool();
        const bool active = index.data(active_input_role).toBool();
        const bool populated = index.data(populated_reference_role).toBool();
        // Inspection belongs to the eye button and the View overlay. It must
        // never recolour a stored reference label: on the dark Properties
        // table every populated reference remains normally light and
        // readable, independent of eye state and window focus.
        QColor text_color;
        if (inspected) {
            clean.backgroundBrush = QColor(QStringLiteral("#00d1ff"));
        }
        if (populated) {
            // Never inherit the active/inactive window palette for stored
            // references. A dialog focus change must not turn a valid
            // reference into low-contrast or effectively invisible text.
            text_color = QColor(QStringLiteral("#e6edf3"));
        } else {
            const auto foreground = index.data(Qt::ForegroundRole).value<QBrush>();
            text_color = foreground.style() == Qt::NoBrush
                ? QColor(QStringLiteral("#8d969f")) : foreground.color();
        }
        for (const auto group : {QPalette::Active, QPalette::Inactive,
                                 QPalette::Disabled}) {
            clean.palette.setColor(group, QPalette::Text, text_color);
            clean.palette.setColor(group, QPalette::HighlightedText, text_color);
            clean.palette.setColor(group, QPalette::WindowText, text_color);
        }
        QStyledItemDelegate::paint(painter, clean, index);
        if (!active) return;
        painter->save();
        QPen pen(QColor(QStringLiteral("#42d66b")), 2.0);
        pen.setJoinStyle(Qt::MiterJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(1, 1, -2, -2));
        painter->restore();
    }
};

}  // namespace

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

void install_reference_cell_delegate(QTableWidget* table) {
    if (table == nullptr) return;
    table->setItemDelegate(new ReferenceCellDelegate(table));
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
}

QToolButton* build_reference_inspection_button(
    bool enabled, bool checked, std::function<void(bool)> toggled_callback) {
    auto* button = new QToolButton();
    button->setObjectName("referenceInspectionButton");
    button->setText(QStringLiteral("\U0001f441"));
    button->setCheckable(true);
    button->setChecked(checked && enabled);
    button->setEnabled(enabled);
    button->setFixedSize(30, 30);
    button->setToolTip(enabled
        ? QObject::tr("Zobrazit nebo skrýt tuto referenci ve View")
        : QObject::tr("Nejprve zadejte referenci"));
    button->setStyleSheet(
        "QToolButton{color:#dddddd;background:#2f3339;"
        "border:1px solid #4a4f57;border-radius:4px;"
        "font-size:14px;padding:0}"
        "QToolButton:hover{background:#3c414a;border-color:#6a7078}"
        "QToolButton:checked{color:#102027;background:#00d1ff;"
        "border-color:#00a9d1}"
        "QToolButton:disabled{color:#666666;background:#26282c;"
        "border-color:#35383e}");
    if (toggled_callback) {
        QObject::connect(button, &QToolButton::toggled, button,
            [callback = std::move(toggled_callback)](bool value) {
                callback(value);
            });
    }
    return button;
}

ReferenceCellItem::ReferenceCellItem(const QString& text)
    : QTableWidgetItem(text) {}

void ReferenceCellItem::set_reference(const QString& value) {
    reference_ = value;
    has_reference_ = true;
    setData(populated_reference_role, true);
}

void ReferenceCellItem::clear_reference() {
    reference_.clear();
    has_reference_ = false;
    setData(populated_reference_role, false);
}

void ReferenceCellItem::set_placeholder_style(const QColor& muted) {
    setForeground(QBrush(muted));
}

bool ReferenceCellItem::is_active_input() const {
    return data(active_input_role).toBool();
}

void ReferenceCellItem::set_active_input(bool value) {
    setData(active_input_role, value);
}

bool ReferenceCellItem::is_inspected() const {
    return data(inspected_role).toBool();
}

void ReferenceCellItem::set_inspected(bool value) {
    setData(inspected_role, value);
}

}  // namespace zima::ui

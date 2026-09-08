#include <zima/ui/properties_subwindow.hpp>

#include <QApplication>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QFontMetricsF>
#include <cmath>
#include <QKeyEvent>
#include <QPointer>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <exception>

namespace zima::ui {

int numeric_decimal_places(const QWidget* owner, int fallback) {
    for (auto* current=owner; current; current=current->parentWidget()) {
        const auto value=current->property("zimaDocumentDecimalPlaces");
        if (value.isValid()) return std::clamp(value.toInt(),0,12);
    }
    return std::clamp(fallback,0,12);
}

QPushButton* create_origin_selection_button(QWidget* parent) {
    auto* button = new QPushButton(QObject::tr("POČÁTEK"), parent);
    button->setObjectName("containerOriginSelectionButton");
    button->setCheckable(true);
    button->setAutoDefault(false);
    button->setStyleSheet(
        "QPushButton:checked{background:#4dd811;color:#102010;font-weight:700;}");
    button->setToolTip(QObject::tr(
        "Kliknutím zobrazit nebo skrýt lokální Počátky kontejnerů"));
    return button;
}

QPushButton* PropertiesSubWindow::ensure_origin_selection_button() {
    if (auto* button = findChild<QPushButton*>("containerOriginSelectionButton"))
        return button;
    auto* button = create_origin_selection_button(this);
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(button);
    content_layout()->insertLayout(0, row);
    return button;
}


namespace {
// Measure the actual formatted value and the polished editor chrome. Never
// use the numerical range (often +/-1e9) as a proxy for the displayed width.
int numeric_width(QDoubleSpinBox* spin) {
    auto* editor=spin->findChild<QLineEdit*>();
    if (!editor) return spin->minimumSizeHint().width();
    const QFontMetricsF metrics(editor->font());
    QString zero=spin->prefix();
    if (spin->minimum()<0) zero+=spin->locale().negativeSign();
    zero+=spin->locale().toString(0.0,'f',spin->decimals())+spin->suffix();
    const auto margins=editor->textMargins();
    const int chrome=std::max(0,spin->width()-editor->contentsRect().width())+
        margins.left()+margins.right()+6+(spin->property("zimaValueLockKey").isValid()?24:0);
    return static_cast<int>(std::ceil(std::max(metrics.horizontalAdvance(spin->text()),
        metrics.horizontalAdvance(zero))))+chrome;
}

void fit_numeric_field(QDoubleSpinBox* spin) {
    if (!spin->isVisible()) return;
    const int width=numeric_width(spin);
    if (spin->maximumWidth()<width) spin->setMaximumWidth(width);
    if (spin->minimumWidth()!=width) spin->setMinimumWidth(width);
    for (auto* ancestor=spin->parentWidget(); ancestor; ancestor=ancestor->parentWidget()) {
        auto* table=qobject_cast<QTableWidget*>(ancestor);
        if (!table) continue;
        int column=-1;
        for(int row=0;row<table->rowCount() && column<0;++row)
            for(int c=0;c<table->columnCount();++c)
                if(table->cellWidget(row,c)==spin) {column=c;break;}
        if (column<0) return;
        auto* header=table->horizontalHeader();
        int required=0;
        if (auto* item=table->horizontalHeaderItem(column))
            required=header->fontMetrics().horizontalAdvance(item->text())+18;
        for(int row=0;row<table->rowCount();++row)
            if(auto* field=qobject_cast<QDoubleSpinBox*>(table->cellWidget(row,column)))
                required=std::max(required,numeric_width(field)+2);
        // Reference columns retain their Stretch mode and give the numeric
        // column the space it needs, including disabled and read-only values.
        if(header->sectionResizeMode(column)!=QHeaderView::Fixed)
            header->setSectionResizeMode(column,QHeaderView::Fixed);
        if(header->sectionSize(column)!=required) header->resizeSection(column,required);
        return;
    }
}

// One application filter also covers numeric editors created later in tables.
class NumericInputInteraction final : public QObject {
public:
    explicit NumericInputInteraction(QObject* parent) : QObject(parent) {}
    bool eventFilter(QObject* watched, QEvent* event) override {
        auto* edit = qobject_cast<QLineEdit*>(watched);
        auto* spin = edit ? qobject_cast<QAbstractSpinBox*>(edit->parentWidget())
                         : qobject_cast<QAbstractSpinBox*>(watched);
        if (!spin) return false;
        if (auto* numeric=qobject_cast<QDoubleSpinBox*>(spin)) {
            if (event->type()==QEvent::Show || event->type()==QEvent::Resize ||
                event->type()==QEvent::FontChange || event->type()==QEvent::StyleChange ||
                event->type()==QEvent::LocaleChange || event->type()==QEvent::LayoutRequest ||
                (event->type()==QEvent::Paint && numeric->property("zimaNumericWidthText").toString()!=numeric->text())) {
                if (!numeric->property("zimaNumericWidthBound").toBool()) {
                    numeric->setProperty("zimaNumericWidthBound",true);
                    connect(numeric,&QDoubleSpinBox::textChanged,this,[this,numeric] { schedule_width(numeric); });
                }
                schedule_width(numeric);
            }
        }
        if (!spin->isEnabled() || spin->isReadOnly()) return false;
        // Publishing every keystroke can rebuild a reference table and destroy
        // its editor halfway through a decimal number. Commit on Enter/focus-out.
        spin->setKeyboardTracking(false);
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                const QPointer<QAbstractSpinBox> guarded(spin);
                spin->interpretText();
                if (guarded) QMetaObject::invokeMethod(guarded, "editingFinished",
                    Qt::DirectConnection);
                event->accept();
                return true; // Never propagate numeric confirmation to QDialog::accept.
            }
            if (qobject_cast<QDoubleSpinBox*>(spin) &&
                (key->text() == "," || key->text() == ".") &&
                !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
                if (!edit) edit = spin->findChild<QLineEdit*>();
                if (edit) {
                    edit->insert(spin->locale().decimalPoint());
                    return true;
                }
            }
        }
        if (!edit) return false;
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            pressed_ = mouse->button() == Qt::LeftButton &&
                mouse->modifiers() == Qt::NoModifier ? edit : nullptr;
            origin_ = mouse->globalPosition().toPoint();
        } else if (event->type() == QEvent::MouseMove && pressed_ == edit) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if ((mouse->globalPosition().toPoint() - origin_).manhattanLength() >=
                    QApplication::startDragDistance()) pressed_.clear();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && pressed_ == edit) {
                pressed_.clear();
                spin->selectAll();
                return true;
            }
        }
        return false;
    }
private:
    void schedule_width(QDoubleSpinBox* spin) {
        if(spin->property("zimaNumericWidthPending").toBool()) return;
        spin->setProperty("zimaNumericWidthPending",true);
        QTimer::singleShot(0,spin,[spin] {
            spin->setProperty("zimaNumericWidthPending",false);
            fit_numeric_field(spin);
            // A trailing value-lock action increases the minimum width after
            // show/layout. Propagate that change through nested row widgets.
            for(auto* ancestor=spin->parentWidget();ancestor;ancestor=ancestor->parentWidget()) {
                if(ancestor->layout()){ancestor->layout()->invalidate();ancestor->layout()->activate();}
                ancestor->updateGeometry();
                if(ancestor->property("zimaPropertiesSubWindow").toBool()) {
                    const int needed=ancestor->minimumSizeHint().width();
                    const int available=ancestor->parentWidget()?ancestor->parentWidget()->width():needed;
                    if(ancestor->width()<needed)ancestor->resize(std::min(needed,available),ancestor->height());
                    break;
                }
            }
            spin->setProperty("zimaNumericWidthText",spin->text());
        });
    }
    QPointer<QLineEdit> pressed_;
    QPoint origin_;
};
}

PropertiesSubWindow::PropertiesSubWindow(const QString& title, QWidget* parent)
    : QDialog(parent) {
    static QPointer<NumericInputInteraction> numeric_selection;
    if (!numeric_selection) {
        numeric_selection = new NumericInputInteraction(qApp);
        qApp->installEventFilter(numeric_selection);
    }
    setWindowFlags(Qt::SubWindow | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setModal(false);
    setSizeGripEnabled(true);
    setObjectName("zimaPropertiesSubWindow");
    setProperty("zimaPropertiesSubWindow", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    setStyleSheet(
        "QDialog[zimaPropertiesSubWindow=\"true\"] { background: palette(window);"
        " border: 1px solid #5b6065; border-radius: 5px; }");
    setWindowTitle(title);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 8);
    outer->setSpacing(8);
    title_bar_ = new QWidget(this);
    title_bar_->setObjectName("propertiesTitleBar");
    title_bar_->setFixedHeight(34);
    title_bar_->setCursor(Qt::SizeAllCursor);
    title_bar_->installEventFilter(this);
    title_bar_->setStyleSheet(
        "QWidget#propertiesTitleBar { background: palette(midlight);"
        " border: 1px solid palette(mid); border-radius: 4px; }");
    auto* title_layout = new QHBoxLayout(title_bar_);
    title_layout->setContentsMargins(10, 2, 4, 2);
    title_label_ = new QLabel(title, title_bar_);
    auto title_font = title_label_->font();
    title_font.setBold(true);
    title_label_->setFont(title_font);
    title_layout->addWidget(title_label_, 1);
    auto* close = new QPushButton(title_bar_);
    close->setObjectName("propertiesCloseButton");
    close->setFixedSize(27, 26);
    close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close->setIconSize(QSize(16, 16));
    close->setToolTip(tr("Zrušit"));
    close->setStyleSheet(
        "QPushButton { border:none; border-radius:4px; font-weight:700; }"
        "QPushButton:hover { background:#b83232; color:white; }");
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    title_layout->addWidget(close);
    title_layout->setAlignment(close, Qt::AlignVCenter);
    outer->addWidget(title_bar_);

    content_layout_ = new QVBoxLayout;
    content_layout_->setSpacing(8);
    outer->addLayout(content_layout_);
    submit_error_ = new QLabel(this);
    submit_error_->setObjectName("propertiesSubmitError");
    submit_error_->setWordWrap(true);
    submit_error_->setStyleSheet("color:#ed7777;font-weight:700;");
    submit_error_->hide();
    outer->addWidget(submit_error_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                    Qt::Horizontal, this);
    for (auto* abstract_button : buttons_->buttons()) {
        if (auto* button = qobject_cast<QPushButton*>(abstract_button)) {
            button->setAutoDefault(false);
            button->setDefault(false);
        }
    }
    connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
        submit_error_->hide();
        try {
            if (submit()) accept();
        } catch (const std::exception& error) {
            submit_error_->setText(QString::fromUtf8(error.what()));
            submit_error_->show();
        } catch (...) {
            submit_error_->setText(tr("Operaci nelze dokončit."));
            submit_error_->show();
        }
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons_);
    qApp->installEventFilter(this);
}

PropertiesSubWindow::~PropertiesSubWindow() {
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

QVBoxLayout* PropertiesSubWindow::content_layout() const { return content_layout_; }
QDialogButtonBox* PropertiesSubWindow::buttons() const { return buttons_; }

void PropertiesSubWindow::set_internal_title(const QString& title) {
    setWindowTitle(title);
    title_label_->setText(title);
}

void PropertiesSubWindow::set_centered_on_show(bool centered) {
    centered_on_show_ = centered;
}

void PropertiesSubWindow::set_initial_size(const QSize& size) {
    initial_size_ = size;
}

void PropertiesSubWindow::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (initial_size_.isValid()) {
        QSize target = initial_size_.expandedTo(minimumSizeHint());
        if (parentWidget() != nullptr) {
            target.setWidth(std::min(target.width(), parentWidget()->width()));
            target.setHeight(std::min(target.height(), parentWidget()->height()));
        }
        resize(target);
    } else {
        adjustSize();
    }
    if (parentWidget() != nullptr) {
        const int margin = 12;
        if (centered_on_show_) {
            move(std::max(0, (parentWidget()->width() - width()) / 2),
                 std::max(0, (parentWidget()->height() - height()) / 2));
        } else {
            move(std::max(margin, parentWidget()->width() - width() - margin), margin);
        }
    }
    QTimer::singleShot(0, this, [this] { keep_inside_parent(); raise(); });
}

void PropertiesSubWindow::moveEvent(QMoveEvent* event) {
    QDialog::moveEvent(event);
    keep_inside_parent();
}

void PropertiesSubWindow::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (correcting_size_ || parentWidget() == nullptr) return;
    const QSize bounded(
        std::min(width(), parentWidget()->width()),
        std::min(height(), parentWidget()->height()));
    if (bounded != size()) {
        correcting_size_ = true;
        resize(bounded);
        correcting_size_ = false;
    }
    keep_inside_parent();
}

bool PropertiesSubWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == title_bar_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint dialog_local = mapFromGlobal(
                mouse->globalPosition().toPoint());
            if (mouse->button() == Qt::LeftButton &&
                dialog_local.x() > 8 && dialog_local.x() < width() - 8 &&
                dialog_local.y() > 8) {
                title_drag_origin_ = mouse->globalPosition();
                title_drag_window_origin_ = pos();
                title_drag_active_ = true;
                mouse->accept();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && title_drag_active_) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->buttons().testFlag(Qt::LeftButton)) {
                const QPointF delta = mouse->globalPosition() - title_drag_origin_;
                move(title_drag_window_origin_ + QPoint(
                    static_cast<int>(delta.x()), static_cast<int>(delta.y())));
                mouse->accept();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                title_drag_active_ = false;
                mouse->accept();
                return true;
            }
        }
    }
    auto* watched_widget = qobject_cast<QWidget*>(watched);
    const bool inside_dialog = watched_widget == this ||
        (watched_widget != nullptr && isAncestorOf(watched_widget));
    if (isVisible() && inside_dialog &&
        (event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonRelease)) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint local = mapFromGlobal(mouse->globalPosition().toPoint());
        constexpr int margin = 8;
        Qt::Edges edges;
        if (local.x() <= margin) edges |= Qt::LeftEdge;
        if (local.x() >= width() - margin) edges |= Qt::RightEdge;
        if (local.y() <= margin) edges |= Qt::TopEdge;
        if (local.y() >= height() - margin) edges |= Qt::BottomEdge;
        if (event->type() == QEvent::MouseButtonPress &&
            mouse->button() == Qt::LeftButton && edges != Qt::Edges{}) {
            resize_edges_ = edges;
            resize_drag_origin_ = mouse->globalPosition();
            resize_drag_geometry_ = geometry();
            mouse->accept();
            return true;
        }
        if (event->type() == QEvent::MouseMove && resize_edges_ != Qt::Edges{} &&
            mouse->buttons().testFlag(Qt::LeftButton)) {
            const QPoint delta = (mouse->globalPosition() - resize_drag_origin_).toPoint();
            QRect next = resize_drag_geometry_;
            if (resize_edges_.testFlag(Qt::LeftEdge)) next.setLeft(next.left() + delta.x());
            if (resize_edges_.testFlag(Qt::RightEdge)) next.setRight(next.right() + delta.x());
            if (resize_edges_.testFlag(Qt::TopEdge)) next.setTop(next.top() + delta.y());
            if (resize_edges_.testFlag(Qt::BottomEdge)) next.setBottom(next.bottom() + delta.y());
            const QSize minimum = minimumSizeHint().expandedTo(minimumSize());
            if (next.width() < minimum.width()) {
                if (resize_edges_.testFlag(Qt::LeftEdge)) next.setLeft(next.right() - minimum.width() + 1);
                else next.setRight(next.left() + minimum.width() - 1);
            }
            if (next.height() < minimum.height()) {
                if (resize_edges_.testFlag(Qt::TopEdge)) next.setTop(next.bottom() - minimum.height() + 1);
                else next.setBottom(next.top() + minimum.height() - 1);
            }
            if (parentWidget() != nullptr) next = next.intersected(parentWidget()->rect());
            setGeometry(next);
            mouse->accept();
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease &&
            mouse->button() == Qt::LeftButton && resize_edges_ != Qt::Edges{}) {
            resize_edges_ = {};
            mouse->accept();
            return true;
        }
        if (event->type() == QEvent::MouseMove && mouse->buttons() == Qt::NoButton) {
            const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
            const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
            if (horizontal && vertical) {
                setCursor((edges.testFlag(Qt::LeftEdge) == edges.testFlag(Qt::TopEdge))
                    ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
            } else if (horizontal) setCursor(Qt::SizeHorCursor);
            else if (vertical) setCursor(Qt::SizeVerCursor);
            else unsetCursor();
        }
    }
    const bool inside_owner = watched_widget && parentWidget() &&
        (watched_widget == parentWidget() || parentWidget()->isAncestorOf(watched_widget));
    if (isVisible() && (inside_dialog || inside_owner) &&
        event->type() == QEvent::MouseButtonDblClick) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            if (auto* ok = buttons_->button(QDialogButtonBox::Ok);
                ok != nullptr && ok->isEnabled()) {
                ok->click();
                return true;
            }
        }
    }
    return false;
}

void PropertiesSubWindow::keep_inside_parent() {
    if (correcting_position_ || parentWidget() == nullptr) return;
    correcting_position_ = true;
    const int maximum_x = std::max(0, parentWidget()->width() - width());
    const int maximum_y = std::max(0, parentWidget()->height() - height());
    const QPoint bounded(std::clamp(x(), 0, maximum_x), std::clamp(y(), 0, maximum_y));
    if (bounded != pos()) move(bounded);
    correcting_position_ = false;
}

}  // namespace zima::ui

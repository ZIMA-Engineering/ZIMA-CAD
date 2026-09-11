#pragma once
#include <QAction>
#include <QIcon>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QProxyStyle>
#include <QStyleOptionSpinBox>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QEvent>
#include <algorithm>
#include <functional>
#include <set>
#include <string>
namespace zima::ui {
inline QIcon value_lock_icon(bool locked) {
    QPixmap image(20,20);image.fill(Qt::transparent);QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor(locked?"#f0c75e":"#a8b2bd"),1.7));
    painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(QRectF(4,9,12,9),2,2);
    painter.drawArc(QRectF(locked?6:10,2,8,12),0,180*16);
    painter.drawLine(QPointF(10,12),QPointF(10,15));return QIcon(image);
}
// Reserve a separate trailing area outside the spin-box frame and editor.
// A per-widget style preserves normal spin-box identity, table cells and input.
class NumericValueLockStyle final : public QProxyStyle {
public:
    explicit NumericValueLockStyle(const QString& name):QProxyStyle(name) {}
    static constexpr int trailing_width=24; // 22 px button + 2 px separation
    void drawComplexControl(ComplexControl control,const QStyleOptionComplex* option,
            QPainter* painter,const QWidget* widget=nullptr) const override {
        if(control==CC_SpinBox && !adjusting_) {
            if(const auto* spin=qstyleoption_cast<const QStyleOptionSpinBox*>(option)) {
                auto adjusted=*spin;adjusted.rect.adjust(0,0,-trailing_width,0);
                const QScopedValueRollback guard(adjusting_,true);
                QProxyStyle::drawComplexControl(control,&adjusted,painter,widget);return;
            }
        }
        QProxyStyle::drawComplexControl(control,option,painter,widget);
    }
    QRect subControlRect(ComplexControl control,const QStyleOptionComplex* option,
            SubControl sub,const QWidget* widget=nullptr) const override {
        if(control==CC_SpinBox && !adjusting_) {
            if(const auto* spin=qstyleoption_cast<const QStyleOptionSpinBox*>(option)) {
                auto adjusted=*spin;adjusted.rect.adjust(0,0,-trailing_width,0);
                const QScopedValueRollback guard(adjusting_,true);
                return QProxyStyle::subControlRect(control,&adjusted,sub,widget);
            }
        }
        return QProxyStyle::subControlRect(control,option,sub,widget);
    }
    SubControl hitTestComplexControl(ComplexControl control,const QStyleOptionComplex* option,
            const QPoint& point,const QWidget* widget=nullptr) const override {
        if(control==CC_SpinBox && !adjusting_) {
            if(const auto* spin=qstyleoption_cast<const QStyleOptionSpinBox*>(option)) {
                auto adjusted=*spin;adjusted.rect.adjust(0,0,-trailing_width,0);
                if(!adjusted.rect.contains(point))return SC_None;
                const QScopedValueRollback guard(adjusting_,true);
                return QProxyStyle::hitTestComplexControl(control,&adjusted,point,widget);
            }
        }
        return QProxyStyle::hitTestComplexControl(control,option,point,widget);
    }
    QSize sizeFromContents(ContentsType type,const QStyleOption* option,
            const QSize& size,const QWidget* widget=nullptr) const override {
        auto result=QProxyStyle::sizeFromContents(type,option,size,widget);
        if(type==CT_SpinBox)result.rwidth()+=trailing_width;
        return result;
    }
private:
    mutable bool adjusting_{};
};
class NumericValueLockButton final : public QToolButton {
public:
    explicit NumericValueLockButton(QDoubleSpinBox* field):QToolButton(field),field_(field) {
        setObjectName("numericValueLockButton");setAutoRaise(true);setFocusPolicy(Qt::NoFocus);
        setIconSize({20,20});field->installEventFilter(this);position();show();
    }
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(watched==field_ && (event->type()==QEvent::Resize || event->type()==QEvent::Show))position();
        return QToolButton::eventFilter(watched,event);
    }
private:
    void position(){const int height=std::min(22,field_->height());setGeometry(field_->width()-22,(field_->height()-height)/2,22,height);raise();}
    QDoubleSpinBox* field_;
};

// Locks intentional edits, while allowing the resolver to update dependent data.
inline QAction* bind_numeric_value_lock(QDoubleSpinBox* field,const std::string& key,
        bool locked,std::function<void(bool)> changed,bool editable=true) {
    if(!field)return nullptr;
    auto* line=field->findChild<QLineEdit*>();if(!line)return nullptr;
    for(auto* previous:field->findChildren<QAction*>())if(previous->objectName().startsWith("valueLock:")) {
        line->removeAction(previous);delete previous;
    }
    field->setProperty("zimaValueLockKey",QString::fromStdString(key));
    if(!field->property("zimaValueLockStyle").toBool()) {
        auto* style=new NumericValueLockStyle(field->style()->name());style->setParent(field);
        field->setStyle(style);field->setProperty("zimaValueLockStyle",true);
    }
    auto* button=field->findChild<QToolButton*>("numericValueLockButton");
    if(!button)button=new NumericValueLockButton(field);
    auto* action=new QAction(value_lock_icon(locked),QString{},field);
    button->setDefaultAction(action);
    action->setObjectName(QString::fromStdString("valueLock:"+key));action->setCheckable(true);action->setChecked(locked);
    const auto refresh=[field,action,editable](bool state){
        field->setProperty("zimaValueLocked",state);field->setReadOnly(state||!editable);
        action->setIcon(value_lock_icon(state));
        action->setToolTip(editable
            ? (state?QObject::tr("Odemknout hodnotu"):QObject::tr("Zamknout hodnotu"))
            : (state?QObject::tr("Zrušit převzetí současné hodnoty"):QObject::tr("Při výběru reference převzít současnou hodnotu a odemknout")));
    };
    refresh(locked);
    QObject::connect(action,&QAction::toggled,field,[refresh,changed=std::move(changed)](bool state){refresh(state);if(changed)changed(state);});
    return action;
}
inline QAction* bind_numeric_value_lock(QDoubleSpinBox* field,const std::string& key,
        std::set<std::string>& locks,std::function<void()> changed={}) {
    return bind_numeric_value_lock(field,key,locks.contains(key),[&locks,key,changed=std::move(changed)](bool locked){
        if(locked)locks.insert(key);else locks.erase(key);if(changed)changed();
    });
}
} // namespace zima::ui

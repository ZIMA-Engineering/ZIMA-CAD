#pragma once
#include <QAction>
#include <QIcon>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
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
// Locks intentional edits, while allowing the resolver to update dependent data.
inline QAction* bind_numeric_value_lock(QDoubleSpinBox* field,const std::string& key,
        bool locked,std::function<void(bool)> changed,bool editable=true) {
    if(!field)return nullptr;
    auto* line=field->findChild<QLineEdit*>();if(!line)return nullptr;
    for(auto* previous:line->findChildren<QAction*>(QString::fromStdString("valueLock:"+key))) {
        line->removeAction(previous); delete previous;
    }
    field->setProperty("zimaValueLockKey",QString::fromStdString(key));
    auto* action=line->addAction(value_lock_icon(locked),QLineEdit::TrailingPosition);
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

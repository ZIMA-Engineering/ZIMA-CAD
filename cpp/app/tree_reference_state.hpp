#pragma once
#include <QDialog>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTreeWidgetItem>
#include <string>

namespace zima::app {
inline constexpr int missing_reference_role=Qt::UserRole+40;
inline QColor missing_reference_color() {return QColor(164,45,45);}
class ReferenceTreeDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter,const QStyleOptionViewItem& option,
            const QModelIndex& index) const override {
        if (!index.data(missing_reference_role).toBool()) {
            QStyledItemDelegate::paint(painter,option,index);return;
        }
        auto marked=option;
        const bool selected=marked.state & QStyle::State_Selected;
        marked.state &= ~(QStyle::State_Selected|QStyle::State_MouseOver);
        marked.backgroundBrush=missing_reference_color();
        marked.palette.setColor(QPalette::Text,Qt::white);
        QStyledItemDelegate::paint(painter,marked,index);
        if (selected) {
            painter->save();painter->setPen(QColor(0,209,255));
            painter->setBrush(Qt::NoBrush);painter->drawRect(option.rect.adjusted(0,0,-1,-1));painter->restore();
        }
    }
};

// An unresolved reference remains an error until it is actually repaired.
class TreeReferenceState {
public:
    void apply(QTreeWidgetItem* item,const std::string& document,
            const std::string& object,const std::string& issue) {
        const bool missing=!issue.empty();
        item->setData(0,missing_reference_role,missing);
        item->setBackground(0,missing ? QBrush(missing_reference_color()) : QBrush{});
        item->setToolTip(0,missing ? QObject::tr("Prvek ztratil referenci. Zkontrolujte jeho Vlastnosti.") : QString{});
    }
};
}

#pragma once
#include <QDialog>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTreeWidgetItem>
#include <map>
#include <set>
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

// Presentation acknowledgement is separate from reference validity. OK may
// accept a retained fallback; it must never falsify the placement solver flag.
class TreeReferenceState {
    using Key=std::pair<std::string,std::string>;
    std::map<Key,std::string> acknowledged_;
    std::set<Key> accepted_;
public:
    void watch(QDialog* dialog,QObject* context,const std::string& document,
            const std::string& object) {
        QObject::connect(dialog,&QDialog::accepted,context,[this,key=Key{document,object}] {
            accepted_.insert(key);
        });
    }
    void apply(QTreeWidgetItem* item,const std::string& document,
            const std::string& object,const std::string& issue) {
        const Key key{document,object};
        if (accepted_.erase(key)) acknowledged_[key]=issue;
        if (issue.empty()) acknowledged_.erase(key);
        const auto previous=acknowledged_.find(key);
        const bool missing=!issue.empty() &&
            (previous==acknowledged_.end() || previous->second!=issue);
        item->setData(0,missing_reference_role,missing);
        item->setBackground(0,missing ? QBrush(missing_reference_color()) : QBrush{});
        item->setToolTip(0,missing ? QObject::tr("Prvek ztratil referenci. Zkontrolujte jeho Vlastnosti.") : QString{});
    }
};
}

#pragma once
#include "tool_button_style.hpp"
#include <zima/ui/reference_cell.hpp>
#include <QButtonGroup>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <functional>

namespace zima::app {
inline bool automatic_work_plane(const QComboBox* combo) {
    return combo->currentData() == QVariant(QStringLiteral("auto"));
}
inline QVariant selected_work_plane(const QComboBox* combo) {
    return automatic_work_plane(combo) ? combo->currentData(Qt::UserRole + 1) : combo->currentData();
}
inline void sync_compact_work_plane_selector(const QComboBox* combo) {
    const auto* selector=combo->parentWidget();
    if(!selector||selector->objectName()!=combo->objectName()+QStringLiteral("Selector"))return;
    const auto selected=selected_work_plane(combo);
    bool matched=false;
    for(auto* button:selector->findChildren<QToolButton*>(QString{},Qt::FindDirectChildrenOnly)) {
        if(!button->property("workPlaneData").isValid())continue;
        const bool checked=button->property("workPlaneData")==selected;
        const QSignalBlocker blocker(button);button->setChecked(checked);matched|=checked;
    }
    if(!matched)for(auto* button:selector->findChildren<QToolButton*>(QString{},Qt::FindDirectChildrenOnly))
        if(button->property("workPlaneData").isValid()) {
            const QSignalBlocker blocker(button);button->setChecked(true);break;
        }
}
inline void update_automatic_work_plane(QComboBox* combo, const QVariant& plane, const QString& source = {}) {
    const QSignalBlocker blocker(combo);
    const int index = combo->findData(QStringLiteral("auto"));
    if (index < 0) return;
    const auto label = source.isEmpty() ? combo->itemText(combo->findData(plane)) : source;
    combo->setItemText(index, QObject::tr("Automaticky — %1").arg(label));
    combo->setItemData(index, plane, Qt::UserRole + 1);
    sync_compact_work_plane_selector(combo);
}
inline void install_automatic_work_plane(QComboBox* combo, bool automatic) {
    const auto plane = combo->currentData();
    combo->addItem(QObject::tr("Automaticky"), QStringLiteral("auto"));
    update_automatic_work_plane(combo, plane);
    if (automatic) combo->setCurrentIndex(combo->count() - 1);
    combo->setToolTip(QObject::tr("Automaticky: celý Počátek používá XY, jinak první rovinnou referenci. Výběr XY/XZ/YZ uloží ruční volbu v místních souřadnicích kontejneru."));
}

// Compact common work-plane control.  The original combo remains the single
// data owner (and keeps its established object name/API), while the visible
// row offers direct one-click presets plus the ordinary View-reference route.
inline QWidget* compact_work_plane_selector(QComboBox* combo,
        std::function<void()> request_view_plane = {}) {
    auto* selector = new QWidget(combo->parentWidget());
    selector->setObjectName(combo->objectName()+QStringLiteral("Selector"));
    auto* layout = new QHBoxLayout(selector);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(1);
    auto* group = new QButtonGroup(selector);
    group->setExclusive(true);
    const bool axes = combo->findText(QStringLiteral("XY"),
        Qt::MatchExactly) < 0 && combo->findText(QStringLiteral("X"),
        Qt::MatchExactly) >= 0;
    const std::array plane_labels{QStringLiteral("XY"),
        QStringLiteral("XZ"),QStringLiteral("YZ")};
    const std::array axis_labels{QStringLiteral("X"),
        QStringLiteral("Y"),QStringLiteral("Z")};
    for(int slot=0;slot<3;++slot) {
        const auto label=axes?axis_labels[slot]:plane_labels[slot];
        int index=combo->findText(label,Qt::MatchExactly);
        if(index<0)index=combo->findText(label,Qt::MatchContains);
        auto* button=new QToolButton(selector);
        button->setObjectName(combo->objectName()+QStringLiteral("Choice")+label);
        button->setText(label);button->setCheckable(true);
        button->setAutoRaise(false);button->setFixedWidth(22);
        button->setProperty("workPlaneData",index>=0?combo->itemData(index):QVariant{});
        button->setStyleSheet(command_button_style());
        group->addButton(button,slot);layout->addWidget(button);
        QObject::connect(button,&QToolButton::clicked,combo,[combo,index] {
            if(index>=0)combo->setCurrentIndex(index);
        });
    }
    auto* view = new QTableWidget(1,2,selector);
    view->setObjectName(combo->objectName()+QStringLiteral("View"));
    view->setToolTip(QObject::tr("Vybrat rovinu ve View"));
    view->horizontalHeader()->hide();view->verticalHeader()->hide();
    view->setSelectionMode(QAbstractItemView::NoSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setFixedHeight(30);view->setMinimumWidth(30);
    view->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    view->setColumnWidth(0,32);
    view->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    ui::install_reference_cell_delegate(view);
    auto* field=new ui::ReferenceCellItem;
    field->setText(QObject::tr("Ve View…"));view->setItem(0,1,field);
    auto* indicator=ui::build_reference_row_indicator({});
    view->setCellWidget(0,0,ui::centered_cell_widget(indicator));
    ui::set_reference_row_populated(indicator,false);
    QObject::connect(view,&QTableWidget::cellClicked,selector,
        [request=std::move(request_view_plane)](int,int column) {
            if(column==1&&request)request();
        });
    layout->addWidget(view,1);
    // Keep the established combo available to code and automated contracts,
    // but do not spend horizontal space on a second plane selector.
    combo->hide();layout->addWidget(combo);
    QObject::connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),
        selector,[combo](int){sync_compact_work_plane_selector(combo);});
    sync_compact_work_plane_selector(combo);
    return selector;
}
}

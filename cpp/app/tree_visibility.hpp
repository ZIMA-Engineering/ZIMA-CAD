#pragma once
#include "tree_reference_state.hpp"
#include <QTreeWidgetItem>
#include <QBrush>
#include <QColor>

namespace zima::app {
// Display only: keep selection, reference errors and suppression fonts intact.
inline void shade_hidden_tree_geometry(QTreeWidgetItem* item,bool hidden=true) {
    if(!item||item->data(0,Qt::UserRole+3).toString().endsWith("insert-here"))return;
    if(!item->data(0,missing_reference_role).toBool())item->setForeground(0,hidden?QBrush(QColor(125,125,125)):QBrush{});
    for(int i=0;i<item->childCount();++i)shade_hidden_tree_geometry(item->child(i),hidden);
}
} // namespace zima::app

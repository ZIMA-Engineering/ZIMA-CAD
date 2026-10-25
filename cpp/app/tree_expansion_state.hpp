#pragma once
#include <QSignalBlocker>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <map>

namespace zima::app {
using TreeExpansionState=std::map<QStringList,bool>;
inline QStringList tree_expansion_key(QTreeWidgetItem* item) {
    QStringList key;
    for(;item;item=item->parent()) {
        key.prepend(item->data(0,Qt::UserRole+1).toString());
        key.prepend(item->data(0,Qt::UserRole).toString());
        key.prepend(item->data(0,Qt::UserRole+3).toString());
    }
    return key;
}
inline TreeExpansionState capture_tree_expansion(QTreeWidget* tree) {
    TreeExpansionState state;
    for(QTreeWidgetItemIterator item(tree);*item;++item)state.emplace(tree_expansion_key(*item),(*item)->isExpanded());
    return state;
}
inline void restore_tree_expansion(QTreeWidget* tree,const TreeExpansionState& state) {
    const QSignalBlocker blocker(tree);
    for(QTreeWidgetItemIterator item(tree);*item;++item)
        if(const auto found=state.find(tree_expansion_key(*item));found!=state.end())(*item)->setExpanded(found->second);
}
} // namespace zima::app

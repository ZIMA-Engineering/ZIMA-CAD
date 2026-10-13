#pragma once

#include <QAbstractItemView>
#include <QIcon>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <algorithm>

namespace zima::app {
inline constexpr int file_preview_column=1;
inline constexpr int file_parameter_first_column=5;
struct FileEntryGeometry {
    QStyleOptionViewItem content;
    QRect thumbnail;
};
// Painting, hover and clicks share the Preview cell, independently of filename
// width. The filename's type decoration remains compact.
inline FileEntryGeometry file_entry_geometry(QStyleOptionViewItem option,
    const QModelIndex& index,const QAbstractItemView* view){
    if(index.column()==0)option.decorationSize={20,20};
    FileEntryGeometry result{option,{}};
    if(index.column()!=file_preview_column)return result;
    result.content.icon={};result.content.text.clear();
    result.content.features&=~(QStyleOptionViewItem::HasDisplay|QStyleOptionViewItem::HasDecoration|QStyleOptionViewItem::HasCheckIndicator);
    if(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).isNull())return result;
    const int width=std::min(view->iconSize().width(),std::max(0,option.rect.width()-4));
    const int height=std::min(view->iconSize().height(),std::max(0,option.rect.height()-4));
    if(width<12||height<12)return result;
    result.thumbnail={option.rect.center().x()-width/2,option.rect.center().y()-height/2,width,height};
    return result;
}
}

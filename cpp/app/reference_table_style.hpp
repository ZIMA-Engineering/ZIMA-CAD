#pragma once

#include <zima/ui/reference_cell.hpp>
#include <QHeaderView>
#include <QTableWidget>

namespace zima::app {
// Presentation only. Each command retains ownership of row deletion, entry,
// inspection and any protected rows; this helper never edits model data.
inline void style_reference_table(QTableWidget* table, int action_column,
                                  int reference_column) {
    table->verticalHeader()->show();
    table->verticalHeader()->setMinimumSectionSize(34);
    table->verticalHeader()->setDefaultSectionSize(34);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(action_column,QHeaderView::Fixed);
    table->setColumnWidth(action_column,34);
    table->horizontalHeader()->moveSection(table->horizontalHeader()->visualIndex(action_column),0);
    table->horizontalHeader()->setSectionResizeMode(reference_column,QHeaderView::Stretch);
    ui::install_reference_cell_delegate(table);
}
inline void append_face_reference_entry(QTableWidget* table) {
    const int row=table->rowCount();table->insertRow(row);
    auto* field=new ui::ReferenceCellItem(QObject::tr("Vyberte plochu…"));
    field->setForeground(QColor("#4dd811"));
    field->set_active_input(table->property("referenceEntryActive").toBool());
    table->setItem(row,1,field);
    table->setCellWidget(row,0,ui::centered_cell_widget(ui::build_reference_row_indicator({})));
}
inline void set_face_reference_entry_active(QTableWidget* table,bool active) {
    table->setProperty("referenceEntryActive",active);
    if(auto* field=dynamic_cast<ui::ReferenceCellItem*>(table->item(table->rowCount()-1,1)))
        field->set_active_input(active);
    table->viewport()->update();
}
}

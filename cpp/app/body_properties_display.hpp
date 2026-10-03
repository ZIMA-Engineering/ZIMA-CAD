#pragma once
#include <zima/document/body_properties.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

namespace zima::app {
inline QString body_properties_centroid_tooltip(const document::BodyProperties& row,
    const document::PartDocument& doc,int decimals) {
    const auto centroid=row.centroid();if(!centroid)return {};
    const auto& unit=doc.document_units.at("Length");const double scale=document::length_unit_mm(unit);
    const auto number=[&](double v){return QString::fromStdString(kernel::dimension_number(v/scale,decimals));};
    return QStringLiteral("X: %1; Y: %2; Z: %3 %4").arg(number(centroid->x),number(centroid->y),number(centroid->z),QString::fromStdString(unit));
}
// Metadata changes keep the existing Tree and selection. Update only its text;
// no analysis record, scene, reference or body calculation is rebuilt.
inline void refresh_body_properties_tooltips(QTreeWidget& tree,const document::PartDocument& doc,int decimals) {
    if(doc.body_properties.empty())return;
    const auto context=QString::fromStdString(doc.document_id+":"+doc.document_units.at("Length"))+":"+QString::number(decimals);
    if(tree.property("zimaCentroidTooltipContext").toString()==context)return;
    tree.setProperty("zimaCentroidTooltipContext",context);
    std::map<QString,QString> text;
    for(const auto& row:doc.body_properties)text.emplace(QString::fromStdString(row.id),body_properties_centroid_tooltip(row,doc,decimals));
    for(QTreeWidgetItemIterator it(&tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="body-properties-origin") {
        const auto found=text.find((*it)->data(0,Qt::UserRole+5).toString());
        if(found!=text.end())(*it)->setToolTip(0,found->second);
    }
}
} // namespace zima::app

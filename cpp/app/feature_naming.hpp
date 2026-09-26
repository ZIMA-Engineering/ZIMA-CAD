#pragma once
#include <zima/document/part_document.hpp>
#include <QObject>
#include <QString>
#include <algorithm>

namespace zima::app {
inline QString feature_name_prefix(document::FeatureType type) {
    using document::FeatureType;
    switch(type) {
        case FeatureType::Point:return QObject::tr("Bod");
        case FeatureType::Axis:return QObject::tr("Osa");
        case FeatureType::Plane:return QObject::tr("Rovina");
        case FeatureType::Sketch:return QObject::tr("Skica");
        case FeatureType::Modeling:return QObject::tr("Vytažení");
    }
    return {};
}
inline std::string next_feature_name(const document::PartDocument& part,
        document::FeatureType type,const std::string& excluded_id) {
    const auto prefix=feature_name_prefix(type);
    for(std::size_t number=1;;++number) {
        const auto name=(prefix+" "+QString::number(number).rightJustified(3,'0')).toStdString();
        if(std::ranges::none_of(part.history,[&](const auto& item) {
            return item.id!=excluded_id && item.name==name;
        }) && std::ranges::none_of(part.constructions,[&](const auto& item) {
            return item.id!=excluded_id && item.name==name;
        }))return name;
    }
}
} // namespace zima::app

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
        case FeatureType::Modeling:return QObject::tr("Tažení");
    }
    return {};
}
inline std::string next_feature_name(const document::PartDocument& part,
        document::FeatureType type,const std::string& excluded_id) {
    return part.next_numbered_name(feature_name_prefix(type).toStdString(),excluded_id);
}
} // namespace zima::app

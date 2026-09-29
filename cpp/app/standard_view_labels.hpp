#pragma once
#include <QObject>
#include <QString>
#include <string_view>

namespace zima::app {
inline QString standard_view_label(std::string_view key) {
    if(key=="default")return QObject::tr("Výchozí – izometrický");
    if(key=="front")return QObject::tr("Front – XZ");
    if(key=="back")return QObject::tr("Back – XZ opačně");
    if(key=="left")return QObject::tr("Left – YZ");
    if(key=="right")return QObject::tr("Right – YZ opačně");
    if(key=="top")return QObject::tr("Top – XY");
    if(key=="bottom")return QObject::tr("Bottom – XY opačně");
    return QString::fromUtf8(key.data(),static_cast<qsizetype>(key.size()));
}
}

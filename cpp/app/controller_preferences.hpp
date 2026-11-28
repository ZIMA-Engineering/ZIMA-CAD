#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <array>
#include <algorithm>
#include <cmath>

namespace zima::app::controller {
enum class Context { Model, Sketch, Drawing };
inline QString context_key(Context context) {
    return context==Context::Sketch?QStringLiteral("sketch"):
        context==Context::Drawing?QStringLiteral("drawing"):QStringLiteral("model");
}
struct Profile {
    double pan_speed{1},rotation_speed{1},zoom_speed{1},dead_zone{.04};
    bool rotation{true};
    std::array<bool,6> inverted{};
    QMap<int,QString> buttons{{1,QStringLiteral("fitViewAction")}};
    bool operator==(const Profile&)const=default;
};
struct Preferences {
    bool enabled{true};
    QString selected_device; // Empty selects the first available HID controller.
    QString navigo_port; // Only an explicitly selected port may be opened.
    double navigo_range{100};
    QMap<QString,Profile> profiles;
    [[nodiscard]] Profile profile(const QString& device,Context context)const {
        const auto key=device+"/"+context_key(context);
        if(profiles.contains(key))return profiles.value(key);
        Profile result;result.rotation=context==Context::Model;return result;
    }
    void set_profile(const QString& device,Context context,const Profile& profile) {
        profiles[device+"/"+context_key(context)]=profile;
    }
    bool operator==(const Preferences&)const=default;
};
inline double bounded_number(const QJsonValue& value,double fallback,double minimum,double maximum) {
    const double number=value.toDouble(fallback);
    return std::isfinite(number)&&number>=minimum&&number<=maximum?number:fallback;
}
inline QString encode(const Preferences& preferences) {
    QJsonObject profiles;
    for(auto it=preferences.profiles.cbegin();it!=preferences.profiles.cend();++it) {
        const auto& p=it.value();QJsonArray inverted;for(bool flag:p.inverted)inverted.append(flag);
        QJsonObject buttons;for(auto b=p.buttons.cbegin();b!=p.buttons.cend();++b)buttons[QString::number(b.key())]=b.value();
        profiles[it.key()]=QJsonObject{{"pan",p.pan_speed},{"rotationSpeed",p.rotation_speed},
            {"zoom",p.zoom_speed},{"deadZone",p.dead_zone},{"rotation",p.rotation},
            {"inverted",inverted},{"buttons",buttons}};
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"enabled",preferences.enabled},
        {"device",preferences.selected_device},{"navigoPort",preferences.navigo_port},
        {"navigoRange",preferences.navigo_range},{"profiles",profiles}}).toJson(QJsonDocument::Compact));
}
inline Preferences decode(const QString& encoded) {
    Preferences result;
    if(encoded.size()>262144)return result;
    const auto root=QJsonDocument::fromJson(encoded.toUtf8()).object();
    if(root.value("schema").toInt()!=1)return result;
    result.enabled=root.value("enabled").toBool(true);
    result.selected_device=root.value("device").toString().left(512);
    result.navigo_port=root.value("navigoPort").toString().left(512);
    result.navigo_range=bounded_number(root.value("navigoRange"),100,.01,1000000);
    const auto profiles=root.value("profiles").toObject();
    for(auto it=profiles.begin();it!=profiles.end()&&result.profiles.size()<128;++it) {
        if(it.key().size()>600)continue;
        const auto object=it.value().toObject();Profile p;
        p.pan_speed=bounded_number(object.value("pan"),1,.05,10);
        p.rotation_speed=bounded_number(object.value("rotationSpeed"),1,.05,10);
        p.zoom_speed=bounded_number(object.value("zoom"),1,.05,10);
        p.dead_zone=bounded_number(object.value("deadZone"),.04,0,.5);
        p.rotation=object.value("rotation").toBool(it.key().endsWith("/model"));
        const auto inverted=object.value("inverted").toArray();
        for(int i=0;i<6&&i<inverted.size();++i)p.inverted[i]=inverted[i].toBool();
        if(object.contains("buttons"))p.buttons.clear();
        const auto buttons=object.value("buttons").toObject();
        for(auto b=buttons.begin();b!=buttons.end();++b) {
            bool ok=false;const int id=b.key().toInt(&ok);const auto command=b.value().toString();
            if(ok&&id>0&&id<=128&&command.size()<=160)p.buttons[id]=command;
        }
        result.profiles[it.key()]=std::move(p);
    }
    return result;
}
} // namespace zima::app::controller

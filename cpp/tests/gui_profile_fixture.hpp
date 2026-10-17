#pragma once
#include "profile_command_fixture.hpp"
#include <zima/document/part_document.hpp>
#include <QString>
namespace zima::test {
inline document::HistoryContainer rectangular_default(document::PartDocument& part,
        kernel::Vec3 size={10,10,10}) {
    auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(-size.x/2,-size.y/2,size.x/2,size.y/2));
    auto feature=document::PartDocument::create_feature_container(sketch.id);
    feature.feature.symmetric=true;feature.feature.sides[0].length=size.z/2;
    sketch.owner_container_id=feature.id;part.sketches.push_back(std::move(sketch));
    return feature;
}
template<class Window>
commands::Result gui_rectangular_profile(Window& window,double x,double y,double z) {
    return create_rectangular_profile([&](const commands::Json& request) {
        return window.execute_console_command(QString::fromStdString(request.dump()));
    },x,y,z);
}
}

#pragma once
#include "resource_icon.hpp"
#include <QAction>

namespace zima::app {
inline void update_fem_animation_action(QAction* action,bool running) {
    if(!action)return;
    action->setText(running?QObject::tr("Pause deformation"):QObject::tr("Play deformation"));
    action->setIcon(resource_icon(running?"media-pause":"media-play"));
    action->setChecked(running);
}
}

#pragma once
#include "profile_command_fixture.hpp"
#include <QString>
namespace zima::test {
template<class Window>
commands::Result gui_rectangular_profile(Window& window,double x,double y,double z) {
    return create_rectangular_profile([&](const commands::Json& request) {
        return window.execute_console_command(QString::fromStdString(request.dump()));
    },x,y,z);
}
}

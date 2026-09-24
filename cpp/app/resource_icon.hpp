#pragma once

#include <QIcon>
#include <QString>

namespace zima::app {

[[nodiscard]] QIcon resource_icon(const QString& name, bool surface = false);
[[nodiscard]] QIcon application_icon();
void install_dialog_button_icons();

}  // namespace zima::app

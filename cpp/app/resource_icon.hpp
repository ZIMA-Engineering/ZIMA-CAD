#pragma once

#include <QIcon>
#include <QString>

namespace zima::app {

[[nodiscard]] QIcon resource_icon(const QString& name, bool surface = false);
[[nodiscard]] QIcon resource_tree_icon(const QString& name, bool surface = false);
// Type choices retain the ordinary theme foreground when their row is selected.
[[nodiscard]] QIcon resource_foreground_icon(const QString& name);
[[nodiscard]] QIcon application_icon();
[[nodiscard]] QIcon system_application_icon();
[[nodiscard]] QImage application_wordmark(const QColor& color);
void install_dialog_button_icons();

}  // namespace zima::app

#pragma once

#include <QIcon>
#include <QString>

namespace zima::app {

[[nodiscard]] QIcon resource_icon(const QString& name);
[[nodiscard]] QIcon application_icon();

}  // namespace zima::app

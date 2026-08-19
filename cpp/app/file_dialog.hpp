#pragma once

#include <QString>

class QWidget;

namespace zima::app {

QString open_file(QWidget* parent, const QString& caption,
                  const QString& initial_path, const QString& name_filter);
QString save_file(QWidget* parent, const QString& caption,
                  const QString& initial_path, const QString& name_filter,
                  const QString& default_suffix = {});

}  // namespace zima::app

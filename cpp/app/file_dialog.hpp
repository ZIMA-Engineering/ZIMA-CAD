#pragma once

#include <QString>
#include <QMap>

class QWidget;

namespace zima::app {

QString open_file(QWidget* parent, const QString& caption,
                  const QString& initial_path, const QString& name_filter,
                  const QMap<QString, QString>& translations = {});
QString save_file(QWidget* parent, const QString& caption,
                  const QString& initial_path, const QString& name_filter,
                  const QString& default_suffix = {},
                  const QMap<QString, QString>& translations = {});
QString choose_directory(QWidget* parent, const QString& caption,
                         const QString& initial_path,
                         const QMap<QString, QString>& translations = {});

}  // namespace zima::app

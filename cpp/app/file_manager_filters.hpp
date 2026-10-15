#pragma once
#include <QDir>
#include <QMap>
#include <QRegularExpression>
#include <QSettings>
#include <QVariant>
#include <algorithm>
#include <functional>

namespace zima::app {
inline QStringList file_pattern_list(const QVariant &value) {
  QStringList result;
  for (const auto &pattern : value.toStringList())
    if (!pattern.trimmed().isEmpty())
      result << pattern.trimmed();
  return result;
}
inline QString file_filters_path(const QString &directory) {
  return QDir(directory).filePath("0000-index/filters.ini");
}
inline QMap<QString, QVariant> local_file_filters(const QString &directory) {
  QSettings file(file_filters_path(directory), QSettings::IniFormat);
  file.setFallbacksEnabled(false);
  QMap<QString, QVariant> values;
  for (const auto &key : file.allKeys())
    values.insert(key, file.value(key));
  return values;
}
inline QStringList effective_file_filters(const QMap<QString, QVariant> &local,
                                          const QStringList &global) {
  auto result = local.value("Filters/InheritGlobal", true).toBool()
                    ? global
                    : QStringList{};
  for (const auto &pattern : file_pattern_list(local.value("Filters/Hide")))
    if (!result.contains(pattern))
      result << pattern;
  return result;
}
inline std::function<bool(const QString &)>
compile_file_filters(const QStringList &hidden) {
  QList<QRegularExpression> patterns;
  for (const auto &pattern : hidden)
    if (!pattern.trimmed().isEmpty())
      patterns.emplaceBack(
          QRegularExpression::wildcardToRegularExpression(pattern.trimmed()),
          QRegularExpression::CaseInsensitiveOption);
  return [patterns = std::move(patterns)](const QString &name) {
    return !std::ranges::any_of(patterns, [&](const auto &pattern) {
      return pattern.match(name).hasMatch();
    });
  };
}
inline std::function<bool(const QString &)>
directory_file_filter(const QString &directory, const QStringList &global) {
  return compile_file_filters(
      effective_file_filters(local_file_filters(directory), global));
}
} // namespace zima::app

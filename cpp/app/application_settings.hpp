#pragma once

#include <QMap>
#include <QString>

namespace zima::app {

struct ApplicationSettings {
    QString language{QStringLiteral("cs")};
    QString config_path;
    QString base_config_path;
    QString local_config_path;
    QMap<QString, QString> configured_paths;
    QMap<QString, QString> resolved_paths;
    QMap<QString, QString> units;
    QMap<QString, QString> translations;
    QString part_template{QStringLiteral("start_part.prtz")};

    [[nodiscard]] static ApplicationSettings load();
    [[nodiscard]] bool save(QString* error = nullptr) const;
    [[nodiscard]] QString text(const QString& key, const QString& fallback) const;
};

}  // namespace zima::app

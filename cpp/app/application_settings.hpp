#pragma once

#include <QMap>
#include <QString>

class QApplication;

namespace zima::app {

struct StartupContext {
    QString working_directory;
    QString document_path;
    QString local_config_path;
};

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
    QString assembly_template{QStringLiteral("start_assembly.asmz")};
    bool use_iso_application_font{true};

    [[nodiscard]] static ApplicationSettings load(
        const QString& working_directory = {});
    [[nodiscard]] bool save(QString* error = nullptr) const;
    [[nodiscard]] QString text(const QString& key, const QString& fallback) const;
};

void apply_application_font(QApplication& application,
    const ApplicationSettings& settings);

}  // namespace zima::app

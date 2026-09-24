#pragma once

#include <QMap>
#include <QString>
#include "../common/document_naming.hpp"

class QApplication;

namespace zima::app {

struct StartupContext {
    QString working_directory;
    QString document_path;
    QString local_config_path;
};

struct ApplicationSettings {
    DocumentNaming document_naming;
    QString language{QStringLiteral("cs")};
    QString config_path;
    QString base_config_path;
    QString local_config_path;
    QString platform_config_path;
    QString installation_root;
    QMap<QString, QString> initial_configured_paths;
    QMap<QString, QString> configured_paths;
    QMap<QString, QString> resolved_paths;
    QMap<QString, QString> units;
    QMap<QString, QString> translations;
    QMap<QString, QString> qt_translations;
    QString part_template{QStringLiteral("START_PART.prtz")};
    QString assembly_template{QStringLiteral("START_ASSEMBLY.asmz")};
    QString drawing_pdf_directory{QStringLiteral("pdf")};
    QString drawing_dxf_directory{QStringLiteral("export")};
    QString drawing_view_style{QStringLiteral("hidden_edges")};
    bool use_iso_application_font{false};
    bool stacked_tolerances{};
    double sheet_cut_tolerance{0.05};

    [[nodiscard]] static ApplicationSettings load(
        const QString& working_directory = {}, const QString& executable = {});
    [[nodiscard]] bool save(QString* error = nullptr) const;
    [[nodiscard]] QString text(const QString& key, const QString& fallback) const;
};

void apply_application_translations(QApplication& application,
    const ApplicationSettings& settings);

void apply_application_font(QApplication& application,
    const ApplicationSettings& settings);

}  // namespace zima::app

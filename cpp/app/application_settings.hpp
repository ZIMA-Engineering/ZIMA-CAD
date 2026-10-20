#pragma once

#include <QMap>
#include <QVector>
#include <zima/document/sweep_precision.hpp>
#include <QString>
#include <QStringList>
#include "../common/document_naming.hpp"
#include "../browser/preferences.hpp"

class QApplication;

namespace zima::app {

struct FileManagerSource {
    QString id;
    QString name;
    QString path;
    QString resolved_path;
    bool enabled{true};
    bool read_only{true};
    bool factory{};
};

struct FileManagerPreferences {
    QVector<FileManagerSource> sources;
    QString company_name{QStringLiteral("ZIMA-Engineering")};
    QString company_logo;
    QString company_website{QStringLiteral("https://zima-engineering.cz")};
    QString default_view{QStringLiteral("files")};
    int thumbnail_size{144};
    bool show_hidden{};
    QStringList hidden_patterns{QStringLiteral("0000-index")};
};

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
    QString part_template{QStringLiteral("START_PART_mm.prtz")};
    QString assembly_template{QStringLiteral("START_ASSEMBLY_mm.asmz")};
    QString skeleton_template{QStringLiteral("START_SKELETON_mm.prtz")};
    QString drawing_pdf_directory{QStringLiteral("pdf")};
    QString drawing_dxf_directory{QStringLiteral("export")};
    QString drawing_view_style{QStringLiteral("hidden_edges")};
    QString theme{QStringLiteral("light")};
    bool use_iso_application_font{false};
    bool stacked_tolerances{};
    bool vladimir{true};
    browser::Preferences browser;
    FileManagerPreferences file_manager;
    double sheet_cut_tolerance{0.05};
    document::SweepPrecisionDefaults sweep_precision_defaults;

    [[nodiscard]] static ApplicationSettings load(
        const QString& working_directory = {}, const QString& executable = {});
    [[nodiscard]] bool save(QString* error = nullptr) const;
    [[nodiscard]] QString text(const QString& key, const QString& fallback) const;
};

void apply_application_translations(QApplication& application,
    const ApplicationSettings& settings);

void apply_application_appearance(QApplication& application,
    const ApplicationSettings& settings);

void apply_application_font(QApplication& application,
    const ApplicationSettings& settings);

}  // namespace zima::app

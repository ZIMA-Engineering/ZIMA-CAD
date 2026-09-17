#include "application_settings.hpp"
#include <zima/document/precision.hpp>
#include "../common/installation.hpp"
#include <memory>
#include <iostream>
#include <vector>

#include <QCoreApplication>
#include <QApplication>
#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTextStream>
#include <QSettings>
#include <QScopeGuard>
#include <QTranslator>

namespace zima::app {
namespace {

// Source-text translations share the configured INI catalogue with keyed UI text.
class IniTranslator final : public QTranslator {
public:
    IniTranslator(QMap<QString, QString> messages, QObject* parent)
        : QTranslator(parent), messages_(std::move(messages)) {}
    QString translate(const char* context, const char* source,
                      const char* = nullptr, int n = -1) const override {
        if (!source || n >= 0) return {}; // Numerus messages require a plural-aware catalogue.
        const auto text = QString::fromUtf8(source);
        const auto specific = messages_.constFind(
            QString::fromUtf8(context ? context : "") + "|" + text);
        if (specific != messages_.cend()) return *specific;
        return messages_.value(text);
    }
    bool isEmpty() const override { return messages_.isEmpty(); }
private:
    QMap<QString, QString> messages_;
};

const QStringList path_keys{
    QStringLiteral("Materials"), QStringLiteral("Templates"),
    QStringLiteral("Formats"), QStringLiteral("Localization"),
    QStringLiteral("WorkingDirectory")};

const QMap<QString, QString> path_defaults{
    {QStringLiteral("Materials"), QStringLiteral("materials")},
    {QStringLiteral("Templates"), QStringLiteral("templates")},
    {QStringLiteral("Formats"), QStringLiteral("formats")},
    {QStringLiteral("Localization"), QStringLiteral("localization")},
    {QStringLiteral("WorkingDirectory"), QStringLiteral("../Projects")}};

const QMap<QString, QString> unit_defaults{
    {QStringLiteral("Length"), QStringLiteral("mm")},
    {QStringLiteral("Angle"), QStringLiteral("deg")},
    {QStringLiteral("Mass"), QStringLiteral("kg")},
    {QStringLiteral("Time"), QStringLiteral("s")},
    {QStringLiteral("Temperature"), QStringLiteral("C")},
    {QStringLiteral("Stress"), QStringLiteral("MPa")}};

QString locate_base_config_path() {
    const QString cwd_config = QDir::current().absoluteFilePath("config/config.ini");
    if (QFileInfo::exists(cwd_config)) return QFileInfo(cwd_config).canonicalFilePath();
    const QDir executable(QCoreApplication::applicationDirPath());
    const QString installed = executable.absoluteFilePath("config/config.ini");
    if (QFileInfo::exists(installed)) return QFileInfo(installed).canonicalFilePath();
    const QString source_tree = executable.absoluteFilePath("../../config/config.ini");
    if (QFileInfo::exists(source_tree)) return QFileInfo(source_tree).canonicalFilePath();
    return QDir::cleanPath(cwd_config);
}

QString resolved_path(const QString& config_path, const QString& value) {
    const QString portable = QDir::fromNativeSeparators(value.trimmed());
    if (QDir::isAbsolutePath(portable)) return QDir::cleanPath(portable);
    return QDir(QFileInfo(config_path).absolutePath()).absoluteFilePath(portable);
}

QString next_archive_path(const QString& target) {
    int version = 1;
    QString archive;
    do {
        archive = QStringLiteral("%1.%2").arg(target).arg(version++);
    } while (QFileInfo::exists(archive));
    return archive;
}

}  // namespace

ApplicationSettings ApplicationSettings::load(const QString& working_directory, const QString& executable) {
    ApplicationSettings result;
    const auto qpath = [](const std::filesystem::path& p) {
        const auto bytes = p.generic_u8string();
        return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    };
    const auto installed = distribution::locate_installation(std::filesystem::u8path(
        (executable.isEmpty() ? QCoreApplication::applicationFilePath() : executable).toStdString()));
    QStringList paths;
    if (installed) {
        for (const auto& p : distribution::config_layers(*installed)) paths.push_back(qpath(p));
        result.base_config_path = paths.front();
        result.config_path = qpath(installed->config);
        result.platform_config_path = qpath(installed->platform_config);
        result.installation_root = qpath(installed->root);
    } else {
        result.base_config_path = locate_base_config_path();
        result.config_path = result.base_config_path;
        paths.push_back(result.base_config_path);
    }
    const QDir startup_directory(working_directory.trimmed().isEmpty() ? QDir::currentPath() : working_directory);
    const QString local_candidate = startup_directory.absoluteFilePath("config.ini");
    if (QFileInfo::exists(local_candidate) &&
        !paths.contains(QFileInfo(local_candidate).canonicalFilePath()) &&
        !paths.contains(QFileInfo(local_candidate).absoluteFilePath())) {
        result.local_config_path = QFileInfo(local_candidate).canonicalFilePath();
        result.config_path = result.local_config_path;
        result.platform_config_path.clear();
        paths.push_back(result.local_config_path);
    }
    std::vector<std::unique_ptr<QSettings>> layers;
    for (const auto& p : paths) layers.push_back(std::make_unique<QSettings>(p, QSettings::IniFormat));
    const auto value = [&](const QString& key, const QString& fallback, QString* origin = nullptr) {
        for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
            const auto text = (*it)->value(key).toString().trimmed();
            if (!text.isEmpty()) {
                if (origin) *origin = (*it)->fileName();
                return text;
            }
        }
        if (origin) *origin = result.base_config_path;
        return fallback;
    };
    result.language = value("Application/Language", "cs");
    result.use_iso_application_font = value("Application/UseISOFont", "true").toLower() != "false";
    try {
        result.sheet_cut_tolerance=zima::document::sheet_cut_tolerance({
            {"sheet_cut_tolerance",value("SheetMetal/CutTolerance","0.05").toStdString()}});
    } catch(const std::invalid_argument& error) {
        result.sheet_cut_tolerance=.05;
        std::cerr<<"Warning: invalid SheetMetal/CutTolerance; using 0.05 mm. "<<error.what()<<'\n';
    }
    for (const auto& key : path_keys) {
        QString origin;
        const auto fallback = installed && key == "WorkingDirectory" ? qpath(installed->root / "Projects") : path_defaults.value(key);
        const auto configured = value("Paths/" + key, fallback, &origin);
        const auto resolved = resolved_path(origin, configured);
        result.resolved_paths.insert(key, resolved);
        // Display relative paths against the writable config, not a hidden lower layer.
        const auto displayed = installed ? QDir(QFileInfo(result.config_path).absolutePath()).relativeFilePath(resolved) : configured;
        result.configured_paths.insert(key, displayed);
    }
    result.initial_configured_paths = result.configured_paths;
    for (auto it = unit_defaults.cbegin(); it != unit_defaults.cend(); ++it)
        result.units.insert(it.key(), value("Units/" + it.key(), it.value()));
    result.part_template = value("Templates/Part", "start_part.prtz");
    result.assembly_template = value("Templates/Assembly", "start_assembly.asmz");
    QFile translations(QDir(result.resolved_paths.value("Localization"))
                           .absoluteFilePath(result.language + ".ini"));
    if (translations.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&translations);
        QString section;
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.startsWith('[') && line.endsWith(']')) {
                section = line.mid(1, line.size() - 2);
                continue;
            }
            const int equals = line.indexOf('=');
            if (equals <= 0 || line.trimmed().startsWith('[') ||
                line.trimmed().startsWith('#') || line.trimmed().startsWith(';')) {
                continue;
            }
            auto* messages = section == "Translations" ? &result.translations
                : section == "QtTranslations" ? &result.qt_translations : nullptr;
            if (messages) messages->insert(
                line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }
    return result;
}

QString ApplicationSettings::text(
    const QString& key, const QString& fallback) const {
    return translations.value(key, fallback);
}

static bool save_values(const QString& config_path, const QMap<QString, QVariant>& values, QString* error) {
    const QFileInfo target_info(config_path);
    if (!target_info.absoluteDir().exists() &&
        !QDir().mkpath(target_info.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Konfigurační adresář nelze vytvořit: %1")
                         .arg(target_info.absolutePath());
        }
        return false;
    }

    QString temporary_path;
    {
        QTemporaryFile temporary(target_info.absolutePath() + QStringLiteral("/.") +
            target_info.fileName() + QStringLiteral(".XXXXXX.tmp"));
        if (!temporary.open()) {
            if (error != nullptr) *error = QStringLiteral("Dočasný konfigurační soubor nelze vytvořit: %1").arg(config_path);
            return false;
        }
        temporary_path = temporary.fileName();
        temporary.setAutoRemove(false);
        // Destroy the native temporary handle before QSettings writes this file.
    }
    const auto cleanup = qScopeGuard([&] { QFile::remove(temporary_path); });

    QSettings output(temporary_path, QSettings::IniFormat);
    // Only this disposable staging file may be written directly. The actual
    // user configuration is replaced atomically by QSaveFile below.
    output.setAtomicSyncRequired(false);
    if (QFileInfo::exists(config_path)) {
        QSettings source(config_path, QSettings::IniFormat);
        for (const auto& key : source.allKeys())
            output.setValue(key, source.value(key));
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        output.setValue(it.key(), it.value());
    output.sync();
    if (output.status() != QSettings::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("Konfiguraci nelze připravit: %1")
                         .arg(config_path);
        }
        QFile::remove(temporary_path);
        return false;
    }

    QFile temporary_file(temporary_path);
    if (!temporary_file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Konfiguraci nelze ověřit: %1")
                         .arg(config_path);
        }
        QFile::remove(temporary_path);
        return false;
    }
    const QByteArray contents = temporary_file.readAll();
    temporary_file.close();
    QSettings validator(temporary_path, QSettings::IniFormat);
    if (validator.status() != QSettings::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("Konfigurace není platný INI soubor: %1")
                         .arg(config_path);
        }
        QFile::remove(temporary_path);
        return false;
    }

    QString archive_path;
    if (QFileInfo::exists(config_path)) {
        archive_path = next_archive_path(config_path);
        if (!QFile::copy(config_path, archive_path)) {
            if (error != nullptr) {
                *error = QStringLiteral("Nelze vytvořit zálohu konfigurace: %1")
                             .arg(archive_path);
            }
            QFile::remove(temporary_path);
            return false;
        }
    }

    QSaveFile replacement(config_path);
    if (!replacement.open(QIODevice::WriteOnly) ||
        replacement.write(contents) != contents.size() ||
        !replacement.commit()) {
        if (!archive_path.isEmpty()) QFile::remove(archive_path);
        QFile::remove(temporary_path);
        if (error != nullptr) {
            *error = QStringLiteral("Konfiguraci nelze atomicky uložit: %1")
                         .arg(config_path);
        }
        return false;
    }
    return true;
}

bool ApplicationSettings::save(QString* error) const {
    try {zima::document::validate_sheet_cut_tolerance(sheet_cut_tolerance);}
    catch(const std::exception& issue){if(error)*error=QString::fromUtf8(issue.what());return false;}
    QMap<QString, QVariant> common{
        {"Application/Language", language}, {"Application/UseISOFont", use_iso_application_font},
        {"SheetMetal/CutTolerance",sheet_cut_tolerance},
        {"Templates/Part", part_template}, {"Templates/Assembly", assembly_template}};
    for (auto it = units.cbegin(); it != units.cend(); ++it) common.insert("Units/" + it.key(), it.value());
    QMap<QString, QVariant> paths;
    for (auto it = configured_paths.cbegin(); it != configured_paths.cend(); ++it) {
        if (!installation_root.isEmpty() && it.value() == initial_configured_paths.value(it.key())) continue;
        auto path = QDir::fromNativeSeparators(it.value().trimmed());
        if (!platform_config_path.isEmpty() && !path.isEmpty() && !QDir::isAbsolutePath(path))
            path = QDir(QFileInfo(platform_config_path).absolutePath()).relativeFilePath(resolved_path(config_path, path));
        paths.insert("Paths/" + it.key(), path);
    }
    if (platform_config_path.isEmpty()) common.insert(paths);
    if (platform_config_path.isEmpty() || paths.isEmpty()) return save_values(config_path, common, error);
    const bool existed = QFileInfo::exists(platform_config_path);
    QByteArray previous;
    if (existed) {
        QFile input(platform_config_path);
        if (!input.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Konfiguraci cest nelze přečíst: %1").arg(platform_config_path);
            return false;
        }
        previous = input.readAll();
        if (input.error() != QFile::NoError) return false;
    }
    if (!save_values(platform_config_path, paths, error)) return false;
    if (save_values(config_path, common, error)) return true;
    // A failed OK must not leave only the path half of the edit committed.
    bool restored = false;
    if (existed) {
        QSaveFile restore(platform_config_path);
        restored = restore.open(QIODevice::WriteOnly) && restore.write(previous) == previous.size() && restore.commit();
    } else restored = QFile::remove(platform_config_path);
    if (!restored && error) *error += QStringLiteral("\nZměny cest nelze vrátit: %1").arg(platform_config_path);
    return false;
}

void apply_application_translations(QApplication& application,
    const ApplicationSettings& settings) {
    constexpr auto name = "zimaIniTranslator";
    if (auto* previous = application.findChild<QTranslator*>(
            name, Qt::FindDirectChildrenOnly)) {
        application.removeTranslator(previous);
        delete previous;
    }
    auto* translator = new IniTranslator(settings.qt_translations, &application);
    translator->setObjectName(name);
    application.installTranslator(translator);
}

void apply_application_font(QApplication& application,
    const ApplicationSettings& settings) {
    if (!settings.use_iso_application_font) {
        application.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        return;
    }
    static int font_id = -2;
    if (font_id == -2) font_id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/zima/fonts/osifont-lgpl3fe.ttf"));
    if (font_id < 0) return;
    const auto families = QFontDatabase::applicationFontFamilies(font_id);
    if (families.empty()) return;
    auto font = application.font();
    font.setFamily(families.front());
    application.setFont(font);
}

}  // namespace zima::app

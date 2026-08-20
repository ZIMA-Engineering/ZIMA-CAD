#include "application_settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QSettings>

namespace zima::app {
namespace {

const QStringList path_keys{
    QStringLiteral("Materials"), QStringLiteral("Templates"),
    QStringLiteral("Formats"), QStringLiteral("Localization")};

const QMap<QString, QString> path_defaults{
    {QStringLiteral("Materials"), QStringLiteral("materials")},
    {QStringLiteral("Templates"), QStringLiteral("templates")},
    {QStringLiteral("Formats"), QStringLiteral("formats")},
    {QStringLiteral("Localization"), QStringLiteral("localization")}};

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

QString layered_value(const QSettings& base, const QSettings* local,
                      const QString& key, const QString& fallback) {
    if (local != nullptr) {
        const QString value = local->value(key).toString().trimmed();
        if (!value.isEmpty()) return value;
    }
    return base.value(key, fallback).toString();
}

}  // namespace

ApplicationSettings ApplicationSettings::load() {
    ApplicationSettings result;
    result.base_config_path = locate_base_config_path();
    const QString local_candidate = QDir::current().absoluteFilePath("config.ini");
    if (QFileInfo::exists(local_candidate) &&
        QFileInfo(local_candidate).canonicalFilePath() != result.base_config_path) {
        result.local_config_path = QFileInfo(local_candidate).canonicalFilePath();
    }
    result.config_path = result.local_config_path.isEmpty()
        ? result.base_config_path : result.local_config_path;

    QSettings base(result.base_config_path, QSettings::IniFormat);
    QSettings local(result.local_config_path, QSettings::IniFormat);
    const QSettings* local_layer = result.local_config_path.isEmpty() ? nullptr : &local;
    result.language = layered_value(base, local_layer, "Application/Language", "cs").trimmed();
    if (result.language.isEmpty()) result.language = QStringLiteral("cs");
    for (const auto& key : path_keys) {
        const QString setting_key = QStringLiteral("Paths/") + key;
        const QString configured = layered_value(
            base, local_layer, setting_key, path_defaults.value(key));
        result.configured_paths.insert(key, configured);
        const bool from_local = local_layer != nullptr &&
            !local.value(setting_key).toString().trimmed().isEmpty();
        result.resolved_paths.insert(key, resolved_path(
            from_local ? result.local_config_path : result.base_config_path, configured));
    }
    for (auto it = unit_defaults.cbegin(); it != unit_defaults.cend(); ++it) {
        result.units.insert(it.key(), layered_value(
            base, local_layer, QStringLiteral("Units/") + it.key(), it.value()));
    }
    result.part_template = layered_value(
        base, local_layer, "Templates/Part", "start_part.prtz");
    QFile translations(QDir(result.resolved_paths.value("Localization"))
                           .absoluteFilePath(result.language + ".ini"));
    if (translations.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&translations);
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            const int equals = line.indexOf('=');
            if (equals <= 0 || line.trimmed().startsWith('[') ||
                line.trimmed().startsWith('#') || line.trimmed().startsWith(';')) {
                continue;
            }
            result.translations.insert(
                line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }
    return result;
}

QString ApplicationSettings::text(
    const QString& key, const QString& fallback) const {
    return translations.value(key, fallback);
}

bool ApplicationSettings::save(QString* error) const {
    QString archive_path;
    if (QFileInfo::exists(config_path)) {
        int version = 1;
        do {
            archive_path = QStringLiteral("%1.%2").arg(config_path).arg(version++);
        } while (QFileInfo::exists(archive_path));
        if (!QFile::copy(config_path, archive_path)) {
            if (error != nullptr) {
                *error = QStringLiteral("Nelze vytvořit zálohu konfigurace: %1")
                             .arg(archive_path);
            }
            return false;
        }
    }
    QSettings output(config_path, QSettings::IniFormat);
    output.setValue("Application/Language", language);
    for (auto it = configured_paths.cbegin(); it != configured_paths.cend(); ++it) {
        output.setValue(QStringLiteral("Paths/") + it.key(),
                        QDir::fromNativeSeparators(it.value().trimmed()));
    }
    output.setValue("Templates/Part", part_template);
    for (auto it = units.cbegin(); it != units.cend(); ++it) {
        output.setValue(QStringLiteral("Units/") + it.key(), it.value());
    }
    output.sync();
    if (output.status() == QSettings::NoError) return true;
    if (!archive_path.isEmpty()) {
        QFile::remove(config_path);
        QFile::copy(archive_path, config_path);
        QFile::remove(archive_path);
    }
    if (error != nullptr) {
        *error = QStringLiteral("Konfiguraci nelze uložit: %1").arg(config_path);
    }
    return false;
}

}  // namespace zima::app

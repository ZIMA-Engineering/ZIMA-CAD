#include "application_settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
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

QString next_archive_path(const QString& target) {
    int version = 1;
    QString archive;
    do {
        archive = QStringLiteral("%1.%2").arg(target).arg(version++);
    } while (QFileInfo::exists(archive));
    return archive;
}

}  // namespace

ApplicationSettings ApplicationSettings::load(const QString& working_directory) {
    ApplicationSettings result;
    result.base_config_path = locate_base_config_path();
    const QDir startup_directory(
        working_directory.trimmed().isEmpty()
            ? QDir::currentPath()
            : QFileInfo(working_directory).absoluteFilePath());
    const QString local_candidate = startup_directory.absoluteFilePath("config.ini");
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
    const QFileInfo target_info(config_path);
    if (!target_info.absoluteDir().exists() &&
        !QDir().mkpath(target_info.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Konfigurační adresář nelze vytvořit: %1")
                         .arg(target_info.absolutePath());
        }
        return false;
    }

    QTemporaryFile temporary(
        target_info.absolutePath() + QStringLiteral("/.") +
        target_info.fileName() + QStringLiteral(".XXXXXX.tmp"));
    temporary.setAutoRemove(true);
    if (!temporary.open()) {
        if (error != nullptr) {
            *error = QStringLiteral("Dočasný konfigurační soubor nelze vytvořit: %1")
                         .arg(config_path);
        }
        return false;
    }
    const QString temporary_path = temporary.fileName();
    temporary.close();

    QSettings output(temporary_path, QSettings::IniFormat);
    if (QFileInfo::exists(config_path)) {
        QSettings source(config_path, QSettings::IniFormat);
        for (const auto& key : source.allKeys())
            output.setValue(key, source.value(key));
    }
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

}  // namespace zima::app

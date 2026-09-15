#include "aipreferences.h"
#include <QSettings>
#include <QObject>
namespace {
QString config;
const char* executableKey() {
#ifdef Q_OS_WIN
    return "AI/CodexExecutableWindows";
#else
    return "AI/CodexExecutableLinux";
#endif
}
}
void CadAi::configurePreferences(const QString& path) { config = path; }
QString CadAi::preferencesPath() { return config; }
CadAi::Preferences CadAi::preferences(const QString& path) {
    if (path.isEmpty() && config.isEmpty()) return {};
    QSettings settings(path.isEmpty() ? config : path, QSettings::IniFormat);
    return {settings.value(executableKey()).toString(), settings.value("AI/Model").toString()};
}
bool CadAi::savePreferences(const Preferences& value, const QString& path, QString* error) {
    if (path.isEmpty()) { if (error) *error = QObject::tr("AI preferences are unavailable."); return false; }
    const auto old = preferences(path);
    if (old.executable == value.executable && old.model == value.model) return true;
    QSettings settings(path, QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    settings.setValue(executableKey(), value.executable);
    settings.setValue("AI/Model", value.model);
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    if (error) *error = QObject::tr("Could not save AI preferences.");
    return false;
}

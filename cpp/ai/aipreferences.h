#pragma once
#include <QString>
namespace CadAi {
struct Preferences { QString executable, model; };
void configurePreferences(const QString& path);
QString preferencesPath();
Preferences preferences(const QString& path = {});
bool savePreferences(const Preferences& value, const QString& path, QString* error);
}

// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#ifndef ZIMA_INSTALLATIONCLIENT_H
#define ZIMA_INSTALLATIONCLIENT_H
#include <QString>
QString zimaInstallationRoot(const QString& executable = {});
QString zimaUpdateExecutable();
bool registerZimaInstance(QString *error, const QString& executable = {});
#endif

// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#include "updatecore.h"
#include "version.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <cstdio>

static void print(const QJsonObject &object)
{
    const auto bytes = CadUpdate::canonical(object);
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); std::fflush(stdout);
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("ZIMA-Engineering");
    app.setApplicationName("ZIMA-CAD"); app.setApplicationVersion(VERSION);
    // Download cache and discovery state are disposable; preferences belong to
    // the shared config/config.ini and are owned by the GUI, never the registry.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/update-state");
#ifdef ZIMA_UPDATE_TESTING
    const auto testState = qEnvironmentVariable("ZIMA_UPDATE_TEST_STATE");
    if (!testState.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testState);
    }
#endif
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"root", "Managed installation root", "directory"});
    parser.addOption({"target", "Target version", "YYYYMMDDNN"});
    parser.addOption({"operation", "Installation operation ID", "id"});
    parser.addOption({"apply", "Activate a verified update or rollback"});
    parser.addOption({"version", "Print binary version"});
#ifdef ZIMA_UPDATE_TESTING
    parser.addOption({"archive", "Fixture archive", "file"});
    parser.addOption({"manifest", "Fixture manifest", "file"});
    parser.addOption({"signature", "Fixture signature", "file"});
#endif
    parser.addPositionalArgument("command", "check, download, install, rollback, status, launch, recover");
    if (!parser.parse(app.arguments())) { print({{"error", parser.errorText()}}); return 2; }
    if (parser.isSet("version")) { std::printf("%s\n", VERSION); return 0; }
    if (parser.isSet("help")) { std::printf("%s", parser.helpText().toUtf8().constData()); return 0; }
    const auto args = parser.positionalArguments();
    if (args.size() != 1) { print({{"error", "Expected one update command"}}); return 2; }
    const auto root = parser.isSet("root") ? QDir(parser.value("root")).absolutePath() : CadUpdate::installationRoot();
    const auto saveResult = [&](QJsonObject result) {
        if (root.isEmpty() || !QStringList{"install", "rollback", "launch"}.contains(args[0])) return;
        if (!parser.isSet("apply") && args[0] != "launch") return;
        try {
            CadUpdate::assertManaged(root);
            QDir().mkpath(CadUpdate::child(root, ".updates"));
            result["operation"] = parser.value("operation");
            CadUpdate::writeJson(CadUpdate::child(root, ".updates/" + CadUpdate::platform() + "-result.json"), result);
        } catch (...) {} // Reporting must not replace the original result.
    };
    try {
        const auto command = args[0];
        QJsonObject result;
        const auto progress = [](const QJsonObject &data) { auto object = data; object["event"] = "progress"; print(object); };
        if (command == "check") result = CadUpdate::check(root);
        else if (command == "status") result = root.isEmpty() ? QJsonObject{{"status", "unmanaged"}} : CadUpdate::status(root);
        else if (command == "launch" || command == "recover") return CadUpdate::launch(root, command == "recover");
        else if (command == "download") {
            const auto offer = CadUpdate::check(root);
            if (offer["status"] != "available" || (parser.isSet("target") && offer["availableVersion"] != parser.value("target"))) throw QString("Requested update is no longer available");
            if (!offer["installable"].toBool()) throw QString("This installation cannot install the offered update: ") + offer["reason"].toString();
            result = CadUpdate::download(root, offer, progress);
        } else if (command == "install") {
            if (!parser.isSet("apply")) result = {{"status", "confirmation-required"}, {"version", parser.value("target")}};
            else result = CadUpdate::activate(root, parser.value("target"), progress);
        } else if (command == "rollback") {
            if (!parser.isSet("apply")) result = {{"status", "confirmation-required"}};
            else result = CadUpdate::rollback(root, progress);
#ifdef ZIMA_UPDATE_TESTING
        } else if (command == "prepare") {
            QFile payload(parser.value("manifest")), sig(parser.value("signature"));
            if (!payload.open(QIODevice::ReadOnly) || !sig.open(QIODevice::ReadOnly)) throw QString("Missing fixture manifest");
            result = CadUpdate::prepare(root, parser.value("archive"), payload.readAll(), sig.readAll(), progress);
#endif
        } else throw QString("Unknown update command");
        saveResult(result);
        result["schemaVersion"] = 1; result["event"] = "result"; print(result); return 0;
    } catch (const QString &error) { saveResult({{"status", "error"}, {"error", error}}); print({{"schemaVersion", 1}, {"event", "result"}, {"status", "error"}, {"error", error}}); return 3; }
}

#include "../app/application_settings.hpp"
#include "../cli/settings.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <sstream>
#include <stdexcept>

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path);
    require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "fixture write failed");
}
QByteArray read(const QString& path) { QFile f(path); require(f.open(QIODevice::ReadOnly), "fixture read failed"); return f.readAll(); }
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        const QString sample=QString::fromUtf8("  Příruba   čelní.PRTZ  ");
        require(zima::DocumentNaming{}.normalize(sample)==sample,"Absent naming settings changed a name");
        const zima::DocumentNaming naming{true,true,true};
        require(naming.normalize(QString::fromUtf8("  Příruba   čelní.PRTZ"))=="PRIRUBA_CELNI.prtz","Combined naming policy or native suffix failed");
        require(naming.normalize(QString::fromUtf8("Pr\u030ci\u0301ruba"))=="PRIRUBA","Decomposed Unicode diacritics were not removed");
        require(zima::DocumentNaming{false,true,false}.normalize(QString::fromUtf8("Čelní díl"))=="Celni dil","Independent diacritics option failed");
        require(zima::DocumentNaming{true,false,false}.normalize(QString::fromUtf8("Čelní díl"))==QString::fromUtf8("ČELNÍ DÍL"),"Independent uppercase option failed");
        require(zima::DocumentNaming{false,false,true}.normalize("  front   part  ")=="front_part","Independent whitespace option failed");
        QTemporaryDir temporary; require(temporary.isValid(), "temporary directory unavailable");
        const auto root = temporary.path() + "/portable space";
#ifdef _WIN32
        const auto platform = QStringLiteral("windows"); const auto executable_name = QStringLiteral("/zima-cad-cpp.exe");
#else
        const auto platform = QStringLiteral("linux"); const auto executable_name = QStringLiteral("/bin/zima-cad-cpp");
#endif
        const auto runtime = root + "/" + platform + "/2026091501";
        const auto exe = runtime + executable_name;
        const auto factory = runtime + "/config/config.ini";
        write(root + "/launcher.ini", "[launcher]\n"); write(runtime + "/version.json", "{}\n"); write(exe, "fixture");
        const QByteArray factory_bytes("[Application]\nLanguage=en\n[Paths]\nTemplates=templates\n[Units]\nLength=mm\n[SheetMetal]\nCutTolerance=0.05\n");
        write(factory, factory_bytes);
        write(root + "/config/config.ini", "[Application]\nLanguage=de\n[Units]\nLength=cm\n[SheetMetal]\nCutTolerance=0.025\n");
        write(root + "/config/" + platform + "/config.ini", "[Paths]\nTemplates=../my-templates\n");
        QDir().mkpath(root + "/Projects");
        auto settings = zima::app::ApplicationSettings::load(root + "/Projects", exe);
        require(settings.language == "de" && settings.units["Length"] == "cm", "common overrides lost");
        require(QDir::cleanPath(settings.resolved_paths["Templates"]) == root + "/config/my-templates", "platform-relative path wrong");
        require(settings.resolved_paths["WorkingDirectory"] == root + "/Projects", "portable project default wrong");
        const auto cli = zima::cli::load_settings(std::filesystem::u8path(exe.toStdString()), std::filesystem::u8path((root + "/Projects").toStdString()), {});
        require(cli.documents.units.at("Length") == "cm", "CLI/common settings differ");
        require(settings.document_naming.normalize(sample)==sample&&cli.documents.normalize_document_name(sample.toStdString())==sample.toStdString(),"Missing configuration enabled name conversion");
        {
            QSettings names(root+"/config/config.ini",QSettings::IniFormat);
            for(const auto* key:{"Uppercase","RemoveDiacritics","ReplaceSpaces"})names.setValue(QString("DocumentNames/")+key,true);
            names.sync();
            const auto gui_names=zima::app::ApplicationSettings::load(root+"/Projects",exe);
            const auto cli_names=zima::cli::load_settings(std::filesystem::u8path(exe.toStdString()),std::filesystem::u8path((root+"/Projects").toStdString()),{});
            require(gui_names.document_naming.normalize(sample)=="PRIRUBA_CELNI.prtz"&&cli_names.documents.normalize_document_name(sample.toStdString())=="PRIRUBA_CELNI.prtz","GUI/CLI naming configuration differs");
        }
        require(settings.sheet_cut_tolerance==.025&&cli.documents.templates.sheet_cut_tolerance==.025,
            "GUI/CLI Sheet Cut default layers differ");
        const auto platform_before = read(root + "/config/" + platform + "/config.ini");
        settings.language = "fr";settings.sheet_cut_tolerance=.075; QString error;
        if (!settings.save(&error)) throw std::runtime_error("portable save failed: " + error.toStdString());
        require(read(factory) == factory_bytes, "save modified factory defaults");
        require(read(root + "/config/config.ini.1").contains("Language=de"), "previous user configuration was not backed up");
        require(read(root + "/config/" + platform + "/config.ini") == platform_before, "unchanged paths were materialized");
        QSettings saved(root + "/config/config.ini", QSettings::IniFormat);
        require(saved.value("Application/Language") == "fr" && !saved.contains("Paths/Localization"), "save pinned version paths");
        require(saved.value("SheetMetal/CutTolerance").toDouble()==.075,"Sheet Cut default was not saved");
        auto invalid_tolerance=settings;invalid_tolerance.sheet_cut_tolerance=0;
        const auto before_invalid_tolerance=read(settings.config_path);
        require(!invalid_tolerance.save(&error)&&read(settings.config_path)==before_invalid_tolerance,
            "Invalid Sheet Cut default modified configuration");
        settings.configured_paths["Templates"] = "new-templates";
        require(settings.save(&error), "platform save failed");
        settings = zima::app::ApplicationSettings::load(root + "/Projects", exe);
        require(QDir::cleanPath(settings.resolved_paths["Templates"]) == root + "/config/new-templates", "edited path moved after save");
        auto failing = settings;
        const auto before_failure = read(failing.platform_config_path);
        failing.configured_paths["Templates"] = "failed-change";
        failing.config_path = root + "/config/not-a-file";
        QDir().mkpath(failing.config_path);
        require(!failing.save(&error), "invalid common target unexpectedly saved");
        require(read(failing.platform_config_path) == before_failure, "failed OK left a partial path edit");
        // Switching to another complete version must use its own inherited resources.
        const auto newer = root + "/" + platform + "/2026091502";
        write(newer + "/version.json", "{}\n"); write(newer + executable_name, "fixture"); write(newer + "/config/config.ini", factory_bytes);
        auto switched = zima::app::ApplicationSettings::load(root + "/Projects", newer + executable_name);
        require(switched.language == "fr", "switch lost user language");
        require(QDir::cleanPath(switched.resolved_paths["Localization"]) == newer + "/config/localization", "switch kept previous factory resource path");
        write(root + "/Projects/config.ini", "[Units]\nLength=in\n[SheetMetal]\nCutTolerance=0.0125\n");
        auto local = zima::app::ApplicationSettings::load(root + "/Projects", exe);
        require(local.units["Length"] == "in" && local.config_path == root + "/Projects/config.ini", "project override contract changed");
        const auto local_cli=zima::cli::load_settings(std::filesystem::u8path(exe.toStdString()),std::filesystem::u8path((root+"/Projects").toStdString()),{});
        require(local.sheet_cut_tolerance==.0125&&local_cli.documents.templates.sheet_cut_tolerance==.0125,
            "Project Sheet Cut tolerance did not override the global default");
        for(const QByteArray invalid:{"nan","invalid","0","0.0000001","1.01"}) {
            const QByteArray invalid_config="[SheetMetal]\nCutTolerance="+invalid+"\n";
            write(root+"/Projects/config.ini",invalid_config);
            std::ostringstream warnings;
            struct RestoreLog {std::streambuf* saved;~RestoreLog(){std::cerr.rdbuf(saved);}} restore{std::cerr.rdbuf(warnings.rdbuf())};
            const auto fallback_gui=zima::app::ApplicationSettings::load(root+"/Projects",exe);
            const auto fallback_cli=zima::cli::load_settings(std::filesystem::u8path(exe.toStdString()),std::filesystem::u8path((root+"/Projects").toStdString()),{});
            require(fallback_gui.sheet_cut_tolerance==.05&&fallback_cli.documents.templates.sheet_cut_tolerance==.05,
                "Invalid Sheet Cut configuration did not use the same safe GUI/CLI default");
            const auto warning=warnings.str();const auto first=warning.find("Warning: invalid SheetMetal/CutTolerance");
            require(first!=std::string::npos&&warning.find("Warning: invalid SheetMetal/CutTolerance",first+1)!=std::string::npos,
                "GUI or CLI did not warn about invalid Sheet Cut configuration");
            require(read(root+"/Projects/config.ini")==invalid_config,"Loading invalid configuration modified the user's file");
        }
        std::cout << "Portable GUI/CLI settings, persistence and version switching passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

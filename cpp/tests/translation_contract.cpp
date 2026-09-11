#include "application_settings.hpp"
#include "primitive_properties_dialog.hpp"
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>
#include <QMessageBox>
#include <QFileDialog>
#include <QAbstractButton>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
QStringList tokens(const QString& text) {
    static const QRegularExpression pattern(R"(%[1-9][0-9]*|&bom\.[a-z_]+|\*\.[a-z0-9]+)");
    QStringList result;
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) result.push_back(matches.next().captured());
    result.sort();
    return result;
}
}

int verify_translations(QApplication& application, QWidget& parent) {
    using namespace zima;
    QTemporaryDir directory;
    check(directory.isValid(), "Cannot create translation test directory");
    const auto catalogue = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path() / "config/localization";
    const auto load = [&](const QString& language) {
        QSettings config(directory.filePath("config.ini"), QSettings::IniFormat);
        config.setValue("Application/Language", language);
        config.setValue("Paths/Localization", QString::fromStdString(catalogue.generic_string()));
        config.sync();
        return app::ApplicationSettings::load(directory.path());
    };
    const auto sources = load("cs").qt_translations;
    check(sources.size() >= 76, "New source messages were not loaded");
    const QStringList languages{"cs", "en", "de", "fr", "ru"};
    const QStringList locked{"Odemknout hodnotu", "Unlock value", "Wert entsperren", "Déverrouiller la valeur", "Разблокировать значение"};
    const QStringList unlocked{"Zamknout hodnotu", "Lock value", "Wert sperren", "Verrouiller la valeur", "Заблокировать значение"};
    const QStringList cancel{"Zrušit", "Cancel", "Abbrechen", "Annuler", "Отмена"};
    for (qsizetype language = 0; language < languages.size(); ++language) {
        const auto settings = load(languages[language]);
        check(settings.language == languages[language], "Configured language was ignored");
        check(settings.qt_translations.keys() == sources.keys(), "Language has missing or extra messages");
        check(settings.translations.contains("global.language") &&
            !settings.translations.contains("Zamknout hodnotu"), "INI sections were mixed");
        app::apply_application_translations(application, settings);
        for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
            const auto translated = QCoreApplication::translate("QObject", it.key().toUtf8().constData());
            check(!translated.isEmpty() && translated == settings.qt_translations.value(it.key()),
                "Qt did not consume the configured translation");
            check(tokens(it.key()) == tokens(translated), "Translation changed a placeholder, BOM token or file extension");
        }
        {
            QMessageBox prompt(QMessageBox::Warning,QObject::tr("Neuložené změny"),
                QObject::tr("Dokument obsahuje neuložené změny. Chcete je uložit?"),
                QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,&parent);
            prompt.setDefaultButton(QMessageBox::Save);
            const auto clean=[](QString text){return text.remove('&');};
            check(clean(prompt.button(QMessageBox::Save)->text())==settings.qt_translations.value("Save"),"Unsaved-document Save button is untranslated");
            check(clean(prompt.button(QMessageBox::Discard)->text())==settings.qt_translations.value("Discard"),"Unsaved-document Discard button is untranslated");
            check(clean(prompt.button(QMessageBox::Cancel)->text())==settings.qt_translations.value("Cancel"),"Unsaved-document Cancel button is untranslated");
            prompt.show();application.processEvents();
            if(language==0)prompt.grab().save("unsaved-document-cs.png");
            prompt.hide();
            QFileDialog file(&parent);file.setOption(QFileDialog::DontUseNativeDialog);file.setAcceptMode(QFileDialog::AcceptSave);
            check(clean(file.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->text())==settings.qt_translations.value("Save"),"Save-file button is untranslated");
            check(QObject::tr("Vlastnosti offsetu")==settings.qt_translations.value("Vlastnosti offsetu")&&QObject::tr("Flip")==settings.qt_translations.value("Flip"),"Offset properties are untranslated");
        }
        check(QObject::tr("unregistered source") == "unregistered source", "Missing translation did not fall back to source");
        check(QObject::tr("Šablona uložena: %1").arg("logo.tblz").contains("logo.tblz"), "File-name placeholder is broken");
        auto initial = document::PartDocument::create_box_container();
        initial.value_locks = {"length"};
        app::PrimitivePropertiesDialog dialog(initial, true, false, [](auto) {}, &parent);
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.show();
        application.processEvents();
        auto* length = dialog.findChild<QAction*>("valueLock:length");
        check(length && length->toolTip() == locked[language], "Saved lock tooltip is not translated");
        length->trigger();
        check(length->toolTip() == unlocked[language], "Unlocked tooltip is not translated");
        auto* capture = dialog.findChild<QAction*>("valueLock:placement:reference_slot:0");
        check(capture && capture->toolTip() == settings.qt_translations.value(
            "Při výběru reference převzít současnou hodnotu a odemknout"), "One-shot capture tooltip is not translated");
        capture->trigger();
        // Toggling may rebuild the reference row, so find the current control.
        capture = dialog.findChild<QAction*>("valueLock:placement:reference_slot:0");
        check(capture && capture->toolTip() == settings.qt_translations.value(
            "Zrušit převzetí současné hodnoty"), "Armed capture tooltip is not translated");
        check(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->text() == cancel[language],
            "Shared Cancel button is not translated");
        dialog.reject();
        std::cout << "Translations verified: " << languages[language].toStdString() << '\n';
    }
    auto settings = load("en");
    settings.qt_translations.insert("Scoped|Obrázek", "Scoped image");
    app::apply_application_translations(application, settings);
    app::apply_application_translations(application, settings);
    check(application.findChildren<QTranslator*>("zimaIniTranslator", Qt::FindDirectChildrenOnly).size() == 1,
        "Language changes accumulated translators");
    check(QCoreApplication::translate("Scoped", "Obrázek") == "Scoped image" && QObject::tr("Obrázek") == "Image",
        "Context-specific translation leaked into another context");
    settings.qt_translations.clear();
    app::apply_application_translations(application, settings);
    check(QObject::tr("Obrázek") == "Obrázek", "Empty catalogue left stale translations installed");
    return 0;
}

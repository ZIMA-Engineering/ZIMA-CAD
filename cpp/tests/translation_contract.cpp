#include <QCheckBox>
#include <QTableWidget>
#include "application_settings.hpp"
#include "construction_properties_dialog.hpp"
#include <QComboBox>
#include "drawing_detail_dialog.hpp"
#include "primitive_properties_dialog.hpp"
#include "sweep_station_label.hpp"
#include "feature_naming.hpp"
#include "sheet_transition_dialog.hpp"
#include "mass_properties_dialog.hpp"
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRawFont>
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
    const auto named_sources = load("cs").translations;
    check(sources.size() > 3000, "Complete source catalogs were not loaded");
    // Source coverage is checked against production code, including shared UI.
    const auto source_root = catalogue.parent_path().parent_path() / "cpp";
    QDirIterator files(QString::fromStdString(source_root.generic_string()),
        {"*.cpp", "*.hpp", "*.inc"}, QDir::Files, QDirIterator::Subdirectories);
    const QRegularExpression calls(R"rx(\b(?:tr|QT_TR_NOOP)\s*\(\s*((?:"(?:\\.|[^"\\])*"\s*)+))rx");
    const QRegularExpression literal(R"rx("(?:\\.|[^"\\])*")rx");
    while (files.hasNext()) {
        const auto path = files.next();
        if (path.contains("/tests/") || path.contains("verification")) continue;
        QFile file(path); check(file.open(QIODevice::ReadOnly), "Cannot read translation source");
        auto matches = calls.globalMatch(QString::fromUtf8(file.readAll()));
        while (matches.hasNext()) {
            QString source;
            auto strings = literal.globalMatch(matches.next().captured(1));
            while (strings.hasNext()) {
                const auto json = "[" + strings.next().captured() + "]";
                source += QJsonDocument::fromJson(json.toUtf8()).array().at(0).toString();
            }
            if (!sources.contains(source)) {
                std::cerr << "Missing translation: " << path.toStdString() << ": " << source.toStdString() << '\n';
                throw std::runtime_error("Production UI source is missing from the language catalogs");
            }
        }
    }
    const auto font_path = catalogue.parent_path() / "fonts/osifont-lgpl3fe.ttf";
    QRawFont iso(QString::fromStdString(font_path.generic_string()), 16);
    check(iso.isValid(), "Cannot load ISO font");
    for (char16_t letter = u'А'; letter <= u'я'; ++letter)
        check(iso.supportsCharacter(QChar(letter)), "ISO font is missing a Russian letter");
    check(iso.supportsCharacter(QChar(u'Ё')) && iso.supportsCharacter(QChar(u'ё')), "ISO font is missing Yo");
    const QStringList languages{"cs", "en", "de", "fr", "ru"};
    const QStringList locked{"Odemknout hodnotu", "Unlock value", "Wert entsperren", "Déverrouiller la valeur", "Разблокировать значение"};
    const QStringList unlocked{"Zamknout hodnotu", "Lock value", "Wert sperren", "Verrouiller la valeur", "Заблокировать значение"};
    const QStringList cancel{"Zrušit", "Cancel", "Abbrechen", "Annuler", "Отмена"};
    for (qsizetype language = 0; language < languages.size(); ++language) {
        const auto settings = load(languages[language]);
        check(settings.language == languages[language], "Configured language was ignored");
        check(settings.qt_translations.keys() == sources.keys(), "Language has missing or extra messages");
        check(settings.translations.keys() == named_sources.keys(), "Language has missing or extra named messages");
        for (auto it = named_sources.cbegin(); it != named_sources.cend(); ++it)
            check(!settings.translations.value(it.key()).isEmpty() &&
                tokens(it.value()) == tokens(settings.translations.value(it.key())),
                "Named translation is empty or changes placeholders");
        check(settings.translations.contains("global.language") &&
            !settings.translations.contains("Zamknout hodnotu"), "INI sections were mixed");
        app::apply_application_translations(application, settings);
        {
            app::SweepPrecisionControls controls({},&parent,{});
            check(controls.findChild<QCheckBox*>("sweepCustomPrecision")->text()==settings.qt_translations.value("Vlastní přesnost"),
                "Sweep precision label is not localized");
            check(controls.toolTip()==settings.qt_translations.value("Tolerance aproximace tažení. Menší hodnota znamená přesnější a obvykle pomalejší výpočet."),
                "Sweep precision tooltip is not localized");
        }
        {
            auto part=document::PartDocument::create_default();
            const char* sources[]={"Bod","Osa","Rovina","Skica","Vytažení"};
            for(int type=0;type<5;++type)
                check(app::next_feature_name(part,static_cast<document::FeatureType>(type),"")==
                    (settings.qt_translations.value(sources[type])+" 001").toStdString(),
                    "Automatic Feature name is not localized");
        }
        {
            document::BodyProperties row;row.name="Surface";row.area=300;
            row.surface_centroid=kernel::Vec3{1,2,3};row.density_kg_mm3=.00000785;
            app::MassPropertiesDialog dialog(row,{{"Length","mm"},{"Mass","kg"}},"Surface",[](auto){},[](auto){},&parent);
            dialog.setAttribute(Qt::WA_DeleteOnClose,false);dialog.show();application.processEvents();
            const auto text=dialog.findChild<QLabel*>("bodyPropertiesResults")->text();
            check(text.contains(settings.qt_translations.value("Plošné těžiště — X: %1; Y: %2; Z: %3").section("%1",0,0)),
                "Surface centroid result is untranslated");
            check(text.contains(settings.qt_translations.value("Hmotnost: nelze určit z otevřené plochy bez tloušťky."))&&!text.contains("Ixx"),
                "Surface result fabricated mass inertia or omitted its translated explanation");
            dialog.hide();
        }
        {
            auto feature=document::create_sheet_transition();
            app::SheetTransitionDialog transition(feature,[](auto){},&parent);
            transition.setAttribute(Qt::WA_DeleteOnClose,false);transition.show();application.processEvents();
            check(transition.windowTitle()==settings.qt_translations.value("Vlastnosti přechodu plechu"),"Transition title is untranslated");
            check(transition.findChild<QPushButton*>("transitionSketch0")->text()==settings.qt_translations.value("Skica půlkruhu"),"Transition profile prompt is untranslated");
            transition.hide();
        }
        {
            app::DrawingDetailDialog detail(drawing::DrawingView{},&parent);
            detail.setAttribute(Qt::WA_DeleteOnClose,false);
            check(detail.findChild<QPushButton*>("detailSource")->text()==settings.qt_translations.value("Vybrat bod v pohledu"),"Detail source button is untranslated");
            check(detail.findChild<QCheckBox*>("detailShowBoundary")->text()==settings.qt_translations.value("Zobrazit hranici ve zdrojovém pohledu"),"Detail boundary toggle is untranslated");
        }
        check(app::sweep_station_label("12 — začátek") == settings.qt_translations.value("%1 — začátek").arg(12),
            "Generated Sweep station labels are not translated");
        {
            auto feature=document::PartDocument::create_feature_container("profile");
            feature.value_locks={"side0_length"};
            feature.feature.sides[0].length=51.123456789;
            feature.feature.sides[1].operation=document::FeatureSideOperation::Revolution;
            feature.feature.sides[1].angle_degrees=123.123456789;
            document::ExtrusionParameters::EndTarget target;
            target.label="swap-target";
            feature.feature.sides[0].targets.push_back(target);
            app::PrimitivePropertiesDialog properties(feature,false,true,[](auto){},&parent);
            properties.setAttribute(Qt::WA_DeleteOnClose,false);properties.show();application.processEvents();
            check(properties.windowTitle()==settings.qt_translations.value("Vlastnosti prvku"),"Feature title is untranslated");
            check(properties.findChild<QCheckBox*>("featureShowPoint")->text()==settings.qt_translations.value("Bod")&&
                properties.findChild<QCheckBox*>("featureShowText")->text()==settings.qt_translations.value("Text"),
                "Feature visibility switches are untranslated");
            check(properties.findChild<QPushButton*>("featureSketchButton")->text()==settings.qt_translations.value("Skica"),"Feature Sketch action is untranslated");
            auto* mode=properties.findChild<QComboBox*>("featureSideMode0");
            check(mode->itemText(0)==settings.qt_translations.value("Bez operace")&&mode->itemText(2)==settings.qt_translations.value("Rotace"),"Feature side modes are untranslated");
            auto* value=properties.findChild<QDoubleSpinBox*>("featureSideValue0");
            check(value->isReadOnly(),"Feature length lock was not restored");
            mode->setCurrentIndex(2);check(!value->isReadOnly(),"Feature angle inherited an unrelated length lock");
            mode->setCurrentIndex(1);check(value->isReadOnly(),"Feature mode switching lost length lock");
            auto* swap=properties.findChild<QPushButton*>("featureSwapSides");
            check(swap&&swap->text()==settings.qt_translations.value("Prohodit strany"),"Feature swap action is untranslated");
            const auto before=properties.pending_value();
            swap->click();
            auto swapped=properties.pending_value();
            check(swapped.id==before.id&&swapped.feature.sides[0]==before.feature.sides[1]&&
                swapped.feature.sides[1]==before.feature.sides[0],"Feature swap lost settings, reference or precision");
            check(swapped.value_locks.contains("side1_length")&&!swapped.value_locks.contains("side0_length"),"Feature swap did not transfer dimension locks");
            swap->click();
            check(properties.pending_value().feature==before.feature&&properties.pending_value().value_locks==before.value_locks,"Double swap did not restore the authored definition");
            auto* symmetric=properties.findChild<QCheckBox*>("featureSymmetric");
            symmetric->setChecked(true);check(!swap->isEnabled(),"Symmetric Feature offers an ambiguous side swap");
            symmetric->setChecked(false);check(swap->isEnabled(),"Independent Feature sides cannot be swapped");
            properties.reject();application.processEvents();
        }
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
        auto initial = document::PartDocument::create_twisted_sheet_container();
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
        for(bool revolve:{false,true}) {
            auto feature=revolve?document::PartDocument::create_revolution_container("profile"):document::PartDocument::create_extrusion_container("profile");
            std::optional<document::HistoryContainer> committed;
            app::PrimitivePropertiesDialog properties(feature,false,false,[&](document::HistoryContainer value){committed=std::move(value);},&parent);
            properties.setAttribute(Qt::WA_DeleteOnClose,false);properties.show();application.processEvents();
            auto* origin=properties.findChild<QCheckBox*>("profileOriginCenterline");
            auto* centroid=properties.findChild<QCheckBox*>("profileCentroidCenterline");
            check(origin&&centroid&&origin->text()==settings.qt_translations.value("Osa počátku profilu")&&centroid->text()==settings.qt_translations.value("Osa těžiště profilu"),"Profile centerline controls are missing or untranslated");
            check(!origin->isChecked()&&!centroid->isChecked(),"New centerlines must be opt-in");origin->setChecked(true);centroid->setChecked(true);
            properties.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();
            check(committed.has_value(),"Profile centerline OK failed");
            check(revolve?(committed->revolution.origin_centerline&&committed->revolution.centroid_centerline):(committed->extrusion.origin_centerline&&committed->extrusion.centroid_centerline),"Profile centerline OK lost choices");
            app::PrimitivePropertiesDialog editing(*committed,true,false,[&](document::HistoryContainer){throw std::runtime_error("Cancel committed profile centerline changes");},&parent);
            editing.setAttribute(Qt::WA_DeleteOnClose,false);editing.show();application.processEvents();
            check(editing.findChild<QCheckBox*>("profileOriginCenterline")->isChecked()&&editing.findChild<QCheckBox*>("profileCentroidCenterline")->isChecked(),"Profile centerline edit lost choices");
            editing.findChild<QCheckBox*>("profileOriginCenterline")->setChecked(false);editing.reject();application.processEvents();
        }
        {
            auto axis=document::PartDocument::create_construction(document::ConstructionKind::Axis);
            std::optional<document::ConstructionObject> committed;
            app::ConstructionPropertiesDialog properties(axis,false,[&](auto value){committed=value;},&parent);
            properties.setAttribute(Qt::WA_DeleteOnClose,false);properties.show();application.processEvents();
            auto* mode=properties.findChild<QComboBox*>("constructionAxisExtentMode");
            auto* reverse=properties.findChild<QDoubleSpinBox*>("constructionAxisReverseLength");
            check(mode&&reverse&&mode->itemText(0)==settings.qt_translations.value("Jedna strana")&&mode->itemText(1)==settings.qt_translations.value("Obě strany")&&mode->itemText(2)==settings.qt_translations.value("Symetricky"),"Axis extent modes are untranslated");
            check(!reverse->isVisible(),"One-sided axis exposes second length");
            mode->setCurrentIndex(1);application.processEvents();check(reverse->isVisible(),"Two-sided axis hides second length");
            auto* end=properties.findChild<QComboBox*>("axisEndMode0");
            check(end&&end->count()==2&&end->itemText(0)==settings.qt_translations.value("Na délku")&&end->itemText(1)==settings.qt_translations.value("Až k…"),"Axis end modes/untranslated Through All");
            end->setCurrentIndex(1);application.processEvents();
            auto* target=properties.findChild<QTableWidget*>("axisEndTarget0");
            check(target&&target->isVisible()&&!properties.findChild<QDoubleSpinBox*>("constructionDisplaySize")->isVisible(),"Up-to reference row missing");
            QMetaObject::invokeMethod(target,"cellClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,1));
            check(properties.active_axis_target()==0,"Axis target field did not arm");
            properties.set_axis_target({"","target","plane"});
            check(!properties.active_axis_target()&&properties.pending_value().axis_ends[0].target.owner_id=="target","Axis target confirmation failed");
            end->setCurrentIndex(0);application.processEvents();
            reverse->setValue(23);properties.findChild<QDoubleSpinBox*>("constructionDisplaySize")->setValue(71);
            properties.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();
            check(committed&&committed->axis_extent_mode==document::AxisExtentMode::TwoSides&&committed->axis_reverse_length==23&&committed->display_size==71,"Axis extent OK lost values");
            app::ConstructionPropertiesDialog editing(*committed,true,[](auto){throw std::runtime_error("Axis Cancel committed changes");},&parent);
            editing.setAttribute(Qt::WA_DeleteOnClose,false);editing.show();application.processEvents();
            check(editing.findChild<QComboBox*>("constructionAxisExtentMode")->currentIndex()==1,"Axis edit lost mode");
            editing.findChild<QComboBox*>("constructionAxisExtentMode")->setCurrentIndex(2);application.processEvents();
            check(!editing.findChild<QDoubleSpinBox*>("constructionAxisReverseLength")->isVisible(),"Symmetric axis exposes second length");
            editing.reject();application.processEvents();
        }
        std::cout << "Translations verified: " << languages[language].toStdString() << '\n';
    }
    auto settings = load("en");
    settings.stacked_tolerances=true;QString settings_error;
    check(settings.save(&settings_error),"Cannot persist global tolerance layout");
    check(load("en").stacked_tolerances,"Global tolerance layout did not survive reload");
    app::apply_application_font(application,settings);
    check(application.property("zimaStackedTolerances").toBool(),"Global tolerance layout did not reach viewers");
    settings.stacked_tolerances=false;app::apply_application_font(application,settings);
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

#include "standard_view_labels.hpp"
#include "file_dialog.hpp"
#include <QTimer>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
#include "application_settings.hpp"
#include "document_tools_dialogs.hpp"
#include "construction_properties_dialog.hpp"
#include <QComboBox>
#include "drawing_detail_dialog.hpp"
#include "primitive_properties_dialog.hpp"
#include "sweep_station_label.hpp"
#include "feature_naming.hpp"
#include "sheet_transition_dialog.hpp"
#include "boundary_surface_dialog.hpp"
#include "helical_sweep_dialog.hpp"
#include "sweep2d_dialog.hpp"
#include "sweep_test_support.hpp"
#include "body_scale_dialog.hpp"
#include "symbol_family_dialog.hpp"
#include "mass_properties_dialog.hpp"
#include "dimension_properties_fields.hpp"
#include "component_properties_dialog.hpp"
#include "sketch_dimension_properties_dialog.hpp"
#include <zima/document/relation_program.hpp>
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
        application.processEvents();
        {
            const auto part=document::PartDocument::create_default();app::DocumentToolData data;
            data.units=part.document_units;data.precision=part.document_precision;
            app::FileSettingsDialog dialog(data,[](auto){throw document::RelationError(7,3,"Cannot safely convert relation units.","width");},settings,&parent);
            dialog.setAttribute(Qt::WA_DeleteOnClose,false);dialog.show();application.processEvents();
            dialog.buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
            const auto expected=settings.qt_translations.value("Line %1: %2 %3").arg(7).arg(settings.qt_translations.value("Cannot safely convert relation units."),"width");
            const auto labels=dialog.findChildren<QLabel*>();
            check(dialog.isVisible()&&std::ranges::any_of(labels,[&](auto* label){return label->text()==expected;}),"Settings unit conversion error is not localized or closes the dialog");
            dialog.reject();
        }
        {
            sketcher::SketchDimension dimension{"units",sketcher::DimensionKind::Distance,"first","second",10};
            app::SketchDimensionPropertiesDialog dialog(dimension,true,[](auto){},&parent);
            dialog.setAttribute(Qt::WA_DeleteOnClose,false);dialog.show();application.processEvents();
            auto* field=dialog.findChild<QDoubleSpinBox*>("sketchDimensionValue");
            for(const auto& [input,message]:std::array<std::pair<const char*,const char*>,2>{{
                    {"1rad","Unit does not match the dimension."},{"nonsense","Invalid numeric expression."}}}) {
                field->findChild<QLineEdit*>()->setText(input);
                dialog.buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
                const auto labels=dialog.findChildren<QLabel*>();
                check(std::ranges::any_of(labels,[&](auto* label){return label->text()==settings.qt_translations.value(message);}),
                    "Unit input validation is not translated in the dimension dialog");
            }
            dialog.reject();
        }
        {
            const auto check_modes=[&](auto& dialog,const char* mode_name,const char* thickness_name) {
                dialog.show();application.processEvents();
                auto* mode=dialog.template findChild<QComboBox*>(mode_name);
                auto* thickness=dialog.template findChild<QDoubleSpinBox*>(thickness_name);
                check(mode&&thickness&&mode->count()==3,"Sweep result controls missing");
                check(mode->itemText(2)==settings.qt_translations.value("Plocha"),"Sweep Surface result is not localized");
                mode->setCurrentIndex(1);application.processEvents();
                check(thickness->isVisible(),"Thin thickness is hidden");
                mode->setCurrentIndex(2);application.processEvents();
                const auto surface=[&] {if constexpr(requires{dialog.pending;})return dialog.pending.is_surface_result();
                    else return dialog.pending_sweep_value().is_surface_result();};
                check(surface()&&!thickness->isVisible(),"Surface mode did not update pending result");
                auto* subtract=dialog.template findChild<QPushButton*>("primitiveSubtractOperation");
                if(!subtract)subtract=dialog.template findChild<QPushButton*>("sweep3DSubtractOperation");
                check(subtract&&!subtract->isEnabled(),"Surface subtraction is offered");
                check(!subtract->icon().isNull()&&subtract->height()==44&&subtract->styleSheet().contains("border-radius:6px"),
                    "Sweep operation appearance differs from Extrusion");
                check(subtract->text()==settings.qt_translations.value("Odečíst"),"Sweep operation is not localized");
                mode->setCurrentIndex(0);application.processEvents();
                check(!surface()&&!thickness->isVisible(),"Solid mode did not restore controls");
                mode->setCurrentIndex(2);dialog.buttons()->button(QDialogButtonBox::Ok)->click();
                check(!dialog.isVisible(),"Surface OK did not close properties");
            };
            int committed=0;
            const auto commit=[&](auto value){check(value.is_surface_result(),"Properties committed wrong result mode");++committed;};
            app::HelicalSweepDialog helical(test_support::sweep_fixture(document::FeatureKind::HelicalSweep),commit,&parent);
            helical.setAttribute(Qt::WA_DeleteOnClose,false);
            auto* base_plane=helical.findChild<QComboBox*>("helicalBasePlane");
            check(base_plane&&base_plane->count()==3,"Helical own-plane combo missing");
            auto* base_offset=helical.findChild<QDoubleSpinBox*>("helicalBaseOffset");
            base_offset->setValue(-7);
            const auto placement=helical.pending.placement;
            for(int index:{1,2,0}) {
                base_plane->setCurrentIndex(index);
                document::PartDocument::reframe_helical_sketches(helical.pending);
                const auto base=sketcher::Sketch::from_serialized(helical.pending.helical.sketches[0]);
                const auto guide=sketcher::Sketch::from_serialized(helical.pending.helical.sketches[1]);
                check(base.plane==static_cast<sketcher::SketchPlane>(index)&&base.plane_offset==-7&&helical.pending.placement==placement,
                    "Helical plane selection lost offset or changed container placement");
                check(std::abs(base.resolved_normal.x-guide.resolved_y_axis.x)<1e-9&&
                    std::abs(base.resolved_normal.y-guide.resolved_y_axis.y)<1e-9&&
                    std::abs(base.resolved_normal.z-guide.resolved_y_axis.z)<1e-9,"Radial guide did not follow base plane");
            }
            helical.show();application.processEvents();
            auto* result=helical.findChild<QComboBox*>("helicalResultType");
            check(helical.findChild<QPushButton*>("helicalSketch2")->y()<result->y()&&result->y()<base_plane->y()&&base_plane->y()<base_offset->y(),
                "Helical controls are not ordered Sketches, result, plane, offset");
            bool plane_label=false;
            for(auto* label:helical.findChildren<QLabel*>())plane_label|=label->text()==settings.qt_translations.value("Rovina");
            check(plane_label,"Helical plane label is not localized");
            check_modes(helical,"helicalResultType","helicalThickness");
            app::Sweep2DDialog planar(test_support::sweep_fixture(document::FeatureKind::Sweep2D),commit,&parent);
            planar.setAttribute(Qt::WA_DeleteOnClose,false);
            check_modes(planar,"sweep2dResultType","sweep2dThickness");
            app::ConstructionPropertiesDialog spatial(test_support::sweep_fixture(document::FeatureKind::Sweep3D),false,true,commit,&parent);
            spatial.setAttribute(Qt::WA_DeleteOnClose,false);
            check_modes(spatial,"sweep3DResultType","sweep3DThickness");
            check(committed==3,"Surface dialogs did not each commit once");
            app::HelicalSweepDialog invalid(test_support::sweep_fixture(document::FeatureKind::HelicalSweep),
                [](auto){throw std::runtime_error("Tloušťka uzavírá kruhový profil");},&parent);
            invalid.setAttribute(Qt::WA_DeleteOnClose,false);invalid.show();application.processEvents();
            invalid.buttons()->button(QDialogButtonBox::Ok)->click();
            const auto labels=invalid.findChildren<QLabel*>();
            check(invalid.isVisible()&&std::ranges::any_of(labels,[&](auto* label){return label->text()==settings.qt_translations.value("Tloušťka uzavírá kruhový profil");}),
                "Sweep calculation error is not localized or closed the failed edit");
            invalid.buttons()->button(QDialogButtonBox::Cancel)->click();
        }
        {
            check(QDir(directory.path()).mkpath("nested"), "Cannot create directory navigation fixture");
            bool inspected = false;
            bool labels_match = true;
            QTimer::singleShot(0, &parent, [&] {
                auto* chooser = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                if (!chooser) return;
                auto* buttons = chooser->findChild<QDialogButtonBox*>();
                auto* accept = buttons ? buttons->button(QDialogButtonBox::Open) : nullptr;
                auto* name = chooser->findChild<QLineEdit*>("fileNameEdit");
                inspected = accept && name;
                const auto verify_label = [&](const char* stage) {
                    if (accept && accept->text() != settings.translations.value("button.select"))
                        std::cerr << "Directory label: " << accept->text().toStdString()
                            << "; stage: " << stage << "; expected: " << settings.translations.value("button.select").toStdString() << '\n';
                    labels_match &= accept && accept->text() == settings.translations.value("button.select");
                };
                verify_label("initial");
                chooser->setDirectory(directory.filePath("nested"));
                if (name) name->setText(".");
                verify_label("nested");
                chooser->setDirectory(directory.path());
                if (name) name->setText(".");
                verify_label("parent");
                chooser->reject();
            });
            const auto selected = app::choose_directory(&parent, "Directory translation test",
                directory.path(), settings.translations);
            check(inspected && selected.isEmpty(), "Directory chooser inspection or Cancel failed");
            check(labels_match, "Directory navigation replaced the translated Select button");
        }
        {
            app::RelationsDialog dialog({},"",[](auto){},settings,&parent);
            dialog.setAttribute(Qt::WA_DeleteOnClose,false);
            const auto* arrow=dialog.findChild<QPushButton*>("relationsPickDimension");
            check(arrow&&arrow->text()==settings.qt_translations.value("Insert dimension from View"),"Relation picker is untranslated");
            check(dialog.findChild<QPushButton*>("relationsImport")->text()==settings.qt_translations.value("Import text…"),"Relation import is untranslated");
            check(dialog.findChild<QPushButton*>("relationsExport")->text()==settings.qt_translations.value("Export text…"),"Relation export is untranslated");
        }
        {
            assembly::PartOccurrence occurrence;occurrence.name="Source";
            app::ComponentPropertiesDialog dialog(occurrence,[](auto){},&parent);
            dialog.setAttribute(Qt::WA_DeleteOnClose,false);
            auto* option=dialog.findChild<QCheckBox*>("componentBomIgnoreVariant");
            check(option&&option->text()==settings.qt_translations.value("V kusovníku ignorovat variantu"),"Component BOM option is untranslated");
            check(option->toolTip()==settings.qt_translations.value("Použít název zdrojového souboru a sloučit jeho označené varianty do jedné položky kusovníku. Strom a geometrie se nemění."),"Component BOM tooltip is untranslated");
        }
        {
            document::FamilyTable model;model.instances={{"Base",{},"stable-row",false,{{"en","English label"},{"cs","Cesky popisek"}}}};
            app::DocumentToolData data;data.family_table=document::serialize_family_table(model);
            document::FamilyTable stored;bool accepted=false;
            app::FamilyTableDialog dialog("Generic",data,[&](auto value){stored=document::parse_family_table(value.family_table);accepted=true;},settings,&parent);
            dialog.setAttribute(Qt::WA_DeleteOnClose,false);dialog.show();application.processEvents();
            auto* table=dialog.findChild<QTableWidget*>("familyTableTable");
            auto* language_box=dialog.findChild<QComboBox*>("familyLabelLanguage");
            check(language_box&&language_box->count()==5&&!language_box->isEditable()&&language_box->currentText()==settings.language,"Family label language is not restricted or does not follow initial settings");
            check(table->horizontalHeaderItem(2)->text()==settings.qt_translations.value("Sdílený")&&table->horizontalHeaderItem(3)->text()==settings.qt_translations.value("Lokalizace"),"Family localization headers are untranslated");
            const auto labels=dialog.findChildren<QLabel*>();
            check(std::ranges::any_of(labels,[&](auto* label){return label->text()==settings.qt_translations.value("Jazyk popisků");}),"Family label language caption is untranslated");
            language_box->setCurrentText("en");table->item(1,3)->setText("Authored English");
            language_box->setCurrentText("de");table->item(1,3)->setText("Deutsch");
            language_box->setCurrentText("en");check(table->item(1,3)->text()=="Authored English","Changing language discarded pending labels");
            table->item(1,2)->setCheckState(Qt::Checked);
            check(!table->item(1,3)->flags().testFlag(Qt::ItemIsEditable),"Shared Family label remains editable");
            language_box->setCurrentText("de");table->item(1,2)->setCheckState(Qt::Unchecked);
            check(table->item(1,3)->text()=="Deutsch"&&table->item(1,3)->flags().testFlag(Qt::ItemIsEditable),"Shared toggle lost translations");
            table->item(1,3)->setText("");language_box->setCurrentText("cs");
            table->item(1,1)->setText("Renamed");
            dialog.buttons()->button(QDialogButtonBox::Ok)->click();
            check(accepted&&stored.instances.size()==1&&stored.instances.front().id=="stable-row"&&stored.instances.front().name=="Renamed"&&
                stored.instances.front().labels.at("en")=="Authored English"&&stored.instances.front().labels.at("cs")=="Cesky popisek"&&
                !stored.instances.front().labels.contains("de")&&stored.instances.front().values.empty()&&!stored.instances.front().shared_name,
                "Family localization OK changed identity, values, or pending translations");
            data.family_table=document::serialize_family_table(stored);accepted=false;
            app::FamilyTableDialog reopened("Generic",data,[&](auto value){check(value.family_table==data.family_table,"Unchanged Family UI modified its data");accepted=true;},settings,&parent);
            reopened.setAttribute(Qt::WA_DeleteOnClose,false);reopened.buttons()->button(QDialogButtonBox::Ok)->click();
            check(accepted,"Reopened Family did not confirm");accepted=false;
            app::FamilyTableDialog cancelled("Generic",data,[&](auto){accepted=true;},settings,&parent);
            cancelled.setAttribute(Qt::WA_DeleteOnClose,false);
            cancelled.findChild<QTableWidget*>("familyTableTable")->item(1,3)->setText("Discard");
            cancelled.findChild<QComboBox*>("familyLabelLanguage")->setCurrentText("fr");
            cancelled.buttons()->button(QDialogButtonBox::Cancel)->click();check(!accepted,"Family Cancel committed labels");
        }
        {
            const auto definition=symbols::Definition::load(catalogue.parent_path()/"symbols/surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz");
            bool accepted=false;
            app::SymbolFamilyDialog dialog(definition,[&](auto value){check(value.serialized()==definition.serialized(),"Localized Family Table changed an untouched definition");accepted=true;},&parent);dialog.setAttribute(Qt::WA_DeleteOnClose,false);
            dialog.show();application.processEvents();
            check(!dialog.findChild<QPushButton*>("symbolSketchAdd"),"Symbol Family bypasses ordinary Body Sketch creation");
            auto* table=dialog.findChild<QTableWidget*>("symbolFamilyTable");
            check(table->item(0,0)->text()==settings.qt_translations.value("Způsob výroby neurčen"),"Roughness variant is untranslated");
            check(table->horizontalHeaderItem(table->columnCount()-1)->text()==settings.qt_translations.value("Text")+": "+settings.qt_translations.value("Drsnost"),"Roughness field is untranslated");
            dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            check(accepted,"Localized Family Table did not confirm");
        }
        {
            document::BodyHistory body;body.name="Authored scale";body.scale=document::BodyScale{};
            app::BodyScaleDialog scale(body,[](auto){},&parent);scale.setAttribute(Qt::WA_DeleteOnClose,false);
            scale.show();application.processEvents();
            const auto labels=scale.findChildren<QLabel*>();
            check(std::ranges::any_of(labels,[&](const auto* label){return label->text()==settings.qt_translations.value("Scale factor");}),
                "Scale factor label is untranslated");
            const auto* source=scale.findChild<QTableWidget*>("bodyScaleSource");
            check(source&&source->item(0,1)->text()==settings.qt_translations.value("Zdroj")&&
                source->item(0,2)->text()==settings.qt_translations.value("Vyberte…"),"Scale source field is untranslated");
            check(scale.findChild<QLineEdit*>("bodyScaleName")->text()=="Authored scale","Scale translated an authored name");
            scale.hide();
            std::cerr<<"Boundary translations: "<<languages[language].toStdString()<<std::endl;
            auto feature=document::create_boundary_surface();feature.name="Authored surface name";
            app::BoundarySurfaceDialog dialog(feature,[](auto){},&parent);
            dialog.setAttribute(Qt::WA_DeleteOnClose,false);dialog.show();application.processEvents();
            auto* table=dialog.findChild<QTableWidget*>("boundarySurfaceReferences");
            check(table&&table->horizontalHeaderItem(0)&&table->horizontalHeaderItem(2)&&table->item(0,2),"Boundary translation controls missing");
            check(table->horizontalHeaderItem(0)->text()==settings.qt_translations.value("Č.")&&
                table->horizontalHeaderItem(2)->text()==settings.qt_translations.value("Hraniční křivka")&&
                table->item(0,2)->text()==settings.qt_translations.value("Vyberte hranici"),
                "Boundary reference labels are untranslated");
            auto* clear=table->cellWidget(0,1)->findChild<QPushButton*>();
            check(clear,"Boundary clear button missing");
            check(clear->toolTip()==settings.qt_translations.value("Vymazat hranici; řádek zůstane zachován"),
                "Boundary clearing tooltip is untranslated");
            auto* name=dialog.findChild<QLineEdit*>("boundarySurfaceName");check(name,"Boundary name field missing");
            check(name->text()=="Authored surface name",
                "Changing language translated an authored surface name");
            dialog.hide();
            std::cerr<<"Boundary translations checked"<<std::endl;
        }
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
            app::UserParameterData parameters;parameters.order={"test"};parameters.values["test"][""]="1";
            app::UserParametersDialog parameter_dialog(parameters,languages[language],[](auto){},settings,&parent);
            parameter_dialog.setAttribute(Qt::WA_DeleteOnClose,false);parameter_dialog.show();application.processEvents();
            check(parameter_dialog.findChild<QPushButton*>("parameterMoveUp")->text()==settings.qt_translations.value("Nahoru")&&
                parameter_dialog.findChild<QPushButton*>("parameterMoveDown")->text()==settings.qt_translations.value("Dolů"),
                "Parameter movement buttons are untranslated");
            check(parameter_dialog.findChild<QCheckBox*>("parameterOrder")->toolTip()==
                settings.qt_translations.value("Vybrat parametr pro přesun nahoru nebo dolů"),"Parameter ordering tooltip is untranslated");
            parameter_dialog.hide();
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
            check(transition.findChild<QCheckBox*>("transitionReliefs")->text()==settings.qt_translations.value("Odlehčit oba rohy na obdélníkovém konci"),"Collective corner relief is untranslated");
            check(transition.findChild<QPushButton*>("transitionSketch0")->text()==settings.qt_translations.value("Skica půlkruhu"),"Transition profile prompt is untranslated");
            transition.hide();
            auto rectangular_feature=document::create_sheet_transition(true);
            app::SheetTransitionDialog rectangular(rectangular_feature,[](auto){},&parent);
            rectangular.setAttribute(Qt::WA_DeleteOnClose,false);rectangular.show();application.processEvents();
            check(rectangular.findChild<QTableWidget*>("placementSolutionBranchTable")->item(0,1)->text()==
                settings.qt_translations.value("Větev řešení"),"Solution branch field is untranslated");
            check(rectangular.findChild<QPushButton*>("transitionSketch0")->text()==settings.qt_translations.value("Skica druhého obdélníku"),"Rectangular profile prompt is untranslated");
            const auto* sides=rectangular.findChild<QComboBox*>("transitionSides");
            check(sides&&sides->itemText(0)==settings.qt_translations.value("2 sousední strany (L)")&&sides->itemText(1)==settings.qt_translations.value("3 strany (U)"),"Rectangular side selector is untranslated");
            rectangular.hide();
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
            feature.value_locks={"side0_length","side0_draft_angle"};
            feature.feature.sides[0].draft_angle_degrees=5.123456789;
            feature.feature.sides[1].draft_angle_degrees=-2.123456789;
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
            auto* draft=properties.findChild<QDoubleSpinBox*>("featureSideDraft0");
            check(draft&&draft->isReadOnly(),"Feature draft lock was not restored");
            bool draft_label=false;
            for(const auto* label:properties.findChildren<QLabel*>())
                if(label->text()==settings.qt_translations.value("Úhel úkosu"))draft_label=true;
            check(draft_label,"Feature draft label is untranslated");
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
            check(swapped.value_locks.contains("side1_draft_angle")&&!swapped.value_locks.contains("side0_draft_angle"),"Feature swap did not transfer draft locks");
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
        {
            app::DimensionTextFields fields({},&parent);
            const auto* basic=fields.findChild<QCheckBox*>("dimensionBasic");
            check(basic&&basic->text()==settings.qt_translations.value("Teoreticky přesná kóta (rámeček)"),"Basic dimension label is untranslated");
            check(basic->toolTip()==settings.qt_translations.value("Teoreticky přesná kóta nemá rozměrové tolerance. Platí i pro úhly, poloměry a průměry."),"Basic dimension tooltip is untranslated");
        }
        {
            QWidget owner(&parent);QVBoxLayout layout(&owner);
            ui::ContainerPlacementSection section(&owner,&layout,true);
            auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_circle(0,0,10));
            auto geometry=sketch.placement_reference_geometry();
            auto plane=std::make_shared<kernel::SurfaceGeometry>();plane->axis={0,0,1};
            geometry.triangle_references.push_back({"plane","face",{},plane});
            const auto edge=geometry.edges.front().reference;
            section.initialize_from_references({{{},edge.owner_id,edge.semantic_key},{{},"plane","face",7,true}},[](const auto& key){return QString::fromStdString(key);});
            section.set_point_circle_plane_policy(true);
            section.set_branch_geometry_resolver([&]() -> const kernel::ViewerReferenceGeometry& {return geometry;});
            section.refresh_reference_table();
            const auto text=settings.qt_translations.value("Vzdálenost je daná kružnicí. Původní hodnota se obnoví při naklonění roviny.");
            bool found=false;for(auto* field:owner.findChildren<QDoubleSpinBox*>())
                if(field->property("pointCircleDerivedOffset").toBool()){check(field->isReadOnly() && field->toolTip()==text,"Derived circle plane tooltip untranslated");found=true;}
            check(found,"Derived circle plane UI missing in language test");
        }
        check(app::standard_view_label("front")==settings.qt_translations.value("Front – XZ"),"Normal and Drawing view labels differ after language change");
        for(const auto* key:{"Ohyb","Create or edit a Bend.","Koncová rovina","Odstranit pohled","Zářezy na koncích ohybů","Osy ohybů pouze na koncích","Odlehčit oba rohy na obdélníkovém konci","Výroba"})check(QObject::tr(key)==settings.qt_translations.value(QString::fromUtf8(key)),"Transition/view label is untranslated");
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

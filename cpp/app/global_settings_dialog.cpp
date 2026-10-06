#include "global_settings_dialog.hpp"
#include "desktop_integration.hpp"
#include "file_dialog.hpp"
#include "updatespage.h"
#include "updateservice.h"
#include "ai_settings_page.hpp"
#include "aipreferences.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QDialogButtonBox>

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>
#include <QSettings>
#include <QSaveFile>
#include <algorithm>
#include <vector>
#include <zima/document/part_document.hpp>
#include <zima/assembly/assembly_document.hpp>

namespace zima::app {

namespace {
QString setup_config(const ApplicationSettings& settings) {
    return settings.installation_root.isEmpty()?settings.base_config_path:settings.installation_root+"/config/config.ini";
}
bool save_setup_status(const ApplicationSettings& settings,const QString& status) {
    QSettings preferences(setup_config(settings),QSettings::IniFormat);
    preferences.setValue("Setup/Status",status);preferences.sync();return preferences.status()==QSettings::NoError;
}
}

bool GlobalSettingsDialog::needs_initial_setup(const ApplicationSettings& settings) {
    if(settings.installation_root.isEmpty())return false;
    QSettings shared(setup_config(settings),QSettings::IniFormat);
    if(!shared.value("Setup/Status").toString().isEmpty())return false;
    // Factory defaults and a version switch do not identify a new installation.
    // Existing user configuration must never trigger setup after an update.
    for(const auto& key:shared.allKeys())if(key.startsWith("Application/")||key.startsWith("Units/")||
        key.startsWith("Templates/")||key.startsWith("Paths/"))return false;
    return true;
}

GlobalSettingsDialog::GlobalSettingsDialog(
    ApplicationSettings settings, QWidget* parent,bool system_setup,bool first_launch,const desktop::Context* desktop_context)
    : PropertiesSubWindow(system_setup?tr("System Setup"):settings.text(
          "dialog.options.title", tr("Globální nastavení")), parent),
      settings_(std::move(settings)),system_setup_(system_setup),first_launch_(first_launch) {
    setObjectName(system_setup_?"systemSetupDialog":"globalSettingsDialog");
    setMinimumWidth(620);
    auto* config_label = new QLabel(
        QStringLiteral("%1: %2").arg(
            settings_.text("label.options", tr("Nastavení")),
            settings_.config_path), this);
    config_label->setWordWrap(system_setup_);
    content_layout()->addWidget(config_label);

    sections_ = new QTabWidget(this);
    sections_->setObjectName("globalSettingsSections");
    auto* general = new QWidget(sections_);
    auto* general_layout = new QVBoxLayout(general);
    sections_->addTab(general, tr("Obecné"));
    content_layout()->addWidget(sections_);
    auto* form = new QFormLayout;
    if(system_setup_) {
        auto* note=new QLabel(first_launch_?
            tr("Vítejte v ZIMA-CAD. Projděte nastavení, šablony a volitelnou registraci. OK nastavení uloží. Cancel úvodní nastavení přeskočí; později je otevřete přes Nástroje → System Setup."):
            tr("Upravte nastavení, výchozí šablony a volitelnou registraci do systému. Změny potvrdíte tlačítkem OK."),general);
        note->setWordWrap(true);general_layout->addWidget(note);
        unit_preset_=new QComboBox(this);unit_preset_->setObjectName("setupUnitPreset");
        unit_preset_->addItem(tr("Zachovat aktuální jednotky"),"");
        unit_preset_->addItem(tr("Metrické jednotky (mm)"),"mm");
        unit_preset_->addItem(tr("Palcové jednotky (in)"),"in");
        form->addRow(tr("Předvolba jednotek"),unit_preset_);
    }
    language_ = new QComboBox(this);
    language_->setObjectName("globalSettingsLanguage");
    language_->addItems({"cs", "de", "en", "fr", "ru"});
    language_->setCurrentText(settings_.language);
    form->addRow(settings_.text("global.language", tr("Jazyk aplikace")), language_);

    theme_=new QComboBox(this);theme_->setObjectName("globalApplicationTheme");
    theme_->addItem(tr("Světlý"),"light");theme_->addItem(tr("Tmavý"),"dark");
    theme_->setCurrentIndex(settings_.theme=="dark"?1:0);
    theme_->setToolTip(tr("Motiv se řídí tímto nastavením, nikoli motivem operačního systému."));
    form->addRow(tr("Motiv aplikace"),theme_);

    application_font_ = new QCheckBox(tr("Používat ISO font pro GUI"), this);
    application_font_->setObjectName("globalApplicationFont");
    application_font_->setChecked(settings_.use_iso_application_font);
    application_font_->setToolTip(tr("Výkresy, skici a View vždy používají ISO font."));
    form->addRow(application_font_);
    tolerance_layout_ = new QComboBox(this);
    tolerance_layout_->setObjectName("globalToleranceLayout");
    tolerance_layout_->addItem(tr("V řádku"), false);
    tolerance_layout_->addItem(tr("Nad sebou"), true);
    tolerance_layout_->setCurrentIndex(settings_.stacked_tolerances ? 1 : 0);
    form->addRow(tr("Zobrazení tolerancí"), tolerance_layout_);

    auto* naming_page = new QWidget(sections_);
    auto* naming_layout = new QVBoxLayout(naming_page);
    names_uppercase_ = new QCheckBox(tr("Převádět názvy na velká písmena"), naming_page);
    names_uppercase_->setObjectName("globalNamesUppercase");
    names_uppercase_->setChecked(settings_.document_naming.uppercase);
    names_diacritics_ = new QCheckBox(tr("Odstraňovat diakritiku z názvů"), naming_page);
    names_diacritics_->setObjectName("globalNamesRemoveDiacritics");
    names_diacritics_->setChecked(settings_.document_naming.remove_diacritics);
    names_spaces_ = new QCheckBox(tr("Nahrazovat mezery v názvech podtržítkem"), naming_page);
    names_spaces_->setObjectName("globalNamesReplaceSpaces");
    names_spaces_->setChecked(settings_.document_naming.replace_spaces);
    naming_layout->addWidget(names_uppercase_);
    naming_layout->addWidget(names_diacritics_);
    naming_layout->addWidget(names_spaces_);
    naming_layout->addStretch();
    sections_->addTab(naming_page, tr("Názvy souborů"));

    const QMap<QString, QStringList> choices{
        {"Length", {"mm", "cm", "m", "in"}},
        {"Angle", {"deg", "rad"}},
        {"Mass", {"kg", "g", "t", "lb"}},
        {"Time", {"s", "min"}},
        {"Temperature", {"C", "K", "F"}},
        {"Stress", {"Pa", "kPa", "MPa", "GPa", "psi"}}};
    const QMap<QString, QString> unit_labels{
        {"Length", settings_.text("document.unit.length", tr("Jednotka délky"))},
        {"Angle", settings_.text("document.unit.angle", tr("Jednotka úhlu"))},
        {"Mass", settings_.text("document.unit.mass", tr("Jednotka hmotnosti"))},
        {"Time", settings_.text("document.unit.time", tr("Jednotka času"))},
        {"Temperature", settings_.text("document.unit.temperature", tr("Jednotka teploty"))},
        {"Stress", settings_.text("document.unit.stress", tr("Jednotka napětí"))}};
    for (auto it = choices.cbegin(); it != choices.cend(); ++it) {
        auto* combo = new QComboBox(this);
        combo->setObjectName(QStringLiteral("globalUnit") + it.key());
        combo->addItems(it.value());
        combo->setCurrentText(settings_.units.value(it.key(), it.value().front()));
        unit_fields_.insert(it.key(), combo);
        form->addRow(unit_labels.value(it.key()), combo);
    }

    const QMap<QString, QString> path_labels{
        {"WorkingDirectory", settings_.text("global.path.working_directory",
            tr("Výchozí pracovní adresář"))},
        {"Materials", settings_.text("global.path.materials", tr("Knihovna materiálů"))},
        {"Templates", settings_.text("global.path.templates", tr("Šablony"))},
        {"Formats", settings_.text("global.path.formats", tr("Formáty výkresů"))},
        {"Symbols", settings_.text("global.path.symbols", tr("Symboly"))},
        {"Localization", settings_.text("global.path.localization", tr("Překlady"))}};
    for (auto it = path_labels.cbegin(); it != path_labels.cend(); ++it) {
        auto* row = new QWidget(this);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(settings_.configured_paths.value(it.key()), row);
        edit->setObjectName(QStringLiteral("globalPath") + it.key());
        auto* browse = new QPushButton(
            settings_.text("button.browse", tr("Procházet...")), row);
        connect(browse, &QPushButton::clicked, this,
                [this, key = it.key()] { browse_path(key); });
        row_layout->addWidget(edit, 1);
        row_layout->addWidget(browse);
        path_fields_.insert(it.key(), edit);
        form->addRow(it.value(), row);
        if(system_setup_&&it.key()!="WorkingDirectory"&&it.key()!="Templates") {
            row->hide();form->labelForField(row)->hide();
        }
    }
    general_layout->addLayout(form);
    auto* language_note = new QLabel(tr("Po změně jazyka restartujte aplikaci, aby se přeložily i všechny otevřené nabídky a panely."), this);
    language_note->setWordWrap(true);
    general_layout->addWidget(language_note);
    if(system_setup_) {
        template_page_=new QWidget(sections_);auto* template_form=new QFormLayout(template_page_);
        part_template_=new QComboBox(template_page_);part_template_->setEditable(true);part_template_->setObjectName("setupPartTemplate");
        assembly_template_=new QComboBox(template_page_);assembly_template_->setEditable(true);assembly_template_->setObjectName("setupAssemblyTemplate");
        const QDir templates(settings_.resolved_paths.value("Templates"));
        part_template_->addItems(templates.entryList({"*.prtz"},QDir::Files));part_template_->setCurrentText(settings_.part_template);
        assembly_template_->addItems(templates.entryList({"*.asmz"},QDir::Files));assembly_template_->setCurrentText(settings_.assembly_template);
        template_form->addRow(tr("Výchozí šablona dílu"),part_template_);
        template_form->addRow(tr("Výchozí šablona sestavy"),assembly_template_);
        auto* note=new QLabel(tr("Jednotky vybraných šablon musí odpovídat předvolbě. Formát papíru a rohové razítko se vybírají samostatně ve výkresu."),template_page_);
        note->setWordWrap(true);template_form->addRow(note);
        sections_->addTab(template_page_,tr("Šablony"));
        connect(unit_preset_,&QComboBox::currentIndexChanged,this,[this]{
            const auto unit=unit_preset_->currentData().toString();if(unit.isEmpty())return;
            unit_fields_["Length"]->setCurrentText(unit);unit_fields_["Angle"]->setCurrentText("deg");
            unit_fields_["Mass"]->setCurrentText(unit=="mm"?"kg":"lb");
            unit_fields_["Stress"]->setCurrentText(unit=="mm"?"MPa":"psi");
            unit_fields_["Temperature"]->setCurrentText(unit=="mm"?"C":"F");
            unit_fields_["Time"]->setCurrentText("s");
            part_template_->setCurrentText("START_PART_"+unit+".prtz");
            assembly_template_->setCurrentText("START_ASSEMBLY_"+unit+".asmz");
        });
    }
    auto* sheet_page=new QWidget(sections_);
    auto* sheet_form=new QFormLayout(sheet_page);
    sheet_cut_tolerance_=new QDoubleSpinBox(sheet_page);
    sheet_cut_tolerance_->setObjectName("globalSheetCutTolerance");
    sheet_cut_tolerance_->setDecimals(6);sheet_cut_tolerance_->setRange(.000001,1.);
    sheet_cut_tolerance_->setSingleStep(.01);sheet_cut_tolerance_->setSuffix(" mm");
    sheet_cut_tolerance_->setValue(settings_.sheet_cut_tolerance);
    sheet_form->addRow(tr("Tolerance řezu plechem"),sheet_cut_tolerance_);
    auto* sheet_note=new QLabel(tr("Výchozí hodnota pro nové díly. Otevřené a uložené díly používají své vlastní nastavení."),sheet_page);
    sheet_note->setWordWrap(true);sheet_form->addRow(sheet_note);
    sections_->addTab(sheet_page,tr("Plechy"));
    auto* drawing_page=new QWidget(sections_);drawing_page->setObjectName("globalDrawingSettings");
    auto* drawing_form=new QFormLayout(drawing_page);
    drawing_view_style_=new QComboBox(drawing_page);drawing_view_style_->setObjectName("globalDrawingViewStyle");
    drawing_view_style_->addItem(tr("Pouze viditelné hrany"),"visible_edges");
    drawing_view_style_->addItem(tr("Viditelné a skryté hrany"),"hidden_edges");
    drawing_view_style_->addItem(tr("Stínované s hranami"),"shaded_with_edges");
    drawing_view_style_->addItem(tr("Stínované bez hran"),"shaded");
    drawing_view_style_->setCurrentIndex(std::max(0,drawing_view_style_->findData(settings_.drawing_view_style)));
    drawing_form->addRow(tr("Výchozí zobrazení pohledu"),drawing_view_style_);
    drawing_pdf_directory_=new QLineEdit(settings_.drawing_pdf_directory,drawing_page);drawing_pdf_directory_->setObjectName("globalDrawingPdfDirectory");
    drawing_dxf_directory_=new QLineEdit(settings_.drawing_dxf_directory,drawing_page);drawing_dxf_directory_->setObjectName("globalDrawingDxfDirectory");
    drawing_form->addRow(tr("Složka PDF"),drawing_pdf_directory_);
    drawing_form->addRow(tr("Složka DXF"),drawing_dxf_directory_);
    auto* drawing_note=new QLabel(tr("Cesty jsou relativní k uloženému výkresu. PDF obsahuje všechny listy, DXF pouze aktuální list. Projekční pohled přebírá zobrazení rodiče."),drawing_page);
    drawing_note->setWordWrap(true);drawing_form->addRow(drawing_note);
    sections_->addTab(drawing_page,tr("Výkresy"));
    updates_ = new UpdatesPage([this](bool rollback) {
        auto* service = UpdateService::get();
        const auto blocker = service->restartBlocker();
        if (!blocker.isEmpty()) {
            QMessageBox::information(this, tr("Před restartem"), blocker);
            return;
        }
        // Use the same validated OK path as the ordinary confirmation action.
        bool accepted = false;
        const auto connection = connect(this, &QDialog::accepted, this, [&accepted] { accepted = true; });
        buttons()->button(QDialogButtonBox::Ok)->click();
        disconnect(connection);
        if (accepted) { if (rollback) service->rollback(); else service->restartPrepared(); }
    }, sections_);
    sections_->addTab(updates_, tr("Aktualizace"));
    ai_preferences_path_ = settings_.installation_root.isEmpty() ? settings_.base_config_path
        : settings_.installation_root + "/config/config.ini";
    ai_ = new AiSettingsPage(ai_preferences_path_, sections_);
    sections_->addTab(ai_, tr("AI"));
    desktop_ = desktop_context?desktop::settings_page(sections_,*desktop_context):desktop::settings_page(sections_);
    sections_->addTab(desktop_, tr("Desktop integration"));
    if(system_setup_) {
        // Hidden tabs still contribute to QStackedWidget's minimum size.
        // Remove the setup-inapplicable pages from the stack, retaining their
        // controls and existing settings values without sizing the wizard by them.
        for(int i=sections_->count()-1;i>=0;--i)if(sections_->widget(i)!=general&&sections_->widget(i)!=template_page_&&sections_->widget(i)!=desktop_)sections_->removeTab(i);
        const int natural_height=layout()->totalHeightForWidth(800);
        set_initial_size({800,natural_height>0?natural_height:minimumSizeHint().height()});set_centered_on_show();
        if(first_launch_)connect(this,&QDialog::rejected,this,[this]{
            if(!save_setup_status(settings_,"skipped"))QMessageBox::critical(this,tr("Uložení selhalo"),tr("Průběh nastavení nelze uložit."));
        });
    }
    connect(this, &QDialog::finished, updates_, [this] { updates_->cancelPendingInstallation(); });
    connect(this, &QDialog::rejected, this, [] {
        if (UpdateService::get()->busy()) UpdateService::get()->cancel();
    });
}

void GlobalSettingsDialog::show_updates() { sections_->setCurrentWidget(updates_); }
void GlobalSettingsDialog::show_ai() { sections_->setCurrentWidget(ai_); }

const ApplicationSettings& GlobalSettingsDialog::settings() const {
    return settings_;
}

void GlobalSettingsDialog::browse_path(const QString& key) {
    auto* edit = path_fields_.value(key);
    if (edit == nullptr) return;
    QString initial = edit->text().trimmed();
    if (!QDir::isAbsolutePath(initial)) {
        initial = QDir(QFileInfo(settings_.config_path).absolutePath())
                      .absoluteFilePath(initial);
    }
    const QString selected = choose_directory(
        this, settings_.text("file.select_directory", tr("Vybrat adresář")),
        initial, settings_.translations);
    if (selected.isEmpty()) return;
    const QDir config_directory(QFileInfo(settings_.config_path).absolutePath());
    QString display = config_directory.relativeFilePath(selected);
    if (display.startsWith("../")) display = selected;
    edit->setText(QDir::fromNativeSeparators(display));
}

bool GlobalSettingsDialog::submit() {
    settings_.document_naming = {names_uppercase_->isChecked(), names_diacritics_->isChecked(), names_spaces_->isChecked()};
    settings_.stacked_tolerances = tolerance_layout_->currentData().toBool();
    settings_.sheet_cut_tolerance=sheet_cut_tolerance_->value();
    settings_.drawing_view_style=drawing_view_style_->currentData().toString();
    settings_.drawing_pdf_directory=drawing_pdf_directory_->text().trimmed();
    settings_.drawing_dxf_directory=drawing_dxf_directory_->text().trimmed();
    settings_.language = language_->currentText();
    settings_.theme=theme_->currentData().toString();
    settings_.use_iso_application_font =
        application_font_->isChecked();
    for (auto it = unit_fields_.cbegin(); it != unit_fields_.cend(); ++it) {
        settings_.units[it.key()] = it.value()->currentText();
    }
    for (auto it = path_fields_.cbegin(); it != path_fields_.cend(); ++it) {
        settings_.configured_paths[it.key()] = it.value()->text().trimmed();
    }
    QString error;
    if(system_setup_) {
        settings_.part_template=part_template_->currentText().trimmed();settings_.assembly_template=assembly_template_->currentText().trimmed();
        const QDir config(QFileInfo(settings_.config_path).absolutePath());
        const QDir templates(config.absoluteFilePath(settings_.configured_paths.value("Templates")));
        try {
            const auto part=zima::document::PartDocument::load(std::filesystem::u8path(templates.absoluteFilePath(settings_.part_template).toStdString()));
            const auto assembly=zima::assembly::AssemblyDocument::load(std::filesystem::u8path(templates.absoluteFilePath(settings_.assembly_template).toStdString()));
            for(auto it=settings_.units.cbegin();it!=settings_.units.cend();++it)
                if(part.document_units.at(it.key().toStdString())!=it.value().toStdString()||assembly.document_units.at(it.key().toStdString())!=it.value().toStdString()) {
                    sections_->setCurrentWidget(template_page_);QMessageBox::critical(this,tr("Uložení selhalo"),tr("Jednotky šablon neodpovídají zvoleným jednotkám."));return false;
                }
        } catch(const std::exception&) {
            sections_->setCurrentWidget(template_page_);QMessageBox::critical(this,tr("Uložení selhalo"),tr("Vyberte platné nativní šablony dílu a sestavy."));return false;
        }
        struct Snapshot {QString path;QByteArray bytes;bool existed;};std::vector<Snapshot> snapshots;
        for(const auto& path:{settings_.config_path,settings_.platform_config_path,setup_config(settings_)}) {
            if(path.isEmpty()||std::ranges::any_of(snapshots,[&](const auto& s){return s.path==path;}))continue;
            QFile file(path);const bool existed=file.exists();
            if(existed&&!file.open(QIODevice::ReadOnly)){QMessageBox::critical(this,tr("Uložení selhalo"),tr("Průběh nastavení nelze uložit."));return false;}
            snapshots.push_back({path,existed?file.readAll():QByteArray{},existed});
        }
        const QString working=config.absoluteFilePath(settings_.configured_paths.value("WorkingDirectory"));
        const bool created=!QDir(working).exists();
        if(!QDir().mkpath(working)){QMessageBox::critical(this,tr("Uložení selhalo"),tr("Pracovní adresář nelze vytvořit."));return false;}
        bool saved=settings_.save(&error);
        if(saved&&!save_setup_status(settings_,"completed")){saved=false;error=tr("Průběh nastavení nelze uložit.");}
        if(saved&&desktop::submit_settings(desktop_))return true;
        bool restored=true;
        for(const auto& snapshot:snapshots) {
            if(snapshot.existed){QSaveFile file(snapshot.path);restored=(file.open(QIODevice::WriteOnly)&&file.write(snapshot.bytes)==snapshot.bytes.size()&&file.commit())&&restored;}
            else if(QFileInfo::exists(snapshot.path))restored=QFile::remove(snapshot.path)&&restored;
        }
        if(created)QDir().rmdir(working);
        sections_->setCurrentWidget(desktop_);
        if(!restored||!error.isEmpty())QMessageBox::critical(this,tr("Uložení selhalo"),error.isEmpty()?tr("Průběh nastavení nelze uložit."):error);
        return false;
    }
    auto* update_service = UpdateService::get();
    const bool previous_automatic = update_service->automatic();
    const auto previous_ai = CadAi::preferences(ai_preferences_path_);
    if (updates_->save(&error)) {
        if (CadAi::savePreferences(ai_->values(), ai_preferences_path_, &error)) {
            if (settings_.save(&error)) {
                if (desktop::submit_settings(desktop_)) return true;
                sections_->setCurrentWidget(desktop_);
                return false;
            }
            QString restore_ai_error;
            if (!CadAi::savePreferences(previous_ai, ai_preferences_path_, &restore_ai_error)) error += '\n' + restore_ai_error;
        }
        QString restore_error;
        if (!update_service->saveAutomatic(previous_automatic, &restore_error)) error += '\n' + restore_error;
    }
    QMessageBox::critical(this, tr("Uložení selhalo"), error);
    return false;
}

}  // namespace zima::app

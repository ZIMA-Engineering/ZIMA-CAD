#include "global_settings_dialog.hpp"
#include "file_dialog.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

namespace zima::app {

GlobalSettingsDialog::GlobalSettingsDialog(
    ApplicationSettings settings, QWidget* parent)
    : PropertiesSubWindow(settings.text(
          "dialog.options.title", tr("Globální nastavení")), parent),
      settings_(std::move(settings)) {
    setObjectName("globalSettingsDialog");
    setMinimumWidth(620);
    content_layout()->addWidget(new QLabel(
        QStringLiteral("%1: %2").arg(
            settings_.text("label.options", tr("Nastavení")),
            settings_.config_path), this));

    auto* form = new QFormLayout;
    language_ = new QComboBox(this);
    language_->setObjectName("globalSettingsLanguage");
    language_->addItems({"cs", "de", "en", "fr"});
    language_->setCurrentText(settings_.language);
    form->addRow(settings_.text("global.language", tr("Jazyk aplikace")), language_);

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
        {"Materials", settings_.text("global.path.materials", tr("Knihovna materiálů"))},
        {"Templates", settings_.text("global.path.templates", tr("Šablony"))},
        {"Formats", settings_.text("global.path.formats", tr("Formáty výkresů"))},
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
    }
    content_layout()->addLayout(form);
}

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
    settings_.language = language_->currentText();
    for (auto it = unit_fields_.cbegin(); it != unit_fields_.cend(); ++it) {
        settings_.units[it.key()] = it.value()->currentText();
    }
    for (auto it = path_fields_.cbegin(); it != path_fields_.cend(); ++it) {
        settings_.configured_paths[it.key()] = it.value()->text().trimmed();
    }
    QString error;
    if (settings_.save(&error)) return true;
    QMessageBox::critical(this, tr("Uložení selhalo"), error);
    return false;
}

}  // namespace zima::app

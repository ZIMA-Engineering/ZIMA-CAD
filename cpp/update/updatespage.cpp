#include "updatespage.h"
#include "updateservice.h"
#include "installationclient.h"
#include "version.h"
#include <QCheckBox>
#include <QFile>
#include <QJsonDocument>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

UpdatesPage::UpdatesPage(std::function<void(bool)> restart, QWidget* parent) : QWidget(parent) {
    setObjectName("updatesPage");
    auto* layout = new QVBoxLayout(this);
    automatic_ = new QCheckBox(tr("Při spuštění nenápadně zkontrolovat nové verze"), this);
    automatic_->setObjectName("updatesAutomatic");
    automatic_->setChecked(UpdateService::get()->automatic());
    versions_ = new QLabel(this); status_ = new QLabel(this); detail_ = new QLabel(this);
    status_->setObjectName("updateStatus");
    for (auto* label : {versions_, status_, detail_}) {
        label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    }
    notes_ = new QPlainTextEdit(this); notes_->setReadOnly(true);
    notes_->setPlaceholderText(tr("Změny v nové verzi se zobrazí zde."));
    notes_->setMaximumBlockCount(500);
    progress_ = new QProgressBar(this); progress_->setObjectName("updateProgress");
    layout->addWidget(automatic_); layout->addWidget(versions_); layout->addWidget(status_);
    layout->addWidget(progress_); layout->addWidget(notes_, 1); layout->addWidget(detail_);
    auto* row = new QHBoxLayout;
    check_ = new QPushButton(tr("Zkontrolovat"), this); check_->setObjectName("checkForUpdates");
    install_ = new QPushButton(this); install_->setObjectName("installUpdate");
    rollback_ = new QPushButton(tr("Vrátit předchozí verzi"), this); rollback_->setObjectName("rollbackUpdate");
    cancel_ = new QPushButton(tr("Zrušit stahování"), this); cancel_->setObjectName("cancelUpdateDownload");
    for (auto* button : {check_, install_, rollback_, cancel_}) row->addWidget(button);
    layout->addLayout(row);
    connect(check_, &QPushButton::clicked, UpdateService::get(), &UpdateService::check);
    connect(cancel_, &QPushButton::clicked, UpdateService::get(), &UpdateService::cancel);
    connect(install_, &QPushButton::clicked, this, [restart] {
        auto* service = UpdateService::get();
        if (service->preparedVersion().isEmpty()) service->install(); else restart(false);
    });
    connect(rollback_, &QPushButton::clicked, this, [restart] { restart(true); });
    connect(UpdateService::get(), &UpdateService::changed, this, &UpdatesPage::refresh);
    refresh();
}
bool UpdatesPage::save(QString* error) { return UpdateService::get()->saveAutomatic(automatic_->isChecked(), error); }
void UpdatesPage::refresh() {
    const auto* service = UpdateService::get();
    const auto offer = service->offer();
    QString previous;
    const auto root = zimaInstallationRoot();
#ifdef Q_OS_WIN
    const QString platform = "windows-x64";
#else
    const QString platform = "linux-x86_64";
#endif
    if (!root.isEmpty()) {
        QFile file(root + "/.updates/" + platform + "-journal.json");
        if (file.open(QIODevice::ReadOnly) && file.size() < 65536) {
            const auto record = QJsonDocument::fromJson(file.readAll()).object();
            if (record["phase"] == "committed") previous = record["previous"].toString();
        }
    }
    versions_->setText(tr("Spuštěná verze: %1").arg(VERSION) + '\n'
        + tr("Předchozí verze: %1").arg(previous.isEmpty() ? tr("není") : previous) + '\n'
        + tr("Dostupná verze: %1").arg(offer.isEmpty() ? tr("—") : offer["availableVersion"].toString()));
    QString status;
    const auto phase = service->phase();
    if (!service->error().isEmpty()) status = tr("Kontrola nebo instalace se nezdařila. Podrobnosti jsou níže.");
    else if (phase == "checking") status = tr("Kontroluji nové verze…");
    else if (phase == "downloading") status = tr("Stahuji aktualizaci…");
    else if (phase == "verifying") status = tr("Ověřuji podpis a obsah balíku…");
    else if (phase == "waiting") status = tr("Čekám na ukončení aplikace…");
    else if (!service->preparedVersion().isEmpty()) status = tr("Verze %1 je připravena. Instalaci dokončí restart.").arg(service->preparedVersion());
    else if (!offer.isEmpty()) status = tr("Je dostupná nová verze.");
    else if (phase == "current") status = tr("Novější kompatibilní vydání není dostupné.");
    else status = tr("Aktualizace zatím nebyly zkontrolovány.");
    status_->setText(status);
    progress_->setVisible(service->busy());
    const bool known = phase == "downloading" && service->totalBytes() > 0;
    progress_->setRange(0, known ? 100 : 0);
    if (known) progress_->setValue(int(100 * service->receivedBytes() / service->totalBytes()));
    if (notes_->toPlainText() != offer["notes"].toString()) notes_->setPlainText(offer["notes"].toString());
    QString detail = tr("Ponechávají se dvě ověřené verze: aktuální a předchozí. Projekty a uživatelská nastavení zůstávají zachována.");
    if (!service->lastCheck().isEmpty()) detail += '\n' + tr("Poslední úspěšná kontrola: %1").arg(service->lastCheck());
    if (!offer.isEmpty()) detail += '\n' + tr("Velikost: %1 MiB").arg(offer["manifest"].toObject()["archive"].toObject()["size"].toDouble() / 1048576., 0, 'f', 1);
    if (root.isEmpty()) detail += '\n' + tr("Vývojové spuštění: aktualizace lze kontrolovat. Instalace je dostupná v podepsaném distribučním balíku.");
    else if (offer["installable"] == false) detail += '\n' + offer["reason"].toString();
    if (!service->error().isEmpty()) detail += '\n' + service->error();
    detail_->setText(detail);
    check_->setEnabled(!service->busy());
    install_->setText(service->preparedVersion().isEmpty() ? tr("Stáhnout aktualizaci") : tr("Nainstalovat a restartovat"));
    install_->setEnabled(!service->busy() && (!service->preparedVersion().isEmpty() || offer["installable"].toBool()));
    install_->setToolTip(tr("Před restartem uložte dokumenty. Restart potvrdí také nastavení tohoto okna."));
    rollback_->setEnabled(!service->busy() && !previous.isEmpty());
    rollback_->setToolTip(tr("Potvrdí nastavení a restartuje aplikaci v předchozí ověřené verzi."));
    cancel_->setVisible(service->busy() && phase != "waiting");
}

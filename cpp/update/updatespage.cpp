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
#include <QTimer>

namespace {
UpdatesPage::Backend productionBackend() {
    auto* service = UpdateService::get();
    return {
        [service] {
            UpdatesPage::State state;
            state.automatic = service->automatic(); state.busy = service->busy();
            state.phase = service->phase(); state.error = service->error();
            state.prepared = service->preparedVersion(); state.lastCheck = service->lastCheck();
            state.offer = service->offer(); state.received = service->receivedBytes(); state.total = service->totalBytes();
            const auto root = zimaInstallationRoot(); state.installed = !root.isEmpty();
#ifdef Q_OS_WIN
            const QString platform = "windows-x64";
#else
            const QString platform = "linux-x86_64";
#endif
            if (state.installed) {
                QFile file(root + "/.updates/" + platform + "-journal.json");
                if (file.open(QIODevice::ReadOnly) && file.size() < 65536) {
                    const auto record = QJsonDocument::fromJson(file.readAll()).object();
                    if (record["phase"] == "committed") state.previous = record["previous"].toString();
                }
            }
            return state;
        },
        [service] { service->check(); }, [service] { service->install(); },
        [service] { service->cancel(); },
        [service](bool enabled, QString* error) { return service->saveAutomatic(enabled, error); },
        [service] { return service->restartBlocker(); }
    };
}
}

UpdatesPage::UpdatesPage(std::function<void(bool)> restart, QWidget* parent)
    : UpdatesPage(std::move(restart), productionBackend(), parent) {
    connect(UpdateService::get(), &UpdateService::changed, this, &UpdatesPage::refresh);
}
UpdatesPage::UpdatesPage(std::function<void(bool)> restart, Backend backend, QWidget* parent)
    : QWidget(parent), backend_(std::move(backend)), restart_(std::move(restart)) {
    setObjectName("updatesPage");
    auto* layout = new QVBoxLayout(this);
    automatic_ = new QCheckBox(tr("Při spuštění nenápadně zkontrolovat nové verze"), this);
    automatic_->setObjectName("updatesAutomatic");
    automatic_->setChecked(backend_.state().automatic);
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
    connect(check_, &QPushButton::clicked, this, [this] { localError_.clear(); backend_.check(); refresh(); });
    connect(cancel_, &QPushButton::clicked, this, &UpdatesPage::cancel);
    connect(install_, &QPushButton::clicked, this, &UpdatesPage::install);
    connect(rollback_, &QPushButton::clicked, this, [this] { restart_(true); });
    refresh();
}
UpdatesPage::~UpdatesPage() {
    // Approval belongs to this open Settings interaction, never to cached files.
    cancelPendingInstallation();
}
void UpdatesPage::cancelPendingInstallation() {
    if (requested_.isEmpty()) return;
    requested_.clear(); ++requestSerial_; restartQueued_ = false;
    backend_.cancel();
}
bool UpdatesPage::save(QString* error) { return backend_.save(automatic_->isChecked(), error); }
void UpdatesPage::install() {
    const auto state = backend_.state();
    if (state.busy || restartQueued_ || (state.prepared.isEmpty() && !state.offer["installable"].toBool())) return;
    localError_ = backend_.restartBlocker();
    if (!localError_.isEmpty()) { refresh(); return; }
    if (!state.prepared.isEmpty()) { refresh(); restart_(false); return; }
    requested_ = state.offer["availableVersion"].toString();
    if (requested_.isEmpty()) return;
    ++requestSerial_; restartQueued_ = false;
    backend_.download(); refresh();
}
void UpdatesPage::cancel() {
    if (requested_.isEmpty()) backend_.cancel();
    else cancelPendingInstallation();
    refresh();
}
void UpdatesPage::refresh() {
    const auto state = backend_.state();
    const auto& offer = state.offer;
    if (!requested_.isEmpty() && !state.busy && !restartQueued_) {
        if (!state.error.isEmpty() || state.prepared != requested_) requested_.clear();
        else {
            restartQueued_ = true;
            const auto serial = requestSerial_;
            QTimer::singleShot(0, this, [this, serial] {
                if (serial != requestSerial_ || requested_.isEmpty()) return;
                const auto latest = backend_.state();
                const bool ready = !latest.busy && latest.error.isEmpty() && latest.prepared == requested_;
                requested_.clear(); restartQueued_ = false;
                localError_ = backend_.restartBlocker();
                refresh();
                if (ready && localError_.isEmpty()) restart_(false);
            });
        }
    }
    versions_->setText(tr("Spuštěná verze: %1").arg(VERSION) + '\n'
        + tr("Předchozí verze: %1").arg(state.previous.isEmpty() ? tr("není") : state.previous) + '\n'
        + tr("Dostupná verze: %1").arg(offer.isEmpty() ? tr("—") : offer["availableVersion"].toString()));
    QString status;
    const auto& phase = state.phase;
    if (!state.error.isEmpty() || !localError_.isEmpty()) status = tr("Kontrola nebo instalace se nezdařila. Podrobnosti jsou níže.");
    else if (phase == "checking") status = tr("Kontroluji nové verze…");
    else if (phase == "downloading") status = tr("Stahuji aktualizaci…");
    else if (phase == "verifying") status = tr("Ověřuji podpis a obsah balíku…");
    else if (phase == "waiting") status = tr("Čekám na ukončení aplikace…");
    else if (!state.prepared.isEmpty()) status = tr("Verze %1 je připravena. Instalaci dokončí restart.").arg(state.prepared);
    else if (!offer.isEmpty()) status = tr("Je dostupná nová verze.");
    else if (phase == "current") status = tr("Novější kompatibilní vydání není dostupné.");
    else status = tr("Aktualizace zatím nebyly zkontrolovány.");
    status_->setText(status);
    progress_->setVisible(state.busy);
    const bool known = phase == "downloading" && state.total > 0;
    progress_->setRange(0, known ? 100 : 0);
    if (known) progress_->setValue(int(100 * state.received / state.total));
    if (notes_->toPlainText() != offer["notes"].toString()) notes_->setPlainText(offer["notes"].toString());
    QString detail = tr("Ponechávají se dvě ověřené verze: aktuální a předchozí. Projekty a uživatelská nastavení zůstávají zachována.");
    if (!state.lastCheck.isEmpty()) detail += '\n' + tr("Poslední úspěšná kontrola: %1").arg(state.lastCheck);
    if (!offer.isEmpty()) detail += '\n' + tr("Velikost: %1 MiB").arg(offer["manifest"].toObject()["archive"].toObject()["size"].toDouble() / 1048576., 0, 'f', 1);
    if (!state.installed) detail += '\n' + tr("Vývojové spuštění: aktualizace lze kontrolovat. Instalace je dostupná v podepsaném distribučním balíku.");
    else if (offer["installable"] == false) detail += '\n' + offer["reason"].toString();
    if (!state.error.isEmpty()) detail += '\n' + state.error;
    if (!localError_.isEmpty()) detail += '\n' + localError_;
    detail_->setText(detail);
    check_->setEnabled(!state.busy && !restartQueued_);
    install_->setText(tr("Nainstalovat a restartovat"));
    install_->setEnabled(!state.busy && !restartQueued_ && (!state.prepared.isEmpty() || offer["installable"].toBool()));
    install_->setToolTip(tr("Před restartem uložte dokumenty. Restart potvrdí také nastavení tohoto okna."));
    rollback_->setEnabled(!state.busy && !restartQueued_ && !state.previous.isEmpty());
    rollback_->setToolTip(tr("Potvrdí nastavení a restartuje aplikaci v předchozí ověřené verzi."));
    cancel_->setVisible((state.busy || restartQueued_) && phase != "waiting");
}

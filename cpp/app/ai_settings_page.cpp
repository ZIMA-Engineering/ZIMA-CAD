#include "ai_settings_page.hpp"
#include "aiprovider.h"
#include "file_dialog.hpp"
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QDesktopServices>

namespace zima::app {
AiSettingsPage::AiSettingsPage(const QString& path, QWidget* parent, AiProvider* provider)
    : QWidget(parent), provider_(provider ? provider : cadAiProvider()) {
    setObjectName("aiSettingsPage");
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Use Codex with your own ChatGPT account. Type codex in the CAD console. Each request uses the active Part, Assembly or Drawing tab. Requests, selected context and requested command results are sent to OpenAI. Model and file changes require inline approval. Account limits apply. Connect and sign-in act immediately; OK saves the executable and model preferences."), this);
    intro->setWordWrap(true); intro->setTextFormat(Qt::PlainText); layout->addWidget(intro);
    auto* form = new QFormLayout;
    const auto preferences = CadAi::preferences(path);
    executable_ = new QLineEdit(preferences.executable.isEmpty() ? findCodexExecutable() : preferences.executable, this);
    executable_->setObjectName("aiExecutable");
    auto* row = new QHBoxLayout; row->addWidget(executable_);
    browse_ = new QPushButton(tr("Browse..."), this); row->addWidget(browse_);
    form->addRow(tr("Codex executable"), row);
    model_ = new QComboBox(this); model_->setObjectName("aiModel");
    model_->addItem(tr("Codex default"), QString());
    if (!preferences.model.isEmpty()) { model_->addItem(preferences.model, preferences.model); model_->setCurrentIndex(1); }
    form->addRow(tr("Model"), model_); layout->addLayout(form);
    auto* buttons = new QHBoxLayout;
    connect_ = new QPushButton(tr("Connect"), this); connect_->setObjectName("aiConnect");
    login_ = new QPushButton(tr("Sign in with ChatGPT"), this); login_->setObjectName("aiLogin");
    logout_ = new QPushButton(tr("Sign out"), this); logout_->setObjectName("aiLogout");
    stop_ = new QPushButton(tr("Stop"), this); stop_->setObjectName("aiConnectionStop");
    for (auto* button : {connect_, login_, logout_, stop_}) buttons->addWidget(button);
    layout->addLayout(buttons);
    status_ = new QLabel(this); status_->setObjectName("aiStatus");
    status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    auto* help = new QPushButton(tr("Get Codex / installation instructions"), this);
    layout->addWidget(help, 0, Qt::AlignLeft); layout->addStretch();
    connect(browse_, &QPushButton::clicked, this, [this] {
        const auto selected = open_file(this, tr("Codex executable"), executable_->text(), tr("All files (*)"));
        if (!selected.isEmpty()) executable_->setText(selected);
    });
    connect(connect_, &QPushButton::clicked, this, [this] { provider_->connectAccount(executable_->text().trimmed()); });
    connect(login_, &QPushButton::clicked, provider_, &AiProvider::login);
    connect(logout_, &QPushButton::clicked, provider_, &AiProvider::logout);
    connect(stop_, &QPushButton::clicked, provider_, &AiProvider::cancel);
    connect(provider_, &AiProvider::changed, this, &AiSettingsPage::refresh);
    connect(help, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://learn.chatgpt.com/docs/cli")); });
    refresh();
}
CadAi::Preferences AiSettingsPage::values() const { return {executable_->text().trimmed(), model_->currentData().toString()}; }
void AiSettingsPage::refresh() {
    connect_->setEnabled(!provider_->busy());
    login_->setEnabled(provider_->connected() && !provider_->ready() && !provider_->busy());
    logout_->setEnabled(provider_->ready() && !provider_->busy());
    stop_->setEnabled(provider_->busy()); executable_->setEnabled(!provider_->busy()); browse_->setEnabled(!provider_->busy());
    model_->setEnabled(!provider_->busy()); status_->setText(provider_->status());
    QSignalBlocker blocker(model_);
    for (const auto& value : provider_->models()) {
        const auto entry = value.toObject(); const auto id = entry["model"].toString();
        if (!id.isEmpty() && model_->findData(id) < 0) model_->addItem(entry["displayName"].toString(id), id);
    }
}
}

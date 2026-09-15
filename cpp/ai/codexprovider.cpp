// SPDX-License-Identifier: GPL-3.0-or-later
#include "codexprovider.h"
#include "aitools.h"
#include "version.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QDesktopServices>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

AiProvider *cadAiProvider()
{
    static auto provider = [] {
        auto* value = new CodexProvider(qApp);
        QObject::connect(value, &AiProvider::loginUrl, value, [](const QUrl& url) { QDesktopServices::openUrl(url); });
        return value;
    }();
    return provider;
}

QString findCodexExecutable()
{
    auto executable = QStandardPaths::findExecutable("codex");
    // QProcess uses a native executable, never an npm .cmd shell wrapper.
    if (!executable.isEmpty() && !executable.endsWith(".cmd", Qt::CaseInsensitive)
        && !executable.endsWith(".bat", Qt::CaseInsensitive)) return executable;
#ifdef Q_OS_WIN
    QDir bins(QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath("OpenAI/Codex/bin"));
    const auto directories = bins.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const auto &directory : directories) {
        const auto path = QDir(directory.absoluteFilePath()).filePath("codex.exe");
        if (QFileInfo(path).isExecutable()) return path;
    }
#endif
    return {};
}

QString CodexProvider::profileDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("ai/codex");
}

CodexProvider::CodexProvider(QObject *parent, const QString &profile) : AiProvider(parent), m_profile(profile)
{
    m_statusSource = QT_TR_NOOP("AI is disconnected.");
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("Codex did not respond in time. Connect again to retry.")); });
}

CodexProvider::~CodexProvider() { stop(); }

void CodexProvider::stop()
{
    m_timeout.stop();
    m_requests.clear(); m_toolRequests.clear(); m_buffer.clear();
    m_ready = false; m_busy = false; m_initialized = false;
    m_thread.clear(); m_turn.clear(); m_response.clear(); m_loginId.clear();
    m_contextKey.clear(); m_prompt.clear(); m_turnCount = 0;
    if (m_process) {
        auto process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        process->closeWriteChannel();
        if (!process->waitForFinished(100)) { process->kill(); process->waitForFinished(1000); }
        process->deleteLater();
    }
}

void CodexProvider::fail(const QString &message)
{
    stop();
    m_statusSource = nullptr;
    m_status = message;
    emit changed();
    emit failed(message);
}

void CodexProvider::connectAccount(const QString &executable)
{
    if (m_busy) return;
    stop();
    m_executable = executable.isEmpty() ? findCodexExecutable() : executable;
    const QFileInfo file(m_executable);
    if (!file.isAbsolute() || !file.isFile() || !file.isExecutable()
        || file.suffix().compare("cmd", Qt::CaseInsensitive) == 0 || file.suffix().compare("bat", Qt::CaseInsensitive) == 0) {
        fail(tr("Choose the native Codex executable in Settings > AI.")); return;
    }
    const auto profile = m_profile.isEmpty() ? profileDirectory() : m_profile;
    if (!QDir().mkpath(profile)) { fail(tr("Cannot create the private AI profile.")); return; }
    m_models = {};
    m_busy = true; m_statusSource = QT_TR_NOOP("Connecting to Codex..."); emit changed();
    auto process = new QProcess(this);
    m_process = process;
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &key : environment.keys()) {
        if (key.startsWith("CODEX_") || key.startsWith("OPENAI_") || key.startsWith("OTEL_")) environment.remove(key);
    }
    environment.insert("CODEX_HOME", profile);
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(profile);
    process->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    connect(process, &QProcess::readyReadStandardOutput, this, &CodexProvider::read);
    // Diagnostics can include credentials and paths. Drain them without logging.
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::started, this, [this] {
        send("initialize", {{"clientInfo", QJsonObject{{"name", "zima_cad"},
            {"title", "ZIMA-CAD"}, {"version", VERSION}}},
            {"capabilities", QJsonObject{{"experimentalApi", true}}}});
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail(tr("Could not start Codex."));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this] {
        fail(tr("Codex disconnected. Connect again in Settings > AI."));
    });
    QStringList arguments{"app-server", "--listen", "stdio://"};
    // Dedicated profile plus a read-only, tool-disabled planning session. Never
    // inherit a desktop user's plugins, hooks, API billing or repository agents.
    for (const auto &option : {"forced_login_method=\"chatgpt\"", "model_provider=\"openai\"",
            "sandbox_mode=\"read-only\"", "approval_policy=\"never\"", "web_search=\"disabled\"",
            "features.shell_tool=false", "features.unified_exec=false", "features.shell_snapshot=false",
            "features.apply_patch_freeform=false", "features.code_mode=false", "features.js_repl=false",
            "features.multi_agent=false", "features.apps=false",
            "mcp_servers={}", "hooks={}", "project_doc_max_bytes=0", "history.persistence=\"none\"",
            "check_for_update_on_startup=false", "cli_auth_credentials_store=\"keyring\""})
        arguments << "-c" << option;
    process->start(m_executable, arguments);
    m_timeout.start(30000);
}

void CodexProvider::send(const QString &method, const QJsonObject &params)
{
    if (!m_process) return;
    const int id = m_nextId++;
    m_requests[id] = method;
    m_process->write(QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact) + '\n');
}

void CodexProvider::read()
{
    if (!m_process) return;
    m_buffer += m_process->readAllStandardOutput();
    if (m_buffer.size() > 1024 * 1024) { fail(tr("Codex sent an oversized response.")); return; }
    while (m_process) {
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0) break;
        const auto line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;
        QJsonParseError error;
        const auto message = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !message.isObject()) { fail(tr("Invalid Codex protocol response.")); return; }
        receive(message.object());
    }
}

void CodexProvider::account(const QJsonObject &result)
{
    const auto value = result["account"].toObject();
    m_ready = value["type"] == "chatgpt";
    m_busy = false; m_timeout.stop();
    m_statusSource = m_ready ? QT_TR_NOOP("Connected with ChatGPT.") : QT_TR_NOOP("Sign in with your ChatGPT account.");
    emit changed();
    if (m_ready) send("model/list", {{"limit", 100}});
}

void CodexProvider::receive(const QJsonObject &message)
{
    const auto method = message["method"].toString();
    const auto params = message["params"].toObject();
    if (!method.isEmpty() && message.contains("id")) {
        if (method == "item/tool/call" && m_busy && !m_prompt.isEmpty() && !m_thread.isEmpty()
            && params["threadId"].toString() == m_thread
            && (m_turn.isEmpty() || params["turnId"].toString() == m_turn)) {
            const auto id = params["callId"].toString();
            if (id.isEmpty() || m_toolRequests.contains(id) || ++m_toolCount > 64 || !params["arguments"].isObject()) {
                fail(tr("Codex exceeded the tool limit or sent an invalid tool request.")); return;
            }
            m_toolRequests[id] = message["id"];
            m_timeout.stop(); // Local reads and user review have their own cancellation.
            emit toolRequested(id, params["tool"].toString(), params["arguments"].toObject());
            return;
        }
        // No provider may authorize a native tool or permission escalation.
        m_process->write(QJsonDocument(QJsonObject{{"id", message["id"]}, {"error", QJsonObject{
            {"code", -32601}, {"message", "CAD only accepts its registered host tools; native tools are disabled."}}}}).toJson(QJsonDocument::Compact) + '\n');
        fail(tr("An unsupported Codex tool was blocked. Check any earlier operation results."));
        return;
    }
    if (message.contains("id")) {
        const auto request = m_requests.take(message["id"].toInt());
        if (request.isEmpty()) return;
        if (message.contains("error")) {
            // Do not display arbitrary server errors that might repeat tokens.
            fail(tr("Codex could not complete %1 (error %2). Check your account and runtime version.")
                .arg(request).arg(message["error"].toObject()["code"].toInt())); return;
        }
        const auto result = message["result"].toObject();
        if (request == "initialize") {
            m_initialized = true;
            m_process->write("{\"method\":\"initialized\"}\n");
            send("account/read");
        } else if (request == "account/read") account(result);
        else if (request == "account/logout") { m_ready = false; m_models = {}; account({}); }
        else if (request == "model/list") {
            m_models += result["data"].toArray();
            const auto cursor = result["nextCursor"].toString();
            if (!cursor.isEmpty() && m_models.size() < 300) send("model/list", {{"limit", 100}, {"cursor", cursor}});
            emit changed();
        } else if (request == "account/login/start") {
            const QUrl url(result["authUrl"].toString());
            if (url.scheme() != "https" || !(url.host() == "auth.openai.com" || url.host() == "chatgpt.com")) {
                fail(tr("Codex returned an unsupported sign-in address.")); return;
            }
            m_loginId = result["loginId"].toString();
            m_statusSource = QT_TR_NOOP("Complete sign-in in your browser."); emit changed();
            m_timeout.start(5 * 60 * 1000);
            emit loginUrl(url);
        } else if (request == "thread/start") {
            m_thread = result["thread"].toObject()["id"].toString();
            if (m_thread.isEmpty()) { fail(tr("Invalid Codex protocol response.")); return; }
            startTurn();
        } else if (request == "turn/start") {
            m_turn = result["turn"].toObject()["id"].toString();
            if (m_turn.isEmpty()) fail(tr("Invalid Codex protocol response."));
        }
        return;
    }
    if (method == "account/login/completed") {
        if (!m_busy || (!m_loginId.isEmpty() && params["loginId"].toString() != m_loginId)) return;
        m_loginId.clear();
        if (!params["success"].toBool()) { fail(tr("ChatGPT sign-in was not completed.")); return; }
        send("account/read");
    } else if (method == "account/updated" && !m_busy) send("account/read");
    else if (m_busy && !m_prompt.isEmpty() && params["threadId"].toString() == m_thread && !m_thread.isEmpty()) {
        if (method == "item/completed") {
            if (!m_turn.isEmpty() && params["turnId"].toString() != m_turn) return;
            const auto item = params["item"].toObject();
            if (item["type"] == "agentMessage") {
                const auto text = item["text"].toString();
                if (text.size() > 64000) { fail(tr("Codex sent an oversized response.")); return; }
                if (item["phase"] != "commentary") m_response = text;
                emit this->message(text);
            }
        } else if (method == "turn/completed") {
            const auto turn = params["turn"].toObject();
            if (!m_turn.isEmpty() && turn["id"].toString() != m_turn) return;
            if (turn["status"] != "completed") { fail(tr("AI request failed or was interrupted. Check any reported CAD operation results.")); return; }
            if (!m_toolRequests.isEmpty()) { fail(tr("Invalid Codex protocol response.")); return; }
            m_timeout.stop(); m_busy = false;
            const auto response = m_response;
            m_turn.clear(); m_response.clear(); m_prompt.clear();
            m_statusSource = QT_TR_NOOP("Connected with ChatGPT."); emit changed();
            emit answer(response);
        }
    }
}

void CodexProvider::login()
{
    if (!m_initialized || m_busy) return;
    m_busy = true; m_timeout.start(30000);
    m_statusSource = QT_TR_NOOP("Opening ChatGPT sign-in..."); emit changed();
    send("account/login/start", {{"type", "chatgpt"}});
}

void CodexProvider::logout()
{
    if (!m_initialized || m_busy) return;
    m_busy = true; m_ready = false; m_timeout.start(30000); emit changed();
    send("account/logout");
}

void CodexProvider::ask(const QString &text, const QJsonObject &context, const QString &model)
{
    if (!m_ready || m_busy) { emit failed(tr("Connect your ChatGPT account in Settings > AI first.")); return; }
    m_prompt = QString::fromUtf8(QJsonDocument(QJsonObject{{"request", text}, {"context", context}}).toJson(QJsonDocument::Compact));
    if (m_prompt.size() > 100000) { emit failed(tr("The AI request is too large.")); return; }
    const auto cad = context["cad"].toObject();
    const auto key = context["currentDirectory"].toString() + '\n' + cad["active_document"].toString()
        + '\n' + cad["displayed_document"].toString() + '\n' + cad["active_occurrence"].toString() + '\n' + model;
    if (m_contextKey != key || m_turnCount >= 20) newConversation();
    m_contextKey = key;
    m_response.clear(); m_turn.clear(); m_toolCount = 0;
    m_busy = true; m_statusSource = QT_TR_NOOP("Codex is working..."); emit changed();
    QJsonObject params{{"cwd", context["currentDirectory"]}, {"sandbox", "read-only"},
        {"approvalPolicy", "never"}, {"ephemeral", true}, {"baseInstructions", CadAi::instructions()},
        {"developerInstructions", CadAi::hostInstructions()}, {"dynamicTools", CadAi::toolDefinitions()}};
    if (!model.isEmpty()) params["model"] = model;
    if (m_thread.isEmpty()) send("thread/start", params);
    else startTurn();
    m_timeout.start(3 * 60 * 1000);
}

void CodexProvider::startTurn()
{
    ++m_turnCount;
    send("turn/start", {{"threadId", m_thread},
        {"input", QJsonArray{QJsonObject{{"type", "text"}, {"text", m_prompt}}}}});
}

void CodexProvider::newConversation()
{
    if (m_busy) return;
    if (!m_thread.isEmpty()) send("thread/unsubscribe", {{"threadId", m_thread}});
    m_thread.clear(); m_turn.clear(); m_contextKey.clear(); m_turnCount = 0;
}

void CodexProvider::toolResult(const QString &callId, const QJsonObject &result, bool success)
{
    if (!m_process || !m_toolRequests.contains(callId)) return;
    const auto id = m_toolRequests.take(callId);
    const auto text = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    m_process->write(QJsonDocument(QJsonObject{{"id", id}, {"result", QJsonObject{{"success", success},
        {"contentItems", QJsonArray{QJsonObject{{"type", "inputText"}, {"text", text}}}}}}}).toJson(QJsonDocument::Compact) + '\n');
    if (m_toolRequests.isEmpty()) m_timeout.start(3 * 60 * 1000);
}

void CodexProvider::cancel()
{
    stop();
    m_statusSource = QT_TR_NOOP("Cancelled. Connect again to continue."); emit changed();
    emit cancelled();
}

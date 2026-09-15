#pragma once
#include <zima/commands/dispatcher.hpp>
#include <QWidget>
#include <functional>
#include "aitools.h"
#include "aipreferences.h"
#include <memory>
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class AiProvider;
namespace zima::app {
class CommandConsole final : public QWidget {
public:
    using Execute = std::function<zima::commands::Result(const QString&)>;
    struct AiOptions {
        CadAi::CommandSession::Snapshot snapshot;
        std::function<void()> show_settings;
        std::function<CadAi::Preferences()> preferences;
        AiProvider* provider = nullptr;
    };
    explicit CommandConsole(Execute execute, QWidget* parent = nullptr);
    CommandConsole(Execute execute, AiOptions ai, QWidget* parent = nullptr);
    ~CommandConsole() override;
    void focus_input();
private:
    Execute execute_;
    QPlainTextEdit* output_{};
    QLineEdit* input_{};
    QPushButton* run_{};
    QPushButton* stop_{};
    QLabel* ai_status_{};
    QWidget* review_{};
    QPlainTextEdit* review_text_{};
    AiOptions ai_options_;
    AiProvider* provider_{};
    std::unique_ptr<CadAi::CommandSession> ai_session_;
    bool ai_mode_ = false, ai_pending_ = false, local_executing_ = false;
    QString call_id_, target_;
    void submit();
    void submit_ai(const QString& text);
    void finish_ai();
    void refresh_ai();
    void decide(bool allow);
    void receive_tool(const QString& id, const QString& name, const QJsonObject& arguments);
};
}

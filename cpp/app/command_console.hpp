#pragma once
#include <zima/commands/dispatcher.hpp>
#include <QWidget>
#include <functional>
class QPlainTextEdit;
class QLineEdit;
namespace zima::app {
class CommandConsole final : public QWidget {
public:
    using Execute = std::function<zima::commands::Result(const QString&)>;
    explicit CommandConsole(Execute execute, QWidget* parent = nullptr);
    void focus_input();
private:
    Execute execute_;
    QPlainTextEdit* output_{};
    QLineEdit* input_{};
    void submit();
};
}

#include "command_console.hpp"
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QStringList>
#include <utility>

namespace zima::app {
namespace {
class CommandInput final : public QLineEdit {
public:
    using QLineEdit::QLineEdit;
    void remember(const QString& command) {
        if(history_.isEmpty() || history_.last()!=command)history_.append(command);
        if(history_.size()>100)history_.removeFirst();
        index_=history_.size();draft_.clear();
    }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Up && index_>0) {
            if(index_==history_.size())draft_=text();
            setText(history_[--index_]);event->accept();return;
        }
        if(event->key()==Qt::Key_Down && index_<history_.size()) {
            ++index_;setText(index_==history_.size()?draft_:history_[index_]);event->accept();return;
        }
        QLineEdit::keyPressEvent(event);
    }
private:
    QStringList history_;
    qsizetype index_{};
    QString draft_;
};
}
CommandConsole::CommandConsole(Execute execute,QWidget* parent)
    : QWidget(parent),execute_(std::move(execute)) {
    setObjectName("commandConsole");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(6,4,6,4);
    auto* hint=new QLabel(tr("Příkazy CADu: help · documents · context · tree. Historie: ↑ / ↓."),this);
    hint->setWordWrap(true);layout->addWidget(hint);
    output_=new QPlainTextEdit(this);output_->setObjectName("commandConsoleOutput");
    output_->setReadOnly(true);output_->setMaximumBlockCount(1500);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    output_->setMinimumHeight(90);layout->addWidget(output_);
    auto* row=new QHBoxLayout;
    input_=new CommandInput(this);input_->setObjectName("commandConsoleInput");
    input_->setPlaceholderText(tr("Zadejte příkaz nebo JSON požadavek…"));
    input_->setFont(output_->font());row->addWidget(input_,1);
    auto* run=new QPushButton(tr("Spustit"),this);run->setObjectName("commandConsoleRun");row->addWidget(run);
    auto* clear=new QPushButton(tr("Vyčistit výpis"),this);row->addWidget(clear);
    layout->addLayout(row);
    connect(input_,&QLineEdit::returnPressed,this,[this]{submit();});
    connect(run,&QPushButton::clicked,this,[this]{submit();});
    connect(clear,&QPushButton::clicked,output_,&QPlainTextEdit::clear);
}
void CommandConsole::focus_input(){input_->setFocus(Qt::ShortcutFocusReason);}
void CommandConsole::submit() {
    const auto text=input_->text().trimmed();if(text.isEmpty() || !input_->isEnabled())return;
    static_cast<CommandInput*>(input_)->remember(text);input_->clear();input_->setEnabled(false);
    output_->appendPlainText(QStringLiteral("> ")+text);
    const auto result=execute_(text);
    QString report;
    if(result.ok)report=QString::fromStdString(result.data.dump(2, ' ', false, zima::commands::Json::error_handler_t::replace));
    else report=tr("Chyba [%1]: %2").arg(QString::fromStdString(result.code),QString::fromStdString(result.message));
    constexpr qsizetype limit=24000;
    if(report.size()>limit)report=report.left(limit)+QStringLiteral("\n")+tr("… Výpis byl zkrácen.");
    output_->appendPlainText(report);input_->setEnabled(true);focus_input();
}
} // namespace zima::app

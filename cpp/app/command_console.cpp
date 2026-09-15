#include "command_console.hpp"
#include "aiprovider.h"
#include <QScopedValueRollback>
#include <QPlainTextEdit>
#include <QTextDocument>
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
    : CommandConsole(std::move(execute), AiOptions{}, parent) {}
CommandConsole::CommandConsole(Execute execute,AiOptions ai,QWidget* parent)
    : QWidget(parent),execute_(std::move(execute)),ai_options_(std::move(ai)) {
    setObjectName("commandConsole");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(6,4,6,4);layout->setSpacing(3);
    auto* hint=new QLabel(tr("Příkazy CADu: help · documents · context · tree. Historie: ↑ / ↓."),this);
    hint->setWordWrap(true);layout->addWidget(hint);
    output_=new QPlainTextEdit(this);output_->setObjectName("commandConsoleOutput");
    output_->setReadOnly(true);output_->setMaximumBlockCount(1500);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    output_->setMinimumHeight(output_->fontMetrics().height()+2*output_->frameWidth()+2*static_cast<int>(output_->document()->documentMargin()));
    output_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);
    layout->addWidget(output_,1);
    if(ai_options_.snapshot) {
        provider_=ai_options_.provider?ai_options_.provider:cadAiProvider();
        ai_session_=std::make_unique<CadAi::CommandSession>(execute_,ai_options_.snapshot);
        auto* ai_row=new QHBoxLayout;
        ai_status_=new QLabel(this);ai_status_->setObjectName("consoleAiStatus");ai_status_->setTextFormat(Qt::PlainText);
        ai_status_->setWordWrap(true);ai_row->addWidget(ai_status_,1);
        auto* settings=new QPushButton(tr("AI settings"),this);settings->setObjectName("consoleAiSettings");ai_row->addWidget(settings);
        stop_=new QPushButton(tr("Stop"),this);stop_->setObjectName("consoleAiStop");ai_row->addWidget(stop_);
        layout->addLayout(ai_row);
        review_=new QWidget(this);review_->setObjectName("consoleAiReview");
        auto* review_layout=new QVBoxLayout(review_);review_layout->setContentsMargins(0,0,0,0);
        review_text_=new QPlainTextEdit(review_);review_text_->setObjectName("consoleAiReviewText");
        review_text_->setReadOnly(true);review_text_->setMaximumHeight(180);review_text_->setMinimumHeight(65);
        review_text_->setFont(output_->font());review_layout->addWidget(review_text_);
        auto* choices=new QHBoxLayout;
        auto* allow=new QPushButton(tr("Allow command"),review_);allow->setObjectName("consoleAiAllow");
        auto* deny=new QPushButton(tr("Deny"),review_);deny->setObjectName("consoleAiDeny");
        choices->addStretch();choices->addWidget(allow);choices->addWidget(deny);review_layout->addLayout(choices);
        layout->addWidget(review_);review_->hide();
        connect(allow,&QPushButton::clicked,this,[this]{decide(true);});
        connect(deny,&QPushButton::clicked,this,[this]{decide(false);});
        connect(settings,&QPushButton::clicked,this,[this]{if(ai_options_.show_settings)ai_options_.show_settings();});
        connect(stop_,&QPushButton::clicked,this,[this]{if(!local_executing_){finish_ai();provider_->cancel();}});
        connect(provider_,&AiProvider::changed,this,&CommandConsole::refresh_ai);
        connect(provider_,&AiProvider::toolRequested,this,&CommandConsole::receive_tool);
        connect(provider_,&AiProvider::message,this,[this](const QString& text){if(ai_pending_)output_->appendPlainText(text);});
        connect(provider_,&AiProvider::answer,this,[this]{if(ai_pending_)finish_ai();});
        connect(provider_,&AiProvider::failed,this,[this](const QString& text){if(ai_pending_||ai_mode_)output_->appendPlainText(text);finish_ai();});
        connect(provider_,&AiProvider::cancelled,this,[this]{finish_ai();});
    }
    auto* row=new QHBoxLayout;
    input_=new CommandInput(this);input_->setObjectName("commandConsoleInput");
    input_->setPlaceholderText(tr("Zadejte příkaz nebo JSON požadavek…"));
    input_->setFont(output_->font());row->addWidget(input_,1);
    run_=new QPushButton(tr("Spustit"),this);run_->setObjectName("commandConsoleRun");row->addWidget(run_);
    auto* clear=new QPushButton(tr("Vyčistit výpis"),this);row->addWidget(clear);
    layout->addLayout(row);
    connect(input_,&QLineEdit::returnPressed,this,[this]{submit();});
    connect(run_,&QPushButton::clicked,this,[this]{submit();});
    connect(clear,&QPushButton::clicked,output_,&QPlainTextEdit::clear);
    refresh_ai();
}
CommandConsole::~CommandConsole(){if(ai_pending_){provider_->disconnect(this);provider_->cancel();}}
void CommandConsole::focus_input(){input_->setFocus(Qt::ShortcutFocusReason);}
void CommandConsole::submit() {
    const auto text=input_->text().trimmed();if(text.isEmpty() || !input_->isEnabled())return;
    if(ai_pending_ || local_executing_)return; // A draft is never queued or sent automatically.
    if(provider_ && text=="codex" && !ai_mode_) {
        ai_mode_=true;input_->clear();refresh_ai();
        if(!provider_->ready() && ai_options_.show_settings)ai_options_.show_settings();
        return;
    }
    if(ai_mode_) {
        if(text=="/exit") {ai_mode_=false;input_->clear();refresh_ai();return;}
        if(text=="/new") {if(!provider_->busy()){provider_->newConversation();input_->clear();output_->appendPlainText(tr("New AI conversation."));}return;}
        submit_ai(text);return;
    }
    static_cast<CommandInput*>(input_)->remember(text);input_->clear();input_->setEnabled(false);
    output_->appendPlainText(QStringLiteral("> ")+text);
    const auto result=execute_(text);
    QString report;
    const bool catalog_help=result.ok && text==QStringLiteral("help") && result.data.is_array();
    if(catalog_help) {
        QStringList lines;
        for(const auto& command:result.data) {
            QString usage=QString::fromStdString(command.at("name").get<std::string>());
            for(const auto& argument:command.at("arguments")) {
                const auto name=QString::fromStdString(argument.at("name").get<std::string>());
                usage+=argument.at("required").get<bool>()?QStringLiteral(" <")+name+">":QStringLiteral(" [")+name+"]";
            }
            lines.append(usage+QStringLiteral(" — ")+QString::fromStdString(command.at("description").get<std::string>()));
        }
        report=lines.join('\n');
    } else if(result.ok)report=QString::fromStdString(result.data.dump(2, ' ', false, zima::commands::Json::error_handler_t::replace));
    else report=tr("Chyba [%1]: %2").arg(QString::fromStdString(result.code),QString::fromStdString(result.message));
    constexpr qsizetype limit=24000;
    if(!catalog_help && report.size()>limit)report=report.left(limit)+QStringLiteral("\n")+tr("… Výpis byl zkrácen.");
    output_->appendPlainText(report);input_->setEnabled(true);focus_input();
}
void CommandConsole::refresh_ai() {
    if(!provider_ || !run_)return;
    run_->setEnabled(!ai_pending_ && !local_executing_ && (!ai_mode_ || !provider_->busy()));
    stop_->setVisible(ai_mode_ || ai_pending_);stop_->setEnabled(provider_->busy() && !local_executing_);
    ai_status_->setText(ai_mode_ ? (ai_pending_ ? tr("AI · %1 · %2").arg(target_,provider_->status())
        : tr("AI · /new · /exit · %1").arg(provider_->status())) : tr("Type codex to talk to AI about the active tab."));
    input_->setPlaceholderText(ai_mode_ ? tr("Ask about the active Part, Assembly or Drawing…") : tr("Zadejte příkaz nebo JSON požadavek…"));
}
void CommandConsole::submit_ai(const QString& text) {
    if(provider_->busy())return;
    if(!provider_->ready()) {if(ai_options_.show_settings)ai_options_.show_settings();return;}
    const auto context=ai_session_->begin();
    const auto document=context["activeDocument"].toObject();
    target_=document["name"].toString();
    if(target_.isEmpty())target_=context["cad"].toObject()["active_document"].toString();
    if(target_.isEmpty())target_=tr("No active document");
    static_cast<CommandInput*>(input_)->remember(text);input_->clear();
    output_->appendPlainText(tr("> AI [%1]: %2").arg(target_,text));
    ai_pending_=true;refresh_ai();
    provider_->ask(text,context,ai_options_.preferences?ai_options_.preferences().model:QString{});
}
void CommandConsole::finish_ai() {
    ai_pending_=false;call_id_.clear();
    if(ai_session_)ai_session_->cancel();
    if(review_)review_->hide();
    refresh_ai();
}
void CommandConsole::receive_tool(const QString& id,const QString& name,const QJsonObject& arguments) {
    if(!ai_pending_)return;
    if(local_executing_) {
        provider_->toolResult(id,{{"ok",false},{"code","busy"},{"message","Wait for the current CAD command."}},false);return;
    }
    CadAi::CommandSession::Reply result;
    {
        const QScopedValueRollback running(local_executing_,true);
        result=ai_session_->call(name,arguments);
    }
    if(!ai_pending_)return;
    if(result.approval) {
        call_id_=id;
        // An explicitly approved new/open/activate command may have changed
        // the target. Every subsequent review names that actual document.
        const auto document=result.data["activeDocument"].toObject();
        target_=document["name"].toString(tr("No active document"));
        review_text_->setPlainText(tr("Review command for %1").arg(target_)+"\n"+result.review);
        review_->show();
        refresh_ai();
    } else provider_->toolResult(id,result.data,result.success);
}
void CommandConsole::decide(bool allow) {
    if(!ai_pending_ || local_executing_ || call_id_.isEmpty())return;
    const auto id=std::exchange(call_id_,{});review_->hide();
    CadAi::CommandSession::Reply result;
    {
        const QScopedValueRollback running(local_executing_,true);refresh_ai();
        result=ai_session_->decide(allow);
    }
    output_->appendPlainText(result.success ? tr("CAD command completed.")
        : tr("CAD [%1]: %2").arg(result.data["code"].toString(),result.data["message"].toString()));
    if(ai_pending_)provider_->toolResult(id,result.data,result.success);
    refresh_ai();
}
} // namespace zima::app

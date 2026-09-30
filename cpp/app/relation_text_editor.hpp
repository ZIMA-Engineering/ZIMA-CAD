#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QPainter>
#include <QTextBlock>
#include <QKeyEvent>
#include <QFontDatabase>
#include <QRegularExpression>

namespace zima::app {
class RelationSyntax final : public QSyntaxHighlighter {
public:
    explicit RelationSyntax(QTextDocument* document):QSyntaxHighlighter(document){}
    void highlightBlock(const QString& text) override {
        static const QRegularExpression words(QStringLiteral("\\b(if|elseif|else|endif|and|or|not|true|false)\\b"));
        static const QRegularExpression names(QStringLiteral("\\b[a-zA-Z_][a-zA-Z_0-9.]*(?=\\s*\\()"));
        static const QRegularExpression numbers(QStringLiteral("\\b(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?\\b"));
        const auto mark=[&](const QRegularExpression& expression,QColor color,bool bold=false){
            QTextCharFormat format;format.setForeground(color);if(bold)format.setFontWeight(QFont::Bold);
            auto matches=expression.globalMatch(text);while(matches.hasNext()){const auto match=matches.next();setFormat(match.capturedStart(),match.capturedLength(),format);}
        };
        const bool dark=QGuiApplication::palette().color(QPalette::Base).lightness()<128;
        mark(words,dark?QColor("#86bfff"):QColor("#17499a"),true);
        mark(names,dark?QColor("#d9a7ff"):QColor("#713ba1"));
        mark(numbers,dark?QColor("#f5cc87"):QColor("#885100"));
        for(qsizetype i=0;i<text.size();++i) {
            if(text[i]=='#'){setFormat(i,text.size()-i,dark?QColor("#aaaaaa"):QColor("#666666"));break;}
            if(text[i]!='"')continue;
            const auto start=i++;
            for(;i<text.size();++i){if(text[i]=='\\'){++i;continue;}if(text[i]=='"')break;}
            setFormat(start,std::min(i+1,text.size())-start,dark?QColor("#a6d99a"):QColor("#286a20"));
        }
    }
};
class RelationTextEditor final : public QPlainTextEdit {
    class Gutter final : public QWidget {
        RelationTextEditor* editor_;
    public:
        explicit Gutter(RelationTextEditor* editor):QWidget(editor),editor_(editor){}
        void paintEvent(QPaintEvent* event) override {editor_->paint_numbers(event);}
    };
    Gutter* gutter_;
    RelationSyntax* syntax_;
    int gutter_width() const {return 14+fontMetrics().horizontalAdvance('9')*QString::number(blockCount()).size();}
    void margins(){setViewportMargins(gutter_width(),0,0,0);}
    void paint_numbers(QPaintEvent* event) {
        QPainter painter(gutter_);painter.fillRect(event->rect(),palette().color(QPalette::AlternateBase));
        auto block=firstVisibleBlock();int number=block.blockNumber();
        int top=qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        while(block.isValid()&&top<=event->rect().bottom()) {
            const int height=qRound(blockBoundingRect(block).height());
            if(block.isVisible()&&top+height>=event->rect().top()) {
                painter.setPen(palette().color(QPalette::Text));
                painter.drawText(0,top,gutter_->width()-6,fontMetrics().height(),Qt::AlignRight,QString::number(number+1));
            }
            top+=height;block=block.next();++number;
        }
    }
public:
    explicit RelationTextEditor(QWidget* parent=nullptr):QPlainTextEdit(parent),gutter_(new Gutter(this)),syntax_(new RelationSyntax(document())) {
        setObjectName("relationsEditor");setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        setLineWrapMode(QPlainTextEdit::NoWrap);setTabStopDistance(fontMetrics().horizontalAdvance(' ')*4);
        connect(this,&QPlainTextEdit::blockCountChanged,this,[this]{margins();});
        connect(this,&QPlainTextEdit::updateRequest,this,[this](const QRect& rect,int dy){if(dy)gutter_->scroll(0,dy);else gutter_->update(0,rect.y(),gutter_->width(),rect.height());if(rect.contains(viewport()->rect()))margins();});
        connect(this,&QPlainTextEdit::textChanged,this,[this]{setExtraSelections({});});margins();
    }
    void show_error(int line) {
        auto block=document()->findBlockByNumber(std::max(0,line-1));if(!block.isValid())return;
        QTextEdit::ExtraSelection selection;selection.cursor=QTextCursor(block);
        selection.format.setBackground(QColor(220,60,60,70));selection.format.setProperty(QTextFormat::FullWidthSelection,true);
        setExtraSelections({selection});setTextCursor(selection.cursor);ensureCursorVisible();setFocus();
    }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);const auto rect=contentsRect();gutter_->setGeometry(rect.left(),rect.top(),gutter_width(),rect.height());
    }
    void changeEvent(QEvent* event) override {QPlainTextEdit::changeEvent(event);if(event->type()==QEvent::PaletteChange)syntax_->rehighlight();}
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Tab){insertPlainText("    ");return;}
        if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter) {
            const auto text=textCursor().block().text();qsizetype i=0;while(i<text.size()&&(text[i]==' '||text[i]=='\t'))++i;
            QString indent=text.left(i);const auto trimmed=text.trimmed();
            if(trimmed.startsWith("if ")||trimmed.startsWith("elseif ")||trimmed=="else")indent+="    ";
            auto cursor=textCursor();cursor.beginEditBlock();cursor.insertText("\n"+indent);cursor.endEditBlock();setTextCursor(cursor);return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
};
}

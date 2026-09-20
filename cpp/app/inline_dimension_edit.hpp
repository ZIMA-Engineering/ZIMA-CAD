#pragma once
#include <QLineEdit>
#include <QKeyEvent>
namespace zima::app {
// Shared numeric field for Part, Assembly and Drawing dimension values.
class InlineDimensionEdit final : public QLineEdit {
public:
    explicit InlineDimensionEdit(QWidget* parent) : QLineEdit(parent) {
        setObjectName("inlineDimensionValueEdit");
        setMaxLength(4096);
        setAlignment(Qt::AlignCenter);
        setFixedSize(104,28);
        setStyleSheet("QLineEdit { background:#171A1D; color:#FFD400; border:1px solid #00DDF0; border-radius:3px; selection-background-color:#356E22; padding:2px 5px; }");
    }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Escape) {
            setProperty("cancelled",true);hide();deleteLater();event->accept();return;
        }
        QLineEdit::keyPressEvent(event);
        // Enter commits this numeric field only, never an enclosing Properties
        // transaction through QDialog's default button.
        if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)event->accept();
    }
};
}

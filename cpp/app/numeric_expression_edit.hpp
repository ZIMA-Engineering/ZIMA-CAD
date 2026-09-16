#pragma once

#include <zima/document/relations.hpp>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <stdexcept>

namespace zima::app {

inline double numeric_expression_value(QString text) {
    text = text.trimmed();
    text.replace(',', '.');
    return document::evaluate_numeric_expression(text.toStdString());
}

// Retain the familiar numeric field, including its units and arrows, while
// accepting arithmetic. Invalid input must remain available for correction;
// QDoubleSpinBox's default focus-out repair would silently restore old data.
class ExpressionDoubleSpinBox final : public QDoubleSpinBox {
public:
    explicit ExpressionDoubleSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setKeyboardTracking(false);
        setProperty("zimaExpressionInput", true);
        lineEdit()->setMaxLength(4096);
    }
    double expression_value() const {
        const double result = numeric_expression_value(without_units(lineEdit()->text()));
        if (result < minimum() || result > maximum())
            throw std::invalid_argument("Value is outside the allowed range.");
        return result;
    }
protected:
    QValidator::State validate(QString& input, int&) const override {
        try {
            const auto value = numeric_expression_value(without_units(input));
            return value >= minimum() && value <= maximum()
                ? QValidator::Acceptable : QValidator::Intermediate;
        } catch (const std::exception&) { return QValidator::Intermediate; }
    }
    double valueFromText(const QString& text) const override {
        try { return numeric_expression_value(without_units(text)); }
        catch (const std::exception&) { return value(); }
    }
    void focusOutEvent(QFocusEvent* event) override {
        try { static_cast<void>(expression_value()); }
        catch (const std::exception&) { QWidget::focusOutEvent(event); return; }
        QDoubleSpinBox::focusOutEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            try { static_cast<void>(expression_value()); }
            catch (const std::exception&) { event->accept(); return; }
        }
        QDoubleSpinBox::keyPressEvent(event);
    }
private:
    QString without_units(QString text) const {
        if (!prefix().isEmpty() && text.startsWith(prefix())) text.remove(0, prefix().size());
        if (!suffix().isEmpty() && text.endsWith(suffix())) text.chop(suffix().size());
        return text;
    }
};

} // namespace zima::app

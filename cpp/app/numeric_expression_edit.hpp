#pragma once

#include <zima/document/relations.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace zima::app {

inline double numeric_expression_value(QString text) {
    text = text.trimmed();
    text.replace(',', '.');
    return document::evaluate_numeric_expression(text.toStdString());
}

// An optional trailing unit applies to the complete entered expression.
// Canonical results stay in mm/degrees; a suffix never changes document units.
inline double quantity_expression_value(QString text,ui::InputQuantity quantity,
                                       double native_per_unit) {
    text=text.trimmed();
    struct Unit {const char* name;ui::InputQuantity quantity;double scale;};
    constexpr auto length=ui::InputQuantity::Length,angle=ui::InputQuantity::Angle;
    const Unit units[]={{"inches",length,25.4},{"inch",length,25.4},{"mm",length,1},
        {"cm",length,10},{"in",length,25.4},{"m",length,1000},{"\"",length,25.4},
        {"deg",angle,1},{"°",angle,1},{"rad",angle,180./std::numbers::pi}};
    for(const auto& unit:units) {
        const auto suffix=QString::fromUtf8(unit.name);
        if(!text.endsWith(suffix,Qt::CaseInsensitive))continue;
        if(quantity!=unit.quantity)throw std::invalid_argument("Unit does not match the dimension.");
        text.chop(suffix.size());text=text.trimmed();native_per_unit=unit.scale;break;
    }
    // Like existing expression entry, both comma and dot are decimal marks.
    // In particular 0,254 must never be interpreted as a thousands grouping.
    const double value=numeric_expression_value(text)*native_per_unit;
    if(!std::isfinite(value))throw std::invalid_argument("Value is outside the allowed range.");
    return value;
}

// Retain the familiar numeric field, including its units and arrows, while
// accepting arithmetic. Invalid input must remain available for correction;
// QDoubleSpinBox's default focus-out repair would silently restore old data.
template<class NumericField>
class ExpressionSpinBox final : public NumericField {
    static constexpr bool unit_field=std::is_same_v<NumericField,ui::UnitDoubleSpinBox>;
    using NumericField::lineEdit;
public:
    using NumericField::minimum;
    using NumericField::maximum;
    using NumericField::value;
    using NumericField::prefix;
    using NumericField::suffix;
    explicit ExpressionSpinBox(QWidget* parent = nullptr) requires (!unit_field)
        : NumericField(parent) { initialize(); }
    explicit ExpressionSpinBox(ui::InputQuantity quantity,QWidget* parent = nullptr) requires unit_field
        : NumericField(quantity,parent) { initialize(); }
    double expression_value() const {
        const double result = expression_from_text(lineEdit()->text());
        if (result < minimum() || result > maximum())
            throw std::invalid_argument("Value is outside the allowed range.");
        return result;
    }
protected:
    QValidator::State validate(QString& input, int&) const override {
        try {
            const auto value = expression_from_text(input);
            return value >= minimum() && value <= maximum()
                ? QValidator::Acceptable : QValidator::Intermediate;
        } catch (const std::exception&) { return QValidator::Intermediate; }
    }
    double valueFromText(const QString& text) const override {
        try { return expression_from_text(text); }
        catch (const std::exception&) { return value(); }
    }
    void focusOutEvent(QFocusEvent* event) override {
        double pending{};
        try { pending=expression_value(); }
        catch (const std::exception&) { QWidget::focusOutEvent(event); return; }
        QDoubleSpinBox::focusOutEvent(event);
        // Qt formats -0 as 0. The authored sign can select a dimension's
        // solution side, so preserve it until the properties transaction reads it.
        if(pending==0.0&&std::signbit(pending))lineEdit()->setText(prefix()+QStringLiteral("-0")+suffix());
    }
    void keyPressEvent(QKeyEvent* event) override {
        bool negative_zero=false;
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            try { const auto pending=expression_value();negative_zero=pending==0.0&&std::signbit(pending); }
            catch (const std::exception&) { event->accept(); return; }
        }
        QDoubleSpinBox::keyPressEvent(event);
        if(negative_zero)lineEdit()->setText(prefix()+QStringLiteral("-0")+suffix());
    }
private:
    void initialize() {
        this->setKeyboardTracking(false);
        this->setProperty("zimaExpressionInput", true);
        lineEdit()->setMaxLength(4096);
    }
    double expression_from_text(const QString& text) const {
        if constexpr(unit_field) {
            if(this->unchanged_text(text))return value();
            return quantity_expression_value(without_units(text),this->quantity(),this->native_per_unit());
        } else return numeric_expression_value(without_units(text));
    }
    QString without_units(QString text) const {
        if (!prefix().isEmpty() && text.startsWith(prefix())) text.remove(0, prefix().size());
        if (!suffix().isEmpty() && text.endsWith(suffix())) text.chop(suffix().size());
        return text;
    }
};

using ExpressionDoubleSpinBox=ExpressionSpinBox<QDoubleSpinBox>;
using UnitExpressionDoubleSpinBox=ExpressionSpinBox<ui::UnitDoubleSpinBox>;

} // namespace zima::app

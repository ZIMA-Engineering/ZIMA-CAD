#pragma once

#include <zima/document/physical_properties.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVariant>
#include <algorithm>
#include <numbers>

namespace zima::ui {
enum class InputQuantity { Scalar, Length, Angle };

inline QString document_unit(const QWidget* owner,const char* key,const char* fallback) {
    for(auto* current=owner;current;current=current->parentWidget()) {
        const auto units=current->property("zimaDocumentUnits");
        if(units.isValid())return units.toMap().value(QString::fromLatin1(key),QString::fromLatin1(fallback)).toString();
    }
    return QString::fromLatin1(fallback);
}

// Public values, ranges and valueChanged signals remain canonical mm/degrees.
// Conversion belongs solely to text entry/presentation. Keeping native storage
// precision separate from displayed decimals prevents inch input being rounded
// to (for example) three decimal places in millimetres by QDoubleSpinBox.
class UnitDoubleSpinBox : public QDoubleSpinBox {
public:
    explicit UnitDoubleSpinBox(InputQuantity quantity,QWidget* parent=nullptr)
        :QDoubleSpinBox(parent),display_decimals_(numeric_decimal_places(parent)) {
        // Qt rounds the stored value using this precision. Keep its complete
        // supported double range; visible precision is maintained separately.
        QDoubleSpinBox::setDecimals(323);
        setProperty("zimaUnitDisplayDecimals",display_decimals_);
        setKeyboardTracking(false);
        set_quantity(quantity);
    }
    void set_display_decimals(int decimals) {
        display_decimals_=std::clamp(decimals,0,12);
        setProperty("zimaUnitDisplayDecimals",display_decimals_);
        refresh_text();
    }
    [[nodiscard]] int display_decimals()const{return display_decimals_;}
    [[nodiscard]] double native_per_unit()const{return native_per_unit_;}
    [[nodiscard]] InputQuantity quantity()const{return quantity_;}
    void set_quantity(InputQuantity quantity) {
        quantity_=quantity;
        QString symbol;
        native_per_unit_=1;
        if(quantity==InputQuantity::Length) {
            symbol=document_unit(this,"Length","mm");
            native_per_unit_=document::length_unit_mm(symbol.toStdString());
        } else if(quantity==InputQuantity::Angle) {
            const auto unit=document_unit(this,"Angle","deg");
            native_per_unit_=unit=="rad"?180./std::numbers::pi:1.;
            symbol=unit=="rad"?QStringLiteral("rad"):QString::fromUtf8("°");
        }
        QDoubleSpinBox::setSuffix(symbol.isEmpty()?QString{}:QStringLiteral(" ")+symbol);
        refresh_text();
    }
protected:
    [[nodiscard]] QString without_units(QString text)const {
        text=text.trimmed();
        if(!prefix().isEmpty()&&text.startsWith(prefix()))text.remove(0,prefix().size());
        const auto unit=suffix().trimmed();
        if(!unit.isEmpty()&&text.endsWith(unit))text.chop(unit.size());
        return text.trimmed();
    }
    [[nodiscard]] bool unchanged_text(const QString& text)const {
        return without_units(text)==textFromValue(value());
    }
    QString textFromValue(double native)const override {
        return locale().toString(native/native_per_unit_,'f',display_decimals_);
    }
    double valueFromText(const QString& text)const override {
        if(unchanged_text(text))return value();
        bool ok{};const double shown=locale().toDouble(without_units(text),&ok);
        return ok?shown*native_per_unit_:value();
    }
    QValidator::State validate(QString& text,int&)const override {
        if(unchanged_text(text))return QValidator::Acceptable;
        bool ok{};const double shown=locale().toDouble(without_units(text),&ok);
        const double native=shown*native_per_unit_;
        return ok&&std::isfinite(native)&&native>=minimum()&&native<=maximum()
            ?QValidator::Acceptable:QValidator::Intermediate;
    }
    void stepBy(int steps)override {
        const double authored_step=singleStep();
        QDoubleSpinBox::setSingleStep(authored_step*native_per_unit_);
        QDoubleSpinBox::stepBy(steps);
        QDoubleSpinBox::setSingleStep(authored_step);
    }
    void refresh_text() {
        const QSignalBlocker blocked(lineEdit());
        lineEdit()->setText(prefix()+textFromValue(value())+suffix());
        updateGeometry();
    }
private:
    // Changing Qt storage decimals would round canonical values. Call the
    // explicit presentation setter instead when configuring a unit field.
    using QDoubleSpinBox::setDecimals;
    InputQuantity quantity_{InputQuantity::Scalar};
    int display_decimals_{3};
    double native_per_unit_{1};
};
} // namespace zima::ui

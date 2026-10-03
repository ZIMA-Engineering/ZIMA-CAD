#pragma once
#include "annotation_symbols.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <zima/document/dimension_unit_conversion.hpp>
namespace zima::app {
class DimensionTextFields final : public QWidget {
  public:
    DimensionTextFields(kernel::DimensionTextStyle initial, QWidget *parent, bool precision = true, bool angular = false, bool document_units = false, bool unitless = false)
        : QWidget(parent), initial_(initial),unitless_(unitless) {
        form_ = new QFormLayout(this);
        // Existing manufacturing annotations retain their declared basis.
        // Only an unbound new specification starts in the document's units.
        const auto native_unit=angular?"deg":"mm";
        const auto native_suffix=angular?QString::fromUtf8("°"):QStringLiteral("mm");
        const auto suffix=QString::fromStdString(initial.suffix).trimmed();
        if(document_units&&!document::dimension_has_specification(initial)&&initial.text_override.empty()&&
            (suffix.isEmpty()||suffix==native_suffix)) {
            const auto selected=ui::document_unit(parent,angular?"Angle":"Length",native_unit);
            if(selected!=native_unit) {
                initial.value_unit=selected.toStdString();
                if(!initial.suffix.empty())initial.suffix=selected=="deg"?"°":selected.toStdString();
            }
        }
        displayed_initial_=initial;
        const auto unit=initial.value_unit.empty()?(angular?"deg":"mm"):initial.value_unit;
        annotation_unit_=unit;
        auto* units=new QLabel(QString::fromStdString(unit=="deg"?"°":unit),this);
        units->setObjectName("dimensionAnnotationUnits");
        form_->addRow(tr("Jednotky hodnoty a tolerancí"),units);
        if(unitless_)form_->setRowVisible(units,false);
        form_->addRow(tr("Text před hodnotou"),
                      symbol_field(prefix_, initial.prefix, "sketchDimensionPrefix", this));
        form_->addRow(tr("Text za hodnotou"),
                      symbol_field(suffix_, initial.suffix, "sketchDimensionSuffix", this));
        display_text_override_ = new QLineEdit(QString::fromStdString(initial.text_override), this);
        display_text_override_->setObjectName("sketchDimensionDisplayText");
        display_text_override_->setPlaceholderText(tr("Prázdné = zobrazit skutečnou hodnotu"));
        form_->addRow(tr("Text místo hodnoty"), display_text_override_);
        basic_ = new QCheckBox(tr("Teoreticky přesná kóta (rámeček)"), this);
        basic_->setObjectName("dimensionBasic");
        basic_->setToolTip(tr("Teoreticky přesná kóta nemá rozměrové tolerance. Platí i pro úhly, poloměry a průměry."));
        basic_->setChecked(kernel::dimension_is_basic(initial));
        form_->addRow(basic_);
        tolerance_mode_ = new QComboBox(this);
        tolerance_mode_->setObjectName("sketchDimensionToleranceMode");
        tolerance_mode_->addItem(tr("Bez tolerance"), "");
        tolerance_mode_->addItem(tr("Symetrická"), "symmetric");
        tolerance_mode_->addItem(tr("Jednostranná odchylka"), "single_deviation");
        tolerance_mode_->addItem(tr("Horní a dolní odchylka"), "deviations");
        tolerance_mode_->setCurrentIndex(
            std::max(0, tolerance_mode_->findData(QString::fromStdString(initial.tolerance_mode))));
        form_->addRow(tr("Tolerance"), tolerance_mode_);
        symmetric_tolerance_ = new QLineEdit(QString::fromStdString(initial.symmetric_tolerance), this);
        single_tolerance_ = new QLineEdit(QString::fromStdString(initial.single_tolerance), this);
        upper_tolerance_ = new QLineEdit(QString::fromStdString(initial.upper_tolerance), this);
        lower_tolerance_ = new QLineEdit(QString::fromStdString(initial.lower_tolerance), this);
        symmetric_tolerance_->setObjectName("sketchSymmetricTolerance");
        single_tolerance_->setObjectName("sketchSingleTolerance");
        upper_tolerance_->setObjectName("sketchUpperDeviation");
        lower_tolerance_->setObjectName("sketchLowerDeviation");
        form_->addRow(tr("Hodnota ±"), symmetric_tolerance_);
        form_->addRow(tr("Odchylka"), single_tolerance_);
        form_->addRow(tr("Horní odchylka"), upper_tolerance_);
        form_->addRow(tr("Dolní odchylka"), lower_tolerance_);

        decimals_ = new QSpinBox(this);
        decimals_->setObjectName("dimensionDecimals");
        decimals_->setRange(0, 12);
        decimals_->setValue(initial.decimals);
        form_->addRow(tr("Desetinná místa"), decimals_);
        form_->setRowVisible(decimals_, precision);
        trailing_zeros_=new QCheckBox(tr("Zachovat koncové nuly"),this);
        trailing_zeros_->setObjectName("dimensionTrailingZeros");
        trailing_zeros_->setChecked(initial.keep_trailing_zeros);
        form_->addRow(trailing_zeros_);
        connect(tolerance_mode_, &QComboBox::currentIndexChanged, this,
                [this] { refresh_tolerance_fields(); });
        connect(basic_, &QCheckBox::toggled, this, [this] { refresh_tolerance_fields(); });
        refresh_tolerance_fields();
    }
    // Changing the dimension kind defines a different quantity; it is not a
    // length/angle unit conversion. Follow the existing kind-change policy.
    void set_angular_quantity(bool angular) {
        annotation_unit_=angular?"deg":"mm";
        if(!initial_.value_unit.empty())initial_.value_unit=annotation_unit_;
        if(!displayed_initial_.value_unit.empty())displayed_initial_.value_unit=annotation_unit_;
        findChild<QLabel*>("dimensionAnnotationUnits")->setText(angular?QString::fromUtf8("°"):QStringLiteral("mm"));
    }
    kernel::DimensionTextStyle value() const {
        auto result = displayed_initial_;
        result.prefix = prefix_->text().toStdString();
        result.suffix = suffix_->text().toStdString();
        result.text_override = display_text_override_->text().trimmed().toStdString();
        result.tolerance_mode = basic_->isChecked() ? "basic" : tolerance_mode_->currentData().toString().toStdString();
        result.symmetric_tolerance = symmetric_tolerance_->text().trimmed().toStdString();
        result.single_tolerance = single_tolerance_->text().trimmed().toStdString();
        result.upper_tolerance = upper_tolerance_->text().trimmed().toStdString();
        result.lower_tolerance = lower_tolerance_->text().trimmed().toStdString();

        result.decimals = decimals_->value();
        result.keep_trailing_zeros=trailing_zeros_->isChecked();
        if(result==displayed_initial_)return initial_;
        if(!unitless_&&result.value_unit.empty()&&(result.keep_trailing_zeros!=displayed_initial_.keep_trailing_zeros||result.decimals!=displayed_initial_.decimals||
            result.tolerance_mode!=displayed_initial_.tolerance_mode||result.symmetric_tolerance!=displayed_initial_.symmetric_tolerance||
            result.single_tolerance!=displayed_initial_.single_tolerance||result.upper_tolerance!=displayed_initial_.upper_tolerance||result.lower_tolerance!=displayed_initial_.lower_tolerance))
            result.value_unit=annotation_unit_;
        return result;
    }

  private:
    kernel::DimensionTextStyle initial_,displayed_initial_;
    bool unitless_{};
    QCheckBox* trailing_zeros_{};
    std::string annotation_unit_;
    QFormLayout *form_{};
    QLineEdit *prefix_{}, *suffix_{}, *display_text_override_{}, *symmetric_tolerance_{},
        *single_tolerance_{}, *upper_tolerance_{}, *lower_tolerance_{};
    QComboBox *tolerance_mode_{};
    QCheckBox *basic_{};
    QSpinBox *decimals_{};
    void refresh_tolerance_fields() {
        tolerance_mode_->setEnabled(!basic_->isChecked());
        for(auto* field:{symmetric_tolerance_,single_tolerance_,upper_tolerance_,lower_tolerance_})
            field->setEnabled(!basic_->isChecked());
        const auto mode = tolerance_mode_->currentData().toString();
        form_->setRowVisible(symmetric_tolerance_, mode == "symmetric");
        form_->setRowVisible(single_tolerance_, mode == "single_deviation");
        form_->setRowVisible(upper_tolerance_, mode == "deviations");
        form_->setRowVisible(lower_tolerance_, mode == "deviations");
    }

    QWidget *symbol_field(QLineEdit *&edit, const std::string &value, const char *name, QWidget *parent) {
        auto *widget = new QWidget(parent);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit(QString::fromStdString(value), widget);
        edit->setObjectName(name);
        layout->addWidget(edit, 1);
        auto *symbols = annotation_symbols(widget,[edit](const QString& symbol) {
            edit->insert(symbol);edit->setFocus();
        });
        layout->addWidget(symbols);
        return widget;
    }
};
class DimensionPlacementFields final : public QWidget {
  public:
    DimensionPlacementFields(kernel::ViewerDimension dimension, kernel::DimensionLayout initial,
                             QWidget *parent, bool drawing = false)
        : QWidget(parent), preserved_(initial), radius_(dimension.kind==kernel::ViewerDimensionKind::Radius) {
        form_ = new QFormLayout(this);
        plane_ = new QComboBox(this);
        plane_->setObjectName("dimensionProjectionPlane");
        plane_->addItems({tr("Původní rovina modelu"), tr("Kolmá rovina (90°)"), tr("Opačná strana (180°)"),
                          tr("Kolmá rovina (270°)")});
        plane_->setCurrentIndex(initial.plane_quarter_turns);
        plane_->setEnabled(dimension.kind == kernel::ViewerDimensionKind::Linear);
        if (dimension.kind == kernel::ViewerDimensionKind::Radius ||
            dimension.kind == kernel::ViewerDimensionKind::Diameter)
            plane_->setCurrentIndex(0);
        form_->addRow(tr("Rovina zobrazení"), plane_);
        attach_ = new QCheckBox(tr("Uchytit k obálce modelu"), this);
        attach_->setObjectName("dimensionEnvelopeAttachment");
        attach_->setChecked(initial.envelope_offset.has_value());
        form_->addRow(attach_);
        const auto field = [&](const char *name, double value,ui::InputQuantity quantity=ui::InputQuantity::Length) {
            QDoubleSpinBox* f;
            if(drawing) {
                f=new QDoubleSpinBox(this);
                f->setDecimals(ui::numeric_decimal_places(parent));
                f->setSuffix(quantity==ui::InputQuantity::Angle?QString::fromUtf8("°"):QStringLiteral("mm"));
            } else {
                auto* units=new ui::UnitDoubleSpinBox(quantity,this);
                units->set_display_decimals(ui::numeric_decimal_places(parent));f=units;
            }
            f->setObjectName(name);
            f->setRange(-1000000, 1000000);
            f->setValue(value);
            return f;
        };
        offset_ = field("dimensionEnvelopeOffset", initial.envelope_offset.value_or(8));
        offset_->setMinimum(0);
        form_->addRow(tr("Odsazení od obálky"), offset_);
        along_ = field("dimensionTextAlong", initial.text_along);
        outward_ = field("dimensionTextOutward", initial.line_offset);
        form_->addRow(tr("Posunutí textu podél kóty"), along_);
        form_->addRow(tr("Posunutí kótovací čáry"), outward_);
        offset_->setEnabled(attach_->isChecked());
        connect(attach_, &QCheckBox::toggled, offset_, &QDoubleSpinBox::setEnabled);

        if (drawing) {
            form_->setRowVisible(plane_, false);
            form_->setRowVisible(attach_, false);
            form_->setRowVisible(offset_, false);
        }
        rotation_ = field("dimensionRadiusRotation", initial.radius_rotation_degrees,ui::InputQuantity::Angle);
        form_->addRow(tr("Poloha šipky na kružnici"), rotation_);
        form_->setRowVisible(rotation_, dimension.kind == kernel::ViewerDimensionKind::Radius ||
                                            dimension.kind == kernel::ViewerDimensionKind::Diameter);
        transverse_ = field("dimensionTextTransverse", initial.text_outward);
        form_->addRow(tr("Posunutí textu napříč kótou"), transverse_);
        arrows_ = new QCheckBox(tr("Obrátit šipky"), this);
        arrows_->setObjectName("dimensionArrowsReversed");
        arrows_->setChecked(initial.arrows_reversed);
        form_->addRow(arrows_);
        shortened_ = new QCheckBox(tr("Poloměr bez čáry do středu"), this);
        shortened_->setObjectName("dimensionRadiusShortened");
        shortened_->setChecked(initial.radius_center_line_hidden);
        form_->addRow(shortened_);
        form_->setRowVisible(shortened_, dimension.kind == kernel::ViewerDimensionKind::Radius);
        const bool radial = dimension.kind == kernel::ViewerDimensionKind::Radius ||
                            dimension.kind == kernel::ViewerDimensionKind::Diameter;
        form_->setRowVisible(outward_, !radial);
        form_->setRowVisible(transverse_, !radial);
        if (dimension.kind == kernel::ViewerDimensionKind::Radius) {
            arrows_->setEnabled(!shortened_->isChecked());
            connect(shortened_, &QCheckBox::toggled, arrows_,
                    [this](bool shortened) { arrows_->setEnabled(!shortened); });
        }
    }
    void set_value(const kernel::DimensionLayout& layout) {
        const QSignalBlocker plane(plane_),attach(attach_),offset(offset_),along(along_),
            outward(outward_),transverse(transverse_),rotation(rotation_),arrows(arrows_),shortened(shortened_);
        preserved_=layout;
        plane_->setCurrentIndex(layout.plane_quarter_turns);
        attach_->setChecked(layout.envelope_offset.has_value());offset_->setValue(layout.envelope_offset.value_or(8));
        offset_->setEnabled(attach_->isChecked());
        along_->setValue(layout.text_along);outward_->setValue(layout.line_offset);
        transverse_->setValue(layout.text_outward);rotation_->setValue(layout.radius_rotation_degrees);
        arrows_->setChecked(layout.arrows_reversed);shortened_->setChecked(layout.radius_center_line_hidden);
        if(radius_)arrows_->setEnabled(!shortened_->isChecked());
    }
    kernel::DimensionLayout value() const {
        auto value = preserved_;
        value.envelope_offset.reset();
        value.plane_quarter_turns = plane_->currentIndex();
        if (attach_->isChecked())
            value.envelope_offset = offset_->value();
        value.text_along = along_->value();
        value.text_outward = transverse_->value();
        value.line_offset = outward_->value();
        value.arrows_reversed = arrows_->isChecked();
        value.radius_center_line_hidden = shortened_->isChecked();
        value.radius_rotation_degrees = rotation_->value();
        kernel::validate_dimension_layout(value);
        return value;
    }

  private:
    kernel::DimensionLayout preserved_;
    bool radius_{};
    QFormLayout *form_{};
    QComboBox *plane_{};
    QCheckBox *attach_{}, *arrows_{}, *shortened_{};
    QDoubleSpinBox *offset_{}, *along_{}, *outward_{}, *transverse_{}, *rotation_{};
};
class DimensionPropertiesDialog final : public ui::PropertiesSubWindow {
  public:
    DimensionPropertiesDialog(kernel::ViewerDimension dimension, kernel::DimensionLayout initial,
                              std::function<void(kernel::DimensionLayout)> commit, QWidget *parent)
        : PropertiesSubWindow(tr("Vlastnosti kóty"), parent), commit_(std::move(commit)) {
        setObjectName("dimensionPropertiesDialog");
        set_initial_size({440, 510});
        auto *tabs = new QTabWidget(this);
        content_layout()->addWidget(tabs);
        auto *page = new QWidget(tabs);
        auto *column = new QVBoxLayout(page);
        const bool angular=dimension.kind==kernel::ViewerDimensionKind::Angular;
        const bool scalar=dimension.label_only&&dimension.unit_suffix.empty();
        const auto unit=ui::document_unit(parent,angular?"Angle":"Length",angular?"deg":"mm");
        const double scale=scalar?1.:kernel::dimension_annotation_scale(unit.toStdString(),dimension.kind);
        const auto suffix=scalar?QString{}:unit=="deg"?QString::fromUtf8("°"):unit;
        auto *value =
            new QLabel(tr("Měřená hodnota: %1%2")
                           .arg(dimension.value/scale, 0, 'f', ui::numeric_decimal_places(parent))
                           .arg(suffix),page);
        value->setObjectName("dimensionMeasuredValue");
        column->addWidget(value);
        text_ = new DimensionTextFields(initial.text_style.value_or(kernel::dimension_text_style(dimension)),
                                        page,true,angular,!scalar,scalar);
        column->addWidget(text_);
        column->addStretch();
        placement_ = new DimensionPlacementFields(dimension, initial, tabs);
        tabs->addTab(page, tr("Hodnota a tolerance"));
        tabs->addTab(placement_, tr("Umístění"));
    }

  protected:
    bool submit() override {
        auto layout = placement_->value();
        layout.text_style = text_->value();
        commit_(layout);
        return true;
    }

  private:
    DimensionTextFields *text_{};
    DimensionPlacementFields *placement_{};
    std::function<void(kernel::DimensionLayout)> commit_;
};
} // namespace zima::app

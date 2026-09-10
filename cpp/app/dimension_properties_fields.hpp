#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/ui/properties_subwindow.hpp>
namespace zima::app {
class DimensionTextFields final : public QWidget {
  public:
    DimensionTextFields(kernel::DimensionTextStyle initial, QWidget *parent, bool precision = true)
        : QWidget(parent), initial_(initial) {
        form_ = new QFormLayout(this);
        form_->addRow(tr("Text před hodnotou"),
                      symbol_field(prefix_, initial.prefix, "sketchDimensionPrefix", this));
        form_->addRow(tr("Text za hodnotou"),
                      symbol_field(suffix_, initial.suffix, "sketchDimensionSuffix", this));
        display_text_override_ = new QLineEdit(QString::fromStdString(initial.text_override), this);
        display_text_override_->setObjectName("sketchDimensionDisplayText");
        display_text_override_->setPlaceholderText(tr("Prázdné = zobrazit skutečnou hodnotu"));
        form_->addRow(tr("Text místo hodnoty"), display_text_override_);
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
        connect(tolerance_mode_, &QComboBox::currentIndexChanged, this,
                [this] { refresh_tolerance_fields(); });
        refresh_tolerance_fields();
    }
    kernel::DimensionTextStyle value() const {
        auto result = initial_;
        result.prefix = prefix_->text().toStdString();
        result.suffix = suffix_->text().toStdString();
        result.text_override = display_text_override_->text().trimmed().toStdString();
        result.tolerance_mode = tolerance_mode_->currentData().toString().toStdString();
        result.symmetric_tolerance = symmetric_tolerance_->text().trimmed().toStdString();
        result.single_tolerance = single_tolerance_->text().trimmed().toStdString();
        result.upper_tolerance = upper_tolerance_->text().trimmed().toStdString();
        result.lower_tolerance = lower_tolerance_->text().trimmed().toStdString();

        result.decimals = decimals_->value();
        return result;
    }

  private:
    kernel::DimensionTextStyle initial_;
    QFormLayout *form_{};
    QLineEdit *prefix_{}, *suffix_{}, *display_text_override_{}, *symmetric_tolerance_{},
        *single_tolerance_{}, *upper_tolerance_{}, *lower_tolerance_{};
    QComboBox *tolerance_mode_{};
    QSpinBox *decimals_{};
    void refresh_tolerance_fields() {
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
        auto *symbols = new QToolButton(widget);
        symbols->setText(QStringLiteral("⌀"));
        symbols->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(symbols);
        for (const auto &symbol :
             {QStringLiteral("⌀"), QStringLiteral("○"), QStringLiteral("●"), QStringLiteral("R"),
              QStringLiteral("SR"), QStringLiteral("S⌀"), QStringLiteral("□"), QStringLiteral("⌴"),
              QStringLiteral("⌵"), QStringLiteral("↧"), QStringLiteral("⌒"), QStringLiteral("∠"),
              QStringLiteral("°"), QStringLiteral("±"), QStringLiteral("×"), QStringLiteral("≈")}) {
            auto *action = menu->addAction(symbol);
            QObject::connect(action, &QAction::triggered, edit, [edit, symbol] {
                edit->insert(symbol);
                edit->setFocus();
            });
        }
        symbols->setMenu(menu);
        layout->addWidget(symbols);
        return widget;
    }
};
class DimensionPlacementFields final : public QWidget {
  public:
    DimensionPlacementFields(kernel::ViewerDimension dimension, kernel::DimensionLayout initial,
                             QWidget *parent, bool drawing = false)
        : QWidget(parent), preserved_(initial) {
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
        const auto field = [&](const char *name, double value) {
            auto *f = new QDoubleSpinBox(this);
            f->setObjectName(name);
            f->setRange(-1000000, 1000000);
            f->setDecimals(ui::numeric_decimal_places(parent));
            f->setSuffix("mm");
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
        rotation_ = field("dimensionRadiusRotation", initial.radius_rotation_degrees);
        rotation_->setSuffix("°");
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
        auto *value =
            new QLabel(tr("Měřená hodnota: %1%2")
                           .arg(dimension.value, 0, 'f', ui::numeric_decimal_places(parent))
                           .arg(QString::fromStdString(kernel::dimension_unit_text(dimension.unit_suffix))),
                       page);
        value->setObjectName("dimensionMeasuredValue");
        column->addWidget(value);
        text_ = new DimensionTextFields(initial.text_style.value_or(kernel::dimension_text_style(dimension)),
                                        page);
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

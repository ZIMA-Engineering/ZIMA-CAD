#include "sketch_dimension_properties_dialog.hpp"
#include "dimension_properties_fields.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QWidget>

namespace zima::app {
namespace {

QDoubleSpinBox* dimension_field(double value, const char* name, QWidget* parent) {
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(name);
    field->setRange(-1'000'000.0, 1'000'000.0);
    field->setDecimals(zima::ui::numeric_decimal_places(parent));
    field->setSingleStep(1.0);
    field->setSuffix("mm");
    field->setValue(value);
    return field;
}

}  // namespace

SketchDimensionPropertiesDialog::SketchDimensionPropertiesDialog(
    zima::sketcher::SketchDimension initial, bool edit_mode,
    CommitCallback commit, QWidget* parent, QString custom_title)
    : PropertiesSubWindow(custom_title.isEmpty()
          ? tr("Vlastnosti kóty")
          : std::move(custom_title), parent),
      initial_(std::move(initial)), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
    setMinimumHeight(560);
    form_ = new QFormLayout;
    value_ = dimension_field(initial_.value, "sketchDimensionValue", this);
    form_->addRow(tr("Jmenovitá hodnota"), value_);
    driving_ = new QCheckBox(tr("Řídicí kóta"), this);
    driving_->setObjectName("sketchDimensionDriving");
    driving_->setChecked(initial_.driving);
    value_->setEnabled(initial_.driving);
    form_->addRow(tr("Stav kóty"), driving_);
    locked_ = new QCheckBox(tr("Zamknout rozměr"), this);
    locked_->setObjectName("sketchDimensionLocked");
    locked_->setChecked(initial_.locked);
    locked_->setEnabled(initial_.driving);
    value_->setEnabled(initial_.driving);
    form_->addRow(tr("Ochrana hodnoty"), locked_);
    if (initial_.kind == zima::sketcher::DimensionKind::Angle ||
        initial_.kind == zima::sketcher::DimensionKind::AngleBetween ||
        initial_.kind == zima::sketcher::DimensionKind::EllipseRotation) {
        value_->setRange(-180.0, 180.0);
        value_->setSuffix(" °");
    } else if (initial_.kind == zima::sketcher::DimensionKind::AngleSymmetric) {
        value_->setRange(0.0, 360.0);
        value_->setSuffix(" °");
    } else if (initial_.kind ==
               zima::sketcher::DimensionKind::AngleThreePoint) {
        value_->setSuffix(" °");
    }
    tabs_=new QTabWidget(this);content_layout()->addWidget(tabs_);
    auto* values=new QWidget(tabs_);auto* column=new QVBoxLayout(values);column->addLayout(form_);
    kernel::DimensionTextStyle style{initial_.prefix,initial_.suffix,initial_.display_text_override,3,initial_.tolerance_mode,initial_.symmetric_tolerance,initial_.single_tolerance,initial_.upper_tolerance,initial_.lower_tolerance};
    text_fields_=new DimensionTextFields(style,values,false);column->addWidget(text_fields_);column->addStretch();
    tabs_->addTab(values,tr("Hodnota a tolerance"));
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(value_, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, [this](double) { error_->clear(); });
    connect(driving_, &QCheckBox::toggled, this, [this](bool driving) {
        // A reference dimension is a measurement, not an editable command.
        // Restore the last measured value if the user first typed a new
        // number and only then changed the dimension to reference mode.
        if (!driving) value_->setValue(initial_.value);
        if (!driving) locked_->setChecked(false);
        locked_->setEnabled(driving);
        value_->setEnabled(driving);
        error_->clear();
    });
    connect(locked_, &QCheckBox::toggled, this, [this](bool) {
        // Locked protects the value from direct geometry dragging. An
        // intentional numeric edit in Properties remains available.
        value_->setEnabled(driving_->isChecked());
        error_->clear();
    });
}

void SketchDimensionPropertiesDialog::set_presentation(kernel::ViewerDimension source,kernel::DimensionLayout initial,std::function<void(kernel::DimensionLayout)> pending) {
    placement_fields_=new DimensionPlacementFields(source,initial,tabs_);
    tabs_->addTab(placement_fields_,tr("Umístění"));pending_layout_=std::move(pending);
}

void SketchDimensionPropertiesDialog::set_dimension_identifier(const QString& identifier) {
    auto* label = new QLabel(identifier.isEmpty() ? tr("Přidělí se po potvrzení") : identifier, this);
    label->setObjectName("sketchDimensionIdentifier");
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form_->insertRow(0, tr("Identifikace kóty"), label);
}

bool SketchDimensionPropertiesDialog::submit() {
    auto result = initial_;
    result.value = value_->value();
    result.driving = driving_->isChecked();
    result.locked = locked_->isChecked();
    const auto style=text_fields_->value();
    result.prefix=style.prefix;result.suffix=style.suffix;result.display_text_override=style.text_override;
    result.tolerance_mode=style.tolerance_mode;result.symmetric_tolerance=style.symmetric_tolerance;
    result.single_tolerance=style.single_tolerance;result.upper_tolerance=style.upper_tolerance;result.lower_tolerance=style.lower_tolerance;
    if ((result.kind == zima::sketcher::DimensionKind::Distance ||
         result.kind == zima::sketcher::DimensionKind::DistancePointLine ||
         result.kind == zima::sketcher::DimensionKind::DistanceSymmetric ||
         result.kind == zima::sketcher::DimensionKind::DistanceLine ||
         result.kind == zima::sketcher::DimensionKind::DistanceLineSymmetric ||
         result.kind == zima::sketcher::DimensionKind::Radius ||
         result.kind == zima::sketcher::DimensionKind::Diameter ||
         result.kind == zima::sketcher::DimensionKind::EllipseMajorRadius ||
         result.kind == zima::sketcher::DimensionKind::EllipseMinorRadius) &&
        result.value < 0.0) {
        error_->setText(tr("Délka ani poloměr nesmí být záporný."));
        return false;
    }
    if ((result.kind == zima::sketcher::DimensionKind::Angle ||
         result.kind == zima::sketcher::DimensionKind::AngleBetween ||
         result.kind == zima::sketcher::DimensionKind::EllipseRotation) &&
        (result.value < -180.0 || result.value > 180.0)) {
        error_->setText(tr("Úhel musí být v rozsahu −180° až +180°."));
        return false;
    }
    if (result.kind == zima::sketcher::DimensionKind::AngleSymmetric &&
        (result.value < 0.0 || result.value > 360.0)) {
        error_->setText(tr("Symetrický úhel musí být v rozsahu 0° až 360°."));
        return false;
    }
    try {
        if(placement_fields_&&pending_layout_)pending_layout_(placement_fields_->value());
        commit_(std::move(result));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app

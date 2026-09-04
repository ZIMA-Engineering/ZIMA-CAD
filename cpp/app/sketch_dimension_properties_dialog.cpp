#include "sketch_dimension_properties_dialog.hpp"

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
    field->setDecimals(3);
    field->setSingleStep(1.0);
    field->setSuffix(" mm");
    field->setValue(value);
    return field;
}

QWidget* symbol_field(QLineEdit*& edit, const std::string& value,
                      const char* name, QWidget* parent) {
    auto* widget = new QWidget(parent);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit(QString::fromStdString(value), widget);
    edit->setObjectName(name);
    layout->addWidget(edit, 1);
    auto* symbols = new QToolButton(widget);
    symbols->setText(QStringLiteral("⌀"));
    symbols->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(symbols);
    for (const auto& symbol : {QStringLiteral("⌀"), QStringLiteral("○"),
            QStringLiteral("●"), QStringLiteral("R"), QStringLiteral("SR"),
            QStringLiteral("S⌀"), QStringLiteral("□"), QStringLiteral("⌴"),
            QStringLiteral("⌵"), QStringLiteral("↧"), QStringLiteral("⌒"),
            QStringLiteral("∠"), QStringLiteral("°"), QStringLiteral("±"),
            QStringLiteral("×"), QStringLiteral("≈")}) {
        auto* action = menu->addAction(symbol);
        QObject::connect(action, &QAction::triggered, edit,
            [edit, symbol] { edit->insert(symbol); edit->setFocus(); });
    }
    symbols->setMenu(menu);
    layout->addWidget(symbols);
    return widget;
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
    locked_ = new QCheckBox(tr("Zamknout hodnotu"), this);
    locked_->setObjectName("sketchDimensionLocked");
    locked_->setChecked(initial_.locked);
    locked_->setEnabled(initial_.driving);
    value_->setEnabled(initial_.driving);
    form_->addRow(tr("Ochrana hodnoty"), locked_);
    form_->addRow(tr("Text před hodnotou"), symbol_field(
        prefix_, initial_.prefix, "sketchDimensionPrefix", this));
    form_->addRow(tr("Text za hodnotou"), symbol_field(
        suffix_, initial_.suffix, "sketchDimensionSuffix", this));
    display_text_override_ = new QLineEdit(
        QString::fromStdString(initial_.display_text_override), this);
    display_text_override_->setObjectName("sketchDimensionDisplayText");
    display_text_override_->setPlaceholderText(
        tr("Prázdné = zobrazit skutečnou hodnotu"));
    form_->addRow(tr("Text místo hodnoty"), display_text_override_);
    tolerance_mode_ = new QComboBox(this);
    tolerance_mode_->setObjectName("sketchDimensionToleranceMode");
    tolerance_mode_->addItem(tr("Bez tolerance"), "");
    tolerance_mode_->addItem(tr("Symetrická"), "symmetric");
    tolerance_mode_->addItem(tr("Jednostranná odchylka"), "single_deviation");
    tolerance_mode_->addItem(tr("Horní a dolní odchylka"), "deviations");
    tolerance_mode_->setCurrentIndex(std::max(0,
        tolerance_mode_->findData(QString::fromStdString(initial_.tolerance_mode))));
    form_->addRow(tr("Tolerance"), tolerance_mode_);
    symmetric_tolerance_ = new QLineEdit(
        QString::fromStdString(initial_.symmetric_tolerance), this);
    single_tolerance_ = new QLineEdit(
        QString::fromStdString(initial_.single_tolerance), this);
    upper_tolerance_ = new QLineEdit(
        QString::fromStdString(initial_.upper_tolerance), this);
    lower_tolerance_ = new QLineEdit(
        QString::fromStdString(initial_.lower_tolerance), this);
    symmetric_tolerance_->setObjectName("sketchSymmetricTolerance");
    single_tolerance_->setObjectName("sketchSingleTolerance");
    upper_tolerance_->setObjectName("sketchUpperDeviation");
    lower_tolerance_->setObjectName("sketchLowerDeviation");
    form_->addRow(tr("Hodnota ±"), symmetric_tolerance_);
    form_->addRow(tr("Odchylka"), single_tolerance_);
    form_->addRow(tr("Horní odchylka"), upper_tolerance_);
    form_->addRow(tr("Dolní odchylka"), lower_tolerance_);
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
    content_layout()->addLayout(form_);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(tolerance_mode_, &QComboBox::currentIndexChanged, this,
        [this](int) { refresh_tolerance_fields(); error_->clear(); });
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
    refresh_tolerance_fields();
}

void SketchDimensionPropertiesDialog::refresh_tolerance_fields() {
    const auto mode = tolerance_mode_->currentData().toString();
    form_->setRowVisible(symmetric_tolerance_, mode == "symmetric");
    form_->setRowVisible(single_tolerance_, mode == "single_deviation");
    form_->setRowVisible(upper_tolerance_, mode == "deviations");
    form_->setRowVisible(lower_tolerance_, mode == "deviations");
}

bool SketchDimensionPropertiesDialog::submit() {
    auto result = initial_;
    result.value = value_->value();
    result.driving = driving_->isChecked();
    result.locked = locked_->isChecked();
    result.prefix = prefix_->text().toStdString();
    result.suffix = suffix_->text().toStdString();
    result.display_text_override =
        display_text_override_->text().trimmed().toStdString();
    result.tolerance_mode = tolerance_mode_->currentData().toString().toStdString();
    result.symmetric_tolerance = symmetric_tolerance_->text().trimmed().toStdString();
    result.single_tolerance = single_tolerance_->text().trimmed().toStdString();
    result.upper_tolerance = upper_tolerance_->text().trimmed().toStdString();
    result.lower_tolerance = lower_tolerance_->text().trimmed().toStdString();
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
        commit_(std::move(result));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app

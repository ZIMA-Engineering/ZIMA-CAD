#include "primitive_properties_dialog.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include <exception>

namespace zima::app {
namespace {

QString primitive_label(zima::document::FeatureKind kind) {
    return kind == zima::document::FeatureKind::Cylinder ? QObject::tr("válce")
        : kind == zima::document::FeatureKind::Sphere ? QObject::tr("koule")
        : kind == zima::document::FeatureKind::Extrusion
            ? QObject::tr("vytažení")
        : kind == zima::document::FeatureKind::Revolution
            ? QObject::tr("rotace")
        : kind == zima::document::FeatureKind::Fillet
            ? QObject::tr("zaoblení")
        : kind == zima::document::FeatureKind::Chamfer
            ? QObject::tr("sražení")
        : kind == zima::document::FeatureKind::ImportedStep
            ? QObject::tr("importovaného STEP") : QObject::tr("kvádru");
}

}  // namespace

PrimitivePropertiesDialog::PrimitivePropertiesDialog(
    const zima::document::HistoryContainer& initial,
    bool edit_mode,
    bool allow_subtract,
    CommitCallback commit,
    QWidget* parent)
    : PropertiesSubWindow(
          edit_mode
              ? tr("Vlastnosti %1").arg(primitive_label(initial.feature_kind))
              : initial.feature_kind == zima::document::FeatureKind::Cylinder
                  ? tr("Nový válec")
                  : initial.feature_kind == zima::document::FeatureKind::Sphere
                      ? tr("Nová koule")
                  : initial.feature_kind == zima::document::FeatureKind::Extrusion
                      ? tr("Nové vytažení")
                  : initial.feature_kind == zima::document::FeatureKind::Revolution
                      ? tr("Nová rotace")
                  : initial.feature_kind == zima::document::FeatureKind::Fillet
                      ? tr("Nové zaoblení")
                  : initial.feature_kind == zima::document::FeatureKind::Chamfer
                      ? tr("Nové sražení")
                  : initial.feature_kind == zima::document::FeatureKind::ImportedStep
                      ? tr("Import STEP") : tr("Nový kvádr"),
          parent),
      initial_(initial), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    form->addRow(tr("Název"), name_);
    const bool treatment = initial.feature_kind == zima::document::FeatureKind::Fillet ||
        initial.feature_kind == zima::document::FeatureKind::Chamfer;
    if (!treatment) {
        operation_ = new QComboBox(this);
        operation_->addItem(tr("Přičíst"), "add");
        if (allow_subtract) operation_->addItem(tr("Odečíst"), "subtract");
        if (initial.combine_mode == zima::document::CombineMode::Subtract) {
            operation_->setCurrentIndex(operation_->findData("subtract"));
        }
        form->addRow(tr("Operace"), operation_);
    }

    const auto dimension = [this](double value, const char* object_name) {
        auto* field = new QDoubleSpinBox(this);
        field->setRange(0.001, 1'000'000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        field->setSuffix(" mm");
        field->setObjectName(object_name);
        field->setValue(value);
        return field;
    };
    if (initial.feature_kind == zima::document::FeatureKind::Box) {
        length_ = dimension(initial.box.length, "boxLength");
        width_ = dimension(initial.box.width, "boxWidth");
        height_ = dimension(initial.box.height, "boxHeight");
        form->addRow(tr("Délka"), length_);
        form->addRow(tr("Šířka"), width_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Cylinder) {
        radius_ = dimension(initial.cylinder.radius, "cylinderRadius");
        height_ = dimension(initial.cylinder.height, "cylinderHeight");
        form->addRow(tr("Poloměr"), radius_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Sphere) {
        radius_ = dimension(initial.sphere.radius, "sphereRadius");
        form->addRow(tr("Poloměr"), radius_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Extrusion) {
        auto* sketch = new QLabel(QString::fromStdString(initial.extrusion.sketch_id), this);
        sketch->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Zdrojová skica"), sketch);
        height_ = dimension(initial.extrusion.height, "extrusionHeight");
        form->addRow(tr("Celková délka"), height_);
        extrusion_direction_ = new QComboBox(this);
        extrusion_direction_->setObjectName("extrusionDirection");
        extrusion_direction_->addItem(tr("Dopředu"), "forward");
        extrusion_direction_->addItem(tr("Obráceně"), "reverse");
        extrusion_direction_->addItem(tr("Symetricky"), "symmetric");
        const char* direction =
            initial.extrusion.direction ==
                    zima::document::ExtrusionDirection::Forward
                ? "forward"
            : initial.extrusion.direction ==
                    zima::document::ExtrusionDirection::Reverse
                ? "reverse" : "symmetric";
        extrusion_direction_->setCurrentIndex(
            extrusion_direction_->findData(direction));
        form->addRow(tr("Směr"), extrusion_direction_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Revolution) {
        auto* sketch = new QLabel(
            QString::fromStdString(initial.revolution.sketch_id), this);
        sketch->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Zdrojová skica"), sketch);
        revolution_axis_ = new QComboBox(this);
        revolution_axis_->setObjectName("revolutionAxis");
        revolution_axis_->addItem(tr("Osa X skici"), "sketch_x");
        revolution_axis_->addItem(tr("Osa Y skici"), "sketch_y");
        revolution_axis_->setCurrentIndex(revolution_axis_->findData(
            initial.revolution.axis == zima::document::RevolutionAxis::SketchX
                ? "sketch_x" : "sketch_y"));
        form->addRow(tr("Osa"), revolution_axis_);
        angle_ = new QDoubleSpinBox(this);
        angle_->setRange(0.001, 360.0);
        angle_->setDecimals(3);
        angle_->setSingleStep(1.0);
        angle_->setSuffix("°");
        angle_->setObjectName("revolutionAngle");
        angle_->setValue(initial.revolution.angle_degrees);
        form->addRow(tr("Úhel"), angle_);
    } else if (initial.feature_kind == zima::document::FeatureKind::ImportedStep) {
        auto* source = new QLabel(
            QString::fromStdString(initial.imported_step.source_path), this);
        source->setTextInteractionFlags(Qt::TextSelectableByMouse);
        source->setWordWrap(true);
        form->addRow(tr("Zdrojový soubor"), source);
    } else {
        auto* edge = new QLabel(
            QString::fromStdString(initial.edge_treatment.edge.owner_id + " / " +
                                    initial.edge_treatment.edge.semantic_key), this);
        edge->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Hrana"), edge);
        treatment_size_ = dimension(initial.edge_treatment.size, "edgeTreatmentSize");
        form->addRow(initial.feature_kind == zima::document::FeatureKind::Fillet
            ? tr("Poloměr") : tr("Vzdálenost"), treatment_size_);
    }

    const auto placement = [this](double value, bool angular) {
        auto* field = new QDoubleSpinBox(this);
        field->setRange(angular ? -360.0 : -1'000'000.0,
                        angular ? 360.0 : 1'000'000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        field->setSuffix(angular ? "°" : " mm");
        field->setValue(value);
        field->setObjectName(angular ? "primitiveRotation" : "primitiveTranslation");
        return field;
    };
    if (initial.feature_kind == zima::document::FeatureKind::Box ||
        initial.feature_kind == zima::document::FeatureKind::Cylinder ||
        initial.feature_kind == zima::document::FeatureKind::Sphere ||
        initial.feature_kind == zima::document::FeatureKind::ImportedStep) {
        translation_ = {
            placement(initial.placement.x, false),
            placement(initial.placement.y, false),
            placement(initial.placement.z, false),
        };
        rotation_ = {
            placement(initial.placement.rotation_x, true),
            placement(initial.placement.rotation_y, true),
            placement(initial.placement.rotation_z, true),
        };
        form->addRow(tr("Posunutí X"), translation_[0]);
        form->addRow(tr("Posunutí Y"), translation_[1]);
        form->addRow(tr("Posunutí Z"), translation_[2]);
        form->addRow(tr("Natočení X"), rotation_[0]);
        form->addRow(tr("Natočení Y"), rotation_[1]);
        form->addRow(tr("Natočení Z"), rotation_[2]);
    }
    content_layout()->addLayout(form);

    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& name) {
        set_internal_title(name.trimmed().isEmpty()
            ? tr("Vlastnosti %1").arg(primitive_label(initial_.feature_kind))
            : tr("Vlastnosti: %1").arg(name.trimmed()));
        error_->clear();
    });
}

bool PrimitivePropertiesDialog::submit() {
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        name_->setFocus();
        return false;
    }
    auto result = initial_;
    result.name = name.toStdString();
    if (operation_ != nullptr) {
        result.combine_mode = operation_->currentData().toString() == "subtract"
            ? zima::document::CombineMode::Subtract
            : zima::document::CombineMode::Add;
    }
    if (result.feature_kind == zima::document::FeatureKind::Box) {
        result.box = {length_->value(), width_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Cylinder) {
        result.cylinder = {radius_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Sphere) {
        result.sphere = {radius_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Extrusion) {
        result.extrusion.height = height_->value();
        const QString direction =
            extrusion_direction_->currentData().toString();
        result.extrusion.direction = direction == "reverse"
            ? zima::document::ExtrusionDirection::Reverse
            : direction == "symmetric"
                ? zima::document::ExtrusionDirection::Symmetric
                : zima::document::ExtrusionDirection::Forward;
    } else if (result.feature_kind == zima::document::FeatureKind::Revolution) {
        result.revolution.axis =
            revolution_axis_->currentData().toString() == "sketch_y"
                ? zima::document::RevolutionAxis::SketchY
                : zima::document::RevolutionAxis::SketchX;
        result.revolution.angle_degrees = angle_->value();
    } else if (result.feature_kind != zima::document::FeatureKind::ImportedStep) {
        result.edge_treatment.size = treatment_size_->value();
    }
    if (result.feature_kind == zima::document::FeatureKind::Box ||
        result.feature_kind == zima::document::FeatureKind::Cylinder ||
        result.feature_kind == zima::document::FeatureKind::Sphere ||
        result.feature_kind == zima::document::FeatureKind::ImportedStep) {
        result.placement = {
            translation_[0]->value(), translation_[1]->value(), translation_[2]->value(),
            rotation_[0]->value(), rotation_[1]->value(), rotation_[2]->value(),
        };
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

#include "primitive_properties_dialog.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>

#include <exception>
#include <algorithm>

namespace zima::app {
namespace {

QString primitive_label(zima::document::FeatureKind kind) {
    return kind == zima::document::FeatureKind::Cylinder ? QObject::tr("válce")
        : kind == zima::document::FeatureKind::Sphere ? QObject::tr("koule")
        : kind == zima::document::FeatureKind::Cone ? QObject::tr("kužele")
        : kind == zima::document::FeatureKind::Pyramid ? QObject::tr("jehlanu")
        : kind == zima::document::FeatureKind::Wedge ? QObject::tr("klínu")
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
                  : initial.feature_kind == zima::document::FeatureKind::Cone
                      ? tr("Nový kužel")
                  : initial.feature_kind == zima::document::FeatureKind::Pyramid
                      ? tr("Nový jehlan")
                  : initial.feature_kind == zima::document::FeatureKind::Wedge
                      ? tr("Nový klín")
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
    } else if (initial.feature_kind == zima::document::FeatureKind::Cone) {
        radius_ = dimension(initial.cone.bottom_radius, "coneBottomRadius");
        top_radius_ = dimension(std::max(initial.cone.top_radius, 0.001), "coneTopRadius");
        top_radius_->setRange(0.0, 1'000'000.0);
        top_radius_->setValue(initial.cone.top_radius);
        height_ = dimension(initial.cone.height, "coneHeight");
        form->addRow(tr("Dolní poloměr"), radius_);
        form->addRow(tr("Horní poloměr"), top_radius_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Pyramid) {
        length_ = dimension(initial.pyramid.length, "pyramidLength");
        width_ = dimension(initial.pyramid.width, "pyramidWidth");
        height_ = dimension(initial.pyramid.height, "pyramidHeight");
        form->addRow(tr("Délka základny"), length_);
        form->addRow(tr("Šířka základny"), width_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Wedge) {
        length_ = dimension(initial.wedge.length, "wedgeLength");
        width_ = dimension(initial.wedge.width, "wedgeWidth");
        height_ = dimension(initial.wedge.height, "wedgeHeight");
        top_offset_ = dimension(std::max(initial.wedge.top_offset, 0.001), "wedgeTopOffset");
        top_offset_->setRange(0.0, initial.wedge.length);
        top_offset_->setValue(initial.wedge.top_offset);
        connect(length_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            top_offset_, &QDoubleSpinBox::setMaximum);
        form->addRow(tr("Délka"), length_);
        form->addRow(tr("Šířka"), width_);
        form->addRow(tr("Výška"), height_);
        form->addRow(tr("Odsazení horní hrany"), top_offset_);
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
        extrusion_extent_ = new QComboBox(this);
        extrusion_extent_->setObjectName("extrusionExtent");
        extrusion_extent_->addItem(tr("Číselná délka"), "blind");
        extrusion_extent_->addItem(tr("Až k ploše/rovině"), "up_to");
        extrusion_extent_->addItem(tr("Skrz vše (odečíst)"), "through_all");
        const char* extent = (initial.extrusion.extent ==
                zima::document::ExtrusionExtent::UpToPlane ||
                initial.extrusion.extent ==
                    zima::document::ExtrusionExtent::UpToSurface) ? "up_to"
            : initial.extrusion.extent == zima::document::ExtrusionExtent::ThroughAll
                ? "through_all" : "blind";
        extrusion_extent_->setCurrentIndex(extrusion_extent_->findData(extent));
        form->addRow(tr("Rozsah"), extrusion_extent_);
        extrusion_target_ = new QLabel(this);
        extrusion_target_->setObjectName("extrusionTarget");
        extrusion_target_->setText(initial.extrusion.target_face.valid()
            ? QString::fromStdString(initial.extrusion.target_face.owner_id + " / " +
                                      initial.extrusion.target_face.semantic_key)
            : tr("Nevybráno"));
        auto* select_target = new QPushButton(tr("Vybrat plochu ve view"), this);
        select_target->setObjectName("selectExtrusionTarget");
        auto* target_row = new QWidget(this);
        auto* target_layout = new QHBoxLayout(target_row);
        target_layout->setContentsMargins(0, 0, 0, 0);
        target_layout->addWidget(extrusion_target_, 1);
        target_layout->addWidget(select_target);
        form->addRow(tr("Cíl"), target_row);
        connect(select_target, &QPushButton::clicked, this, [this] {
            if (extrusion_target_request_) extrusion_target_request_();
        });
        connect(extrusion_extent_, &QComboBox::currentIndexChanged,
            this, [this, select_target](int) {
                const bool target = extrusion_extent_->currentData() == "up_to";
                if (target && extrusion_direction_->currentData() == "symmetric") {
                    extrusion_direction_->setCurrentIndex(
                        extrusion_direction_->findData("forward"));
                }
                extrusion_target_->setEnabled(target);
                select_target->setEnabled(target);
                height_->setEnabled(extrusion_extent_->currentData() == "blind");
                notify_preview();
            });
        connect(height_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { notify_preview(); });
        connect(extrusion_direction_, &QComboBox::currentIndexChanged,
            this, [this] { notify_preview(); });
        if (operation_ != nullptr) {
            connect(operation_, &QComboBox::currentIndexChanged,
                this, [this] { notify_preview(); });
        }
        const bool target_enabled = extrusion_extent_->currentData() == "up_to";
        extrusion_target_->setEnabled(target_enabled);
        select_target->setEnabled(target_enabled);
        height_->setEnabled(extrusion_extent_->currentData() == "blind");
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
        QStringList references;
        for (const auto& edge : initial.edge_treatment.edges) {
            references.push_back(QString::fromStdString(
                edge.owner_id + " / " + edge.semantic_key));
        }
        auto* edge_list = new QLabel(references.join("\n"), this);
        edge_list->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Hrany"), edge_list);
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
        initial.feature_kind == zima::document::FeatureKind::Cone ||
        initial.feature_kind == zima::document::FeatureKind::Pyramid ||
        initial.feature_kind == zima::document::FeatureKind::Wedge ||
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

zima::document::HistoryContainer PrimitivePropertiesDialog::values() const {
    const QString name = name_->text().trimmed();
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
    } else if (result.feature_kind == zima::document::FeatureKind::Cone) {
        result.cone = {radius_->value(), top_radius_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Pyramid) {
        result.pyramid = {length_->value(), width_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Wedge) {
        result.wedge = {length_->value(), width_->value(), height_->value(),
                        top_offset_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Extrusion) {
        result.extrusion.height = height_->value();
        const QString direction =
            extrusion_direction_->currentData().toString();
        result.extrusion.direction = direction == "reverse"
            ? zima::document::ExtrusionDirection::Reverse
            : direction == "symmetric"
                ? zima::document::ExtrusionDirection::Symmetric
                : zima::document::ExtrusionDirection::Forward;
        const QString extent = extrusion_extent_->currentData().toString();
        result.extrusion.extent = extent == "up_to"
            ? (result.extrusion.target_surface_triangles.empty()
                ? zima::document::ExtrusionExtent::UpToPlane
                : zima::document::ExtrusionExtent::UpToSurface)
            : extent == "through_all"
                ? zima::document::ExtrusionExtent::ThroughAll
                : zima::document::ExtrusionExtent::Blind;
    } else if (result.feature_kind == zima::document::FeatureKind::Revolution) {
        result.revolution.axis =
            revolution_axis_->currentData().toString() == "sketch_y"
                ? zima::document::RevolutionAxis::SketchY
                : zima::document::RevolutionAxis::SketchX;
        result.revolution.angle_degrees = angle_->value();
    } else if (result.feature_kind != zima::document::FeatureKind::ImportedStep) {
        result.edge_treatment.size = treatment_size_->value();
    }
    return result;
}

void PrimitivePropertiesDialog::set_extrusion_target(
    zima::kernel::FaceReference reference, zima::kernel::Vec3 origin,
    zima::kernel::Vec3 normal) {
    initial_.extrusion.target_face = std::move(reference);
    initial_.extrusion.target_plane_origin = origin;
    initial_.extrusion.target_plane_normal = normal;
    initial_.extrusion.target_surface_triangles.clear();
    extrusion_target_->setText(QString::fromStdString(
        initial_.extrusion.target_face.owner_id + " / " +
        initial_.extrusion.target_face.semantic_key));
    notify_preview();
}

void PrimitivePropertiesDialog::set_extrusion_surface_target(
    zima::kernel::FaceReference reference,
    std::vector<zima::kernel::Vec3> triangles) {
    initial_.extrusion.target_face = std::move(reference);
    initial_.extrusion.target_surface_triangles = std::move(triangles);
    extrusion_target_->setText(QString::fromStdString(
        initial_.extrusion.target_face.owner_id + " / " +
        initial_.extrusion.target_face.semantic_key));
    notify_preview();
}

void PrimitivePropertiesDialog::set_extrusion_target_request(
    std::function<void()> callback) {
    extrusion_target_request_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_preview_callback(
    std::function<void(const zima::document::HistoryContainer&)> callback) {
    preview_ = std::move(callback);
    notify_preview();
}

void PrimitivePropertiesDialog::notify_preview() {
    if (preview_) preview_(values());
}

bool PrimitivePropertiesDialog::submit() {
    if (name_->text().trimmed().isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        name_->setFocus();
        return false;
    }
    auto result = values();
    if (result.feature_kind == zima::document::FeatureKind::Extrusion &&
        (result.extrusion.extent == zima::document::ExtrusionExtent::UpToPlane ||
         result.extrusion.extent == zima::document::ExtrusionExtent::UpToSurface) &&
        !result.extrusion.target_face.valid()) {
        error_->setText(tr("Vyberte cílovou rovinnou plochu."));
        return false;
    }
    if (result.feature_kind == zima::document::FeatureKind::Box ||
        result.feature_kind == zima::document::FeatureKind::Cylinder ||
        result.feature_kind == zima::document::FeatureKind::Sphere ||
        result.feature_kind == zima::document::FeatureKind::Cone ||
        result.feature_kind == zima::document::FeatureKind::Pyramid ||
        result.feature_kind == zima::document::FeatureKind::Wedge ||
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

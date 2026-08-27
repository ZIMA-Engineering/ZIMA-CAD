#include "sketch_properties_dialog.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace zima::app {

namespace {
// Sentinel userData for the "no Plane container reference" entry of
// plane_reference_ -- distinct from any real owner_id string, matched
// against a userData() of an empty QString.
constexpr const char* kNoPlaneReference = "";
}  // namespace

SketchPropertiesDialog::SketchPropertiesDialog(
    zima::sketcher::Sketch initial,
    zima::document::Placement initial_placement, bool edit_mode,
    std::vector<PlaneOption> plane_options,
    CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(tr("Skica"), parent),
      initial_(std::move(initial)), initial_placement_(std::move(initial_placement)),
      plane_options_(std::move(plane_options)), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(320);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial_.name), this);
    name_->setObjectName("sketchName");
    plane_ = new QComboBox(this);
    plane_->setObjectName("sketchPlane");
    plane_->addItem("XY", static_cast<int>(zima::sketcher::SketchPlane::XY));
    plane_->addItem("XZ", static_cast<int>(zima::sketcher::SketchPlane::XZ));
    plane_->addItem("YZ", static_cast<int>(zima::sketcher::SketchPlane::YZ));
    plane_->setCurrentIndex(plane_->findData(static_cast<int>(initial_.plane)));
    offset_ = new QDoubleSpinBox(this);
    offset_->setObjectName("sketchPlaneOffset");
    offset_->setRange(-1'000'000.0, 1'000'000.0);
    offset_->setDecimals(3);
    offset_->setSuffix(" mm");
    offset_->setValue(initial_.plane_offset);
    plane_reference_ = new QComboBox(this);
    plane_reference_->setObjectName("sketchPlaneReference");
    plane_reference_->addItem(tr("(žádná — použít rovinu výše)"),
        QString::fromLatin1(kNoPlaneReference));
    for (const auto& option : plane_options_) {
        plane_reference_->addItem(option.label,
            QString::fromStdString(option.owner_id));
    }
    const auto initial_reference_index = plane_reference_->findData(
        QString::fromStdString(initial_.plane_reference_owner_id));
    plane_reference_->setCurrentIndex(
        initial_reference_index >= 0 ? initial_reference_index : 0);
    form->addRow(tr("Název"), name_);
    content_layout()->addLayout(form);
    placement_ = std::make_unique<zima::ui::ContainerPlacementSection>(
        this, content_layout(), /*with_orientation=*/true,
        /*position_rows_can_define_rotation=*/true);
    placement_->initialize_from_references(initial_placement_.references,
        [](const std::string& semantic) {
            return QString::fromStdString(semantic);
        });
    placement_->initialize_numeric_values(initial_placement_);
    placement_->reference_table()->setObjectName("sketchReferenceTable");
    if (placement_->orientation_table() != nullptr) {
        placement_->orientation_table()->setObjectName("sketchOrientationTable");
    }
    const bool constrained = std::any_of(initial_placement_.references.begin(),
        initial_placement_.references.end(), [](const auto& reference) {
            return reference.orientation_drives_rotation;
        });
    placement_->set_orientation_base_rotation(
        {initial_placement_.rotation_x, initial_placement_.rotation_y,
         initial_placement_.rotation_z}, constrained);
    placement_->set_changed_callback([this] {
        refresh_resolved_placement();
        notify_preview();
    });

    // A Sketch owns the same explicit container Plane as Plane properties.
    // External geometry is therefore entered through the shared placement
    // reference table above; do not expose the old parallel Plane-container
    // combo, which made the two dialogs look and behave differently.
    plane_reference_->hide();
    auto* orientation_form = new QFormLayout;
    orientation_form->addRow(tr("Výchozí rovina"), plane_);
    orientation_form->addRow(tr("Odsazení roviny"), offset_);
    content_layout()->addLayout(orientation_form);
    sketch_button_ = new QPushButton(QStringLiteral("SKETCH"), this);
    sketch_button_->setObjectName("sketchOpenButton");
    sketch_button_->setMinimumHeight(40);
    sketch_button_->setStyleSheet(
        "QPushButton{background:#4DD811;color:#102027;font-weight:700;"
        "padding:9px 18px;border-radius:4px}"
        "QPushButton:hover{background:#65ec2c}");
    content_layout()->addWidget(sketch_button_);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString&) {
        error_->clear();
    });
    connect(plane_reference_, &QComboBox::currentIndexChanged, this,
        [this](int) { update_plane_fields_enabled(); });
    connect(plane_, &QComboBox::currentIndexChanged, this,
        [this](int) { notify_preview(); });
    connect(offset_, &QDoubleSpinBox::valueChanged, this,
        [this](double) { notify_preview(); });
    connect(sketch_button_, &QPushButton::clicked, this, [this] {
        if (submit()) accept();
    });
    update_plane_fields_enabled();
}

void SketchPropertiesDialog::update_plane_fields_enabled() {
    plane_->setEnabled(true);
    offset_->setEnabled(true);
}

void SketchPropertiesDialog::set_reference_request_callback(
        ReferenceRequestCallback callback) {
    placement_->set_reference_request_callback(std::move(callback));
}

void SketchPropertiesDialog::set_reference_highlights_changed_callback(
        HighlightsChangedCallback callback) {
    placement_->set_highlights_changed_callback(std::move(callback));
}

void SketchPropertiesDialog::set_reference_geometry(
        zima::kernel::ViewerReferenceGeometry geometry) {
    reference_geometry_ = std::move(geometry);
    refresh_resolved_placement();
}

void SketchPropertiesDialog::set_preview_callback(PreviewCallback callback) {
    preview_ = std::move(callback);
    notify_preview();
}

std::pair<zima::sketcher::Sketch, zima::document::Placement>
SketchPropertiesDialog::current_values() const {
    auto sketch = initial_;
    sketch.name = name_->text().trimmed().toStdString();
    sketch.plane = static_cast<zima::sketcher::SketchPlane>(
        plane_->currentData().toInt());
    sketch.plane_offset = offset_->value();
    sketch.plane_reference_owner_id.clear();
    auto placement = placement_->numeric_placement();
    placement.references = placement_->populated_references();
    return {std::move(sketch), std::move(placement)};
}

void SketchPropertiesDialog::notify_preview() {
    if (!preview_) return;
    auto [sketch, placement] = current_values();
    preview_(sketch, placement);
}

void SketchPropertiesDialog::refresh_resolved_placement() {
    auto value = placement_->numeric_placement();
    value.references = placement_->populated_references();
    zima::kernel::Vec3 base_rotation;
    bool orientation_from_reference = false;
    static_cast<void>(zima::document::resolve_placement(
        value, reference_geometry_, &base_rotation,
        &orientation_from_reference));
    const auto state = zima::document::point_constraint_state(
        value.references, reference_geometry_);
    placement_->set_translation_constraint_state(
        state, {value.x, value.y, value.z});
    placement_->set_remaining_rotation_dof(
        zima::document::orientation_constraint_remaining_dof(
            value.references, reference_geometry_, false));
    placement_->set_orientation_base_rotation(
        base_rotation, orientation_from_reference);
}

std::vector<zima::document::ConstructionReference>
SketchPropertiesDialog::highlighted_reference_entries() const {
    return placement_->highlighted_reference_entries();
}

std::vector<zima::document::ConstructionReference>
SketchPropertiesDialog::references_without(std::size_t index) const {
    return placement_->references_without(index);
}

bool SketchPropertiesDialog::owns_reference_owner(
        const std::string& owner_id) const {
    return owner_id == initial_.owner_container_id;
}

bool SketchPropertiesDialog::set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label) {
    QString error;
    const bool accepted = placement_->set_reference(
        index, std::move(reference), label, &error);
    if (accepted) refresh_resolved_placement();
    return accepted;
}

std::size_t SketchPropertiesDialog::first_empty_position_index() const {
    return placement_->first_empty_position_index();
}

void SketchPropertiesDialog::set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution) {
    placement_->set_translation_constraint_state(state, solution);
}

void SketchPropertiesDialog::set_remaining_rotation_dof(int dof) {
    placement_->set_remaining_rotation_dof(dof);
}

void SketchPropertiesDialog::set_orientation_base_rotation(
        const zima::kernel::Vec3& rotation, bool constrained) {
    placement_->set_orientation_base_rotation(rotation, constrained);
}

bool SketchPropertiesDialog::submit() {
    const auto name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    auto [result, resolved_placement] = current_values();
    try {
        result.validate();
        commit_(std::move(result), std::move(resolved_placement));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app

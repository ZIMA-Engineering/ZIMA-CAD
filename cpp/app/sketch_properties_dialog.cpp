#include "sketch_properties_dialog.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace zima::app {

namespace {
// Sentinel userData for the "no Plane container reference" entry of
// plane_reference_ -- distinct from any real owner_id string, matched
// against a userData() of an empty QString.
constexpr const char* kNoPlaneReference = "";
}  // namespace

SketchPropertiesDialog::SketchPropertiesDialog(
    zima::sketcher::Sketch initial, bool edit_mode,
    std::vector<PlaneOption> plane_options,
    CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(edit_mode ? tr("Vlastnosti skici") : tr("Nová skica"), parent),
      initial_(std::move(initial)), plane_options_(std::move(plane_options)),
      commit_(std::move(commit)) {
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
    form->addRow(tr("Rovina"), plane_);
    form->addRow(tr("Odsazení roviny"), offset_);
    form->addRow(tr("Reference roviny"), plane_reference_);
    content_layout()->addLayout(form);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& value) {
        set_internal_title(value.trimmed().isEmpty()
            ? tr("Vlastnosti skici") : tr("Vlastnosti: %1").arg(value.trimmed()));
        error_->clear();
    });
    connect(plane_reference_, &QComboBox::currentIndexChanged, this,
        [this](int) { update_plane_fields_enabled(); });
    update_plane_fields_enabled();
}

void SketchPropertiesDialog::update_plane_fields_enabled() {
    // While the Sketch follows a referenced Plane container, `plane`/
    // `plane_offset` are ignored (see Sketch::world_point()'s
    // plane_reference_owner_id branch), so gray them out instead of
    // leaving misleading editable fields that have no effect.
    const bool has_reference = !plane_reference_->currentData().toString().isEmpty();
    plane_->setEnabled(!has_reference);
    offset_->setEnabled(!has_reference);
}

bool SketchPropertiesDialog::submit() {
    const auto name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    auto result = initial_;
    result.name = name.toStdString();
    result.plane = static_cast<zima::sketcher::SketchPlane>(plane_->currentData().toInt());
    result.plane_offset = offset_->value();
    result.plane_reference_owner_id =
        plane_reference_->currentData().toString().toStdString();
    try {
        result.validate();
        commit_(std::move(result));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app

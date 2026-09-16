#include "work_plane_selection.hpp"
#include <zima/document/sketch_placement.hpp>
#include <zima/ui/numeric_value_lock.hpp>
#include "sketch_button_style.hpp"
#include "sketch_properties_dialog.hpp"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
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
    : PropertiesSubWindow(tr("Vlastnosti skici"), parent),
      initial_(std::move(initial)), initial_placement_(std::move(initial_placement)),
      plane_options_(std::move(plane_options)), commit_(std::move(commit)) {
    zima::document::normalize_sketch_front_references(initial_placement_.references);
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
    install_automatic_work_plane(plane_, initial_.plane_auto);
    offset_ = new QDoubleSpinBox(this);
    offset_->setObjectName("sketchPlaneOffset");
    offset_->setRange(-1'000'000.0, 1'000'000.0);
    offset_->setDecimals(zima::ui::numeric_decimal_places(this,3));
    offset_->setSuffix(" mm");
    offset_->setValue(initial_.plane_offset);
    setProperty("zimaValueLockOwner",QString::fromStdString(initial_.owner_container_id.empty()?initial_.id:initial_.owner_container_id));
    zima::ui::bind_numeric_value_lock(offset_,"profile_offset",initial_placement_.value_locks,[this]{notify_preview();});
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
        /*position_rows_can_define_rotation=*/true, zima::ui::numeric_decimal_places(this));
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
    orientation_form->setObjectName("sketchPlaneForm");
    orientation_form->addRow(tr("Výchozí rovina"), plane_);
    orientation_form->addRow(tr("Odsazení roviny"), offset_);
    content_layout()->addLayout(orientation_form);
    placement_->install_dof_label(content_layout());
    sketch_button_ = new QPushButton(QStringLiteral("SKETCH"), this);
    sketch_button_->setObjectName("sketchOpenButton");
    sketch_button_->setMinimumHeight(40);
    style_sketch_button(sketch_button_);
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
        if (edit_pending_sketch_) { edit_pending_sketch_(); return; }
        enter_sketch_after_commit_ = true;
        if (submit()) accept();
        else enter_sketch_after_commit_ = false;
    });
    update_plane_fields_enabled();
}

void SketchPropertiesDialog::set_holes_mode(double diameter,
    std::set<std::string>& locks, std::function<void(double)> changed, std::function<void()> edit_sketch,
    std::optional<zima::kernel::DimensionLayout> layout,
    std::function<void(zima::kernel::DimensionLayout)> layout_changed) {
    holes_dimension_layout_ = std::move(layout);
    holes_layout_changed_ = std::move(layout_changed);
    set_internal_title(tr("Vlastnosti otvorů"));
    setObjectName("holesPropertiesDialog");
    auto* form = new QFormLayout;
    auto* field = new QDoubleSpinBox(this);
    holes_diameter_ = field;
    field->setObjectName("holesDiameter");
    field->setDecimals(zima::ui::numeric_decimal_places(this, 3));
    field->setRange(0.001, 1000000.0);
    field->setSuffix(" mm"); field->setValue(diameter);
    zima::ui::bind_numeric_value_lock(field, "diameter", locks, [this] { notify_preview(); });
    form->addRow(tr("Průměr otvorů"), field);
    content_layout()->insertLayout(content_layout()->indexOf(sketch_button_), form);
    connect(field, &QDoubleSpinBox::valueChanged, this,
        [this, changed=std::move(changed)](double value) { changed(value); notify_preview(); });
    edit_pending_sketch_ = std::move(edit_sketch);
}

void SketchPropertiesDialog::set_flat_mode(zima::document::FlatParameters initial,
    const zima::document::SheetMetalDefaults& defaults,std::set<std::string>& locks,
    std::function<void(zima::document::FlatParameters)> changed,std::function<void()> edit_sketch) {
    set_internal_title(tr("Vlastnosti tabule"));setObjectName("flatPropertiesDialog");
    offset_->setValue(0);
    findChild<QFormLayout*>("sketchPlaneForm")->setRowVisible(offset_,false);
    auto pending=std::make_shared<zima::document::FlatParameters>(initial);
    auto* form=new QFormLayout;
    auto* direction=new QComboBox(this);direction->setObjectName("flatDirection");
    direction->addItem(tr("První strana"),static_cast<int>(zima::document::ExtrusionDirection::Forward));
    direction->addItem(tr("Druhá strana"),static_cast<int>(zima::document::ExtrusionDirection::Reverse));
    direction->addItem(tr("Symetricky"),static_cast<int>(zima::document::ExtrusionDirection::Symmetric));
    direction->setCurrentIndex(direction->findData(static_cast<int>(initial.direction)));
    auto* custom=new QCheckBox(tr("Vlastní tloušťka"),this);custom->setObjectName("flatThicknessOverride");custom->setChecked(initial.thickness_override);
    auto* thickness=new QDoubleSpinBox(this);thickness->setObjectName("flatThickness");
    thickness->setRange(.001,1000000);thickness->setDecimals(zima::ui::numeric_decimal_places(this,3));thickness->setSuffix(" mm");
    thickness->setToolTip(tr("Celková tloušťka plechu. Symetricky znamená polovinu na každé straně skici."));
    const double inherited=defaults.thickness_mm.value_or(1);
    const auto refresh=[=] {
        QSignalBlocker blocker(thickness);
        thickness->setValue(pending->thickness_override?pending->thickness:inherited);
        thickness->setEnabled(pending->thickness_override);
    };
    refresh();
    zima::ui::bind_numeric_value_lock(thickness,"thickness",locks,[this]{notify_preview();});
    connect(direction,&QComboBox::currentIndexChanged,this,[=,this](int){
        pending->direction=static_cast<zima::document::ExtrusionDirection>(direction->currentData().toInt());changed(*pending);notify_preview();
    });
    connect(custom,&QCheckBox::toggled,this,[=,this](bool enabled){
        if(enabled)pending->thickness=thickness->value();
        pending->thickness_override=enabled;refresh();changed(*pending);notify_preview();
    });
    connect(thickness,&QDoubleSpinBox::valueChanged,this,[=,this](double value){pending->thickness=value;changed(*pending);notify_preview();});
    form->addRow(tr("Směr"),direction);form->addRow(custom);form->addRow(tr("Tloušťka"),thickness);
    content_layout()->insertLayout(content_layout()->indexOf(sketch_button_),form);
    edit_pending_sketch_=std::move(edit_sketch);
}

void SketchPropertiesDialog::set_bend_mode(zima::document::BendParameters initial,
    const zima::document::SheetMetalDefaults& defaults,std::set<std::string>& locks,
    std::function<void(zima::document::BendParameters)> changed,std::function<void()> edit_sketch,
    std::function<void(std::size_t)> edit_bend_sketch) {
    set_internal_title(tr("Vlastnosti ohybu"));setObjectName("bendPropertiesDialog");
    auto pending=std::make_shared<zima::document::BendParameters>(initial);
    auto* form=new QFormLayout;
    auto* mode=new QComboBox(this);mode->setObjectName("bendState");
    mode->addItem(tr("Ohnutý (Bend)"),false);mode->addItem(tr("Rozvinutý (Unbend)"),true);mode->setCurrentIndex(initial.unbend?1:0);
    form->addRow(tr("Stav"),mode);
    const auto field=[&](const char* name,double value,double minimum,double maximum,const QString& suffix) {
        auto* spin=new QDoubleSpinBox(this);spin->setObjectName(name);spin->setDecimals(6);spin->setRange(minimum,maximum);spin->setSuffix(suffix);spin->setValue(value);return spin;
    };
    bend_radius_=field("bendRadius",initial.radius,0,1000000," mm");
    bend_angle_=field("bendAngle",initial.angle_degrees,0,180," °");
    auto* follows_thickness=new QCheckBox(tr("Poloměr podle tloušťky"),this);
    follows_thickness->setObjectName("bendRadiusFollowsThickness");
    follows_thickness->setChecked(initial.radius_follows_thickness);
    form->addRow(follows_thickness);
    form->addRow(tr("Vnitřní poloměr"),bend_radius_);form->addRow(tr("Úhel ohybu"),bend_angle_);
    zima::ui::bind_numeric_value_lock(bend_radius_,"radius",locks,[this]{notify_preview();});
    zima::ui::bind_numeric_value_lock(bend_angle_,"angle",locks,[this]{notify_preview();});
    const auto refresh_radius=[this,pending,defaults] {
        const QSignalBlocker blocker(bend_radius_);
        bend_radius_->setEnabled(!pending->radius_follows_thickness);
        bend_radius_->setValue(pending->radius_follows_thickness
            ?(pending->thickness_override?pending->thickness:defaults.thickness_mm.value_or(1))
            :pending->radius);
        if(auto* button=findChild<QPushButton*>("bendPathSketchButton"))button->setEnabled(pending->angle_degrees>0);
    };
    const auto publish=[this,pending,changed,refresh_radius]{refresh_radius();changed(*pending);notify_preview();};
    connect(follows_thickness,&QCheckBox::toggled,this,[this,pending,publish](bool enabled){
        if(!enabled)pending->radius=bend_radius_->value();
        pending->radius_follows_thickness=enabled;publish();
    });
    refresh_radius();
    connect(mode,&QComboBox::currentIndexChanged,this,[pending,publish](int index){pending->unbend=index==1;publish();});
    connect(bend_radius_,&QDoubleSpinBox::valueChanged,this,[pending,publish](double v){pending->radius=v;publish();});
    connect(bend_angle_,&QDoubleSpinBox::valueChanged,this,[pending,publish](double v){pending->angle_degrees=v;publish();});
    const auto override_field=[&](bool thickness) {
        auto* row=new QWidget(this);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
        auto* override=new QCheckBox(tr("Vlastní hodnota"),row);override->setObjectName(thickness?"bendThicknessOverride":"bendKFactorOverride");
        const double inherited=thickness?defaults.thickness_mm.value_or(1):defaults.k_factor;
        const bool local=thickness?initial.thickness_override:initial.k_factor_override;
        auto* spin=field(thickness?"bendThickness":"bendKFactor",local?(thickness?initial.thickness:initial.k_factor):inherited,
            thickness?.000001:0,thickness?1000000:1,thickness?" mm":"");
        override->setChecked(local);spin->setEnabled(local);layout->addWidget(override);layout->addWidget(spin,1);
        form->addRow(thickness?tr("Tloušťka materiálu"):tr("K faktor"),row);
        connect(spin,&QDoubleSpinBox::valueChanged,this,[pending,publish,thickness](double value){
            if(thickness)pending->thickness=value;else pending->k_factor=value;publish();
        });
        connect(override,&QCheckBox::toggled,this,[pending,publish,spin,inherited,thickness](bool enabled){
            if(thickness)pending->thickness_override=enabled;else pending->k_factor_override=enabled;
            const QSignalBlocker blocker(spin);spin->setValue(enabled?(thickness?pending->thickness:pending->k_factor):inherited);
            spin->setEnabled(enabled);publish();
        });
    };
    override_field(true);override_field(false);
    auto* note=new QLabel(tr("Bez vlastní hodnoty se použije nastavení plechu v dílu."),this);note->setWordWrap(true);form->addRow(note);
    content_layout()->insertLayout(content_layout()->indexOf(sketch_button_),form);edit_pending_sketch_=std::move(edit_sketch);
    sketch_button_->setText(tr("Počáteční profil…"));
    for(std::size_t stage:{0u,1u}) {
        auto* button=new QPushButton(stage==0?tr("Trajektorie…"):tr("Koncový profil…"),this);
        button->setObjectName(stage==0?"bendPathSketchButton":"bendEndSketchButton");
        button->setMinimumHeight(40);
        style_sketch_button(button);
        if(stage==0)button->setToolTip(tr("Kóta trajektorie měří vnější poloměr: vnitřní poloměr + tloušťka materiálu."));
        content_layout()->insertWidget(content_layout()->indexOf(sketch_button_)+(stage?1:0),button);
        connect(button,&QPushButton::clicked,this,[edit_bend_sketch,stage]{if(edit_bend_sketch)edit_bend_sketch(stage);});
    }
    set_bend_parameters_=[this,pending,changed,mode,follows_thickness,refresh_radius](auto value) {
        *pending=std::move(value);
        const QSignalBlocker angle(bend_angle_),state(mode),linked(follows_thickness);
        bend_angle_->setValue(pending->angle_degrees);mode->setCurrentIndex(pending->unbend?1:0);
        follows_thickness->setChecked(pending->radius_follows_thickness);refresh_radius();changed(*pending);
    };
    refresh_radius();
}

void SketchPropertiesDialog::set_pending_bend_parameters(zima::document::BendParameters value) {
    if(set_bend_parameters_)set_bend_parameters_(std::move(value));
    notify_preview();
}

std::optional<zima::kernel::DimensionLayout> SketchPropertiesDialog::pending_dimension_layout(
    const zima::kernel::EdgeReference& reference) const {
    if (!holes_diameter_ || reference.owner_id != initial_.owner_container_id ||
        reference.semantic_key != "parameter:diameter") return {};
    return holes_dimension_layout_.value_or(zima::kernel::DimensionLayout{0,8.0,0,0});
}

bool SketchPropertiesDialog::set_pending_dimension_layout(
    const zima::kernel::EdgeReference& reference, zima::kernel::DimensionLayout layout) {
    if (!pending_dimension_layout(reference)) return false;
    zima::kernel::validate_dimension_layout(layout);
    holes_dimension_layout_ = layout;
    if (holes_layout_changed_) holes_layout_changed_(std::move(layout));
    notify_preview();
    return true;
}

void SketchPropertiesDialog::set_pending_sketch(zima::sketcher::Sketch sketch) {
    if (sketch.id != initial_.id) throw std::invalid_argument("Sketch identity changed");
    initial_ = std::move(sketch);
    error_->clear();
    notify_preview();
}

void SketchPropertiesDialog::update_plane_fields_enabled() {
    plane_->setEnabled(true);
    offset_->setEnabled(true);
}

void SketchPropertiesDialog::set_reference_request_callback(
        ReferenceRequestCallback callback) {
    placement_->set_reference_request_callback(std::move(callback));
}

void SketchPropertiesDialog::set_origin_selection_mode_callback(
        std::function<void(bool)> callback) {
    if (placement_) placement_->set_origin_selection_mode_callback(std::move(callback));
}

void SketchPropertiesDialog::set_origin_selection_mode_active(bool active) {
    if (placement_) placement_->set_origin_selection_mode_active(active);
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

bool SketchPropertiesDialog::mutate_sketch(const std::string& id,
    const std::function<void(zima::sketcher::Sketch&)>& mutation) {
    if (initial_.id != id) return bend_sketch_mutator&&bend_sketch_mutator(id,mutation);
    auto next = initial_;
    mutation(next); next.validate();
    initial_ = std::move(next);
    error_->clear();
    notify_preview();
    return true;
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
        selected_work_plane(plane_).toInt());
    sketch.plane_auto = automatic_work_plane(plane_);
    sketch.plane_offset = offset_->value();
    sketch.plane_reference_owner_id.clear();
    auto placement = placement_->numeric_placement();
    if(initial_placement_.value_locks.contains("profile_offset"))placement.value_locks.insert("profile_offset");else placement.value_locks.erase("profile_offset");
    placement.references = placement_->combined_references(3);
    zima::document::normalize_sketch_front_references(placement.references);
    if (sketch.plane_auto && zima::document::sketch_placement_uses_front_plane(placement.references))
        sketch.plane = zima::sketcher::SketchPlane::XZ;
    return {std::move(sketch), std::move(placement)};
}

void SketchPropertiesDialog::notify_preview() {
    if (!preview_) return;
    auto [sketch, placement] = current_values();
    preview_(sketch, placement);
}

void SketchPropertiesDialog::refresh_resolved_placement() {
    auto value = placement_->numeric_placement();
    value.references = placement_->combined_references(3);
    zima::document::normalize_sketch_front_references(value.references);
    if (zima::document::sketch_placement_uses_front_plane(value.references) &&
        plane_->itemData(plane_->findData(QStringLiteral("auto")), Qt::UserRole + 1).toInt() !=
            static_cast<int>(zima::sketcher::SketchPlane::XZ))
        update_automatic_work_plane(plane_, static_cast<int>(zima::sketcher::SketchPlane::XZ));
    zima::kernel::Vec3 base_rotation;
    bool orientation_from_reference = false;
    const bool placement_valid = zima::document::resolve_placement(
        value, reference_geometry_, &base_rotation,
        &orientation_from_reference);
    const auto state = zima::document::point_constraint_state(
        value.references, reference_geometry_, {value.x,value.y,value.z});
    placement_->set_translation_constraint_state(
        state, {value.x, value.y, value.z});
    placement_->set_rotation_constraint_state(
        zima::document::orientation_constraint_state(
            value.references, reference_geometry_, true,
            {value.x, value.y, value.z}));
    placement_->set_orientation_base_rotation(
        base_rotation, orientation_from_reference);
    placement_->set_resolved_rotation(
        {value.rotation_x, value.rotation_y, value.rotation_z},
        placement_valid);
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
    const bool first_plane_reference = index == 0 && reference.supports_offset;
    // Shared normalization keeps later positional planes from replacing
    // FRONT. Preserve explicit direction rows: after an anchor point, the
    // curve in row 1 must orient the frame using its tangent at that point.
    QString error;
    const bool accepted = placement_->set_reference(
        index, std::move(reference), label, &error,
        /*derive_orientation=*/index == 0);
    if (accepted && first_plane_reference) {
        // The first planar placement reference is mirrored into FRONT, which
        // maps its normal onto the container's local Y axis. Therefore local
        // XZ — not the default XY — is the actual sketch plane parallel to
        // the picked reference. This keeps placement, preview and the frame
        // later opened in Sketcher on one and the same plane.
        update_automatic_work_plane(plane_, static_cast<int>(zima::sketcher::SketchPlane::XZ), label);
        notify_preview();
    }
    if (accepted) refresh_resolved_placement();
    return accepted;
}

std::size_t SketchPropertiesDialog::first_empty_position_index() const {
    return placement_->first_empty_position_index();
}

void SketchPropertiesDialog::set_active_reference_index(
    std::optional<std::size_t> index) {
    placement_->set_active_reference_index(index);
}

void SketchPropertiesDialog::set_reference_inspected(
    std::size_t index, bool inspected) {
    placement_->set_reference_inspected(index, inspected);
}

void SketchPropertiesDialog::clear_reference_highlights() {
    placement_->clear_reference_highlights();
}

void SketchPropertiesDialog::set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution) {
    placement_->set_translation_constraint_state(state, solution);
}

void SketchPropertiesDialog::set_remaining_rotation_dof(int dof) {
    placement_->set_remaining_rotation_dof(dof);
}

void SketchPropertiesDialog::set_rotation_constraint_state(
        const zima::document::OrientationConstraintState& state) {
    placement_->set_rotation_constraint_state(state);
}

void SketchPropertiesDialog::set_orientation_base_rotation(
        const zima::kernel::Vec3& rotation, bool constrained) {
    placement_->set_orientation_base_rotation(rotation, constrained);
}

void SketchPropertiesDialog::set_resolved_rotation(
        const zima::kernel::Vec3& rotation, bool valid) {
    placement_->set_resolved_rotation(rotation, valid);
}

bool SketchPropertiesDialog::set_inline_parameter_value(
    std::string_view key, double value) {
    const auto set_field = [value](QDoubleSpinBox* field) {
        if (field == nullptr || !field->isEnabled() || field->isReadOnly() || !field->isVisible())
            return false;
        field->setValue(value);
        return true;
    };
    if (key == "profile_offset") return set_field(offset_);
    if (key == "diameter") return set_field(holes_diameter_);
    if (key == "radius") return set_field(bend_radius_);
    if (key == "angle") return set_field(bend_angle_);
    constexpr std::string_view placement_prefix{"placement:"};
    if (!key.starts_with(placement_prefix)) return false;
    key.remove_prefix(placement_prefix.size());
    const auto& translation = placement_->translation_fields();
    if (key == "x") return set_field(translation[0]);
    if (key == "y") return set_field(translation[1]);
    if (key == "z") return set_field(translation[2]);
    if (key == "rotation_x" || key == "rotation_y" ||
        key == "rotation_z") {
        const std::size_t index = key == "rotation_x" ? 0
            : key == "rotation_y" ? 1 : 2;
        if (placement_->rotation_fields()[index] && placement_->rotation_fields()[index]->isEnabled() && placement_->rotation_fields()[index]->isReadOnly()) return false;
        if (set_field(placement_->rotation_fields()[index])) return true;
        return set_field(placement_->rotation_offset_fields()[index]);
    }
    constexpr std::string_view reference_prefix{"reference_offset:"};
    if (!key.starts_with(reference_prefix)) return false;
    const auto suffix = key.substr(reference_prefix.size());
    if (suffix.empty()) return false;
    std::size_t index{};
    for (const char digit : suffix) {
        if (digit < '0' || digit > '9') return false;
        index = index * 10 + static_cast<std::size_t>(digit - '0');
    }
    return placement_->set_reference_offset(index, value);
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
        commit_(std::move(result), std::move(resolved_placement),
            enter_sketch_after_commit_);
    } catch (const std::exception& failure) {
        error_->setText(tr(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app

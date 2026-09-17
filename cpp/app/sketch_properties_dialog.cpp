#include "work_plane_selection.hpp"
#include <zima/document/sketch_placement.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
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
#include <cmath>

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
    bend_seed_from_edge_=!edit_mode;
    zima::document::normalize_container_front_references(initial_placement_.references);
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
    flat_pending_=pending;flat_changed_=changed;
    auto* form=new QFormLayout;
    auto* direction=new QComboBox(this);direction->setObjectName("flatDirection");
    direction->addItem(tr("První strana"),static_cast<int>(zima::document::ExtrusionDirection::Forward));
    direction->addItem(tr("Druhá strana"),static_cast<int>(zima::document::ExtrusionDirection::Reverse));
    direction->addItem(tr("Symetricky"),static_cast<int>(zima::document::ExtrusionDirection::Symmetric));
    direction->setCurrentIndex(direction->findData(static_cast<int>(initial.direction)));
    auto* direction_row=new QWidget(this);
    auto* direction_layout=new QHBoxLayout(direction_row);
    direction_layout->setContentsMargins(0,0,0,0);
    auto* direction_switch=new QPushButton(tr("Přepnout"),direction_row);
    direction_switch->setObjectName("flatDirectionSwitch");
    direction_layout->addWidget(direction,1);
    direction_layout->addWidget(direction_switch);
    connect(direction_switch,&QPushButton::clicked,this,[direction] {
        direction->setCurrentIndex((direction->currentIndex()+1)%direction->count());
    });
    auto* custom=new QCheckBox(tr("Vlastní tloušťka"),this);custom->setObjectName("flatThicknessOverride");custom->setChecked(initial.thickness_override);
    auto* thickness=new QDoubleSpinBox(this);thickness->setObjectName("flatThickness");
    thickness->setRange(.001,1000000);thickness->setDecimals(zima::ui::numeric_decimal_places(this,3));thickness->setSuffix(" mm");
    thickness->setToolTip(tr("Celková tloušťka plechu. Symetricky znamená polovinu na každé straně skici."));
    const double inherited=defaults.thickness_mm.value_or(1);
    const auto refresh=[=] {
        QSignalBlocker blocker(thickness);
        thickness->setValue(pending->thickness_override||pending->sheet_attachment?pending->thickness:inherited);
        thickness->setEnabled(pending->thickness_override&&!pending->sheet_attachment);
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
    form->addRow(tr("Směr"),direction_row);form->addRow(custom);form->addRow(tr("Tloušťka"),thickness);
    auto* other=new QPushButton(tr("Druhý konec hrany"),this);other->setObjectName("flatOtherEndpoint");form->addRow(other);
    connect(other,&QPushButton::clicked,this,[this] {
        if(!flat_pending_->sheet_attachment)return;
        const auto refs=placement_->combined_references(3);
        for(const auto& edge:reference_geometry_.edges)if(edge.reference.owner_id==refs[0].owner_id&&
            edge.reference.semantic_key==refs[0].semantic_key&&edge.reference.instance_path==refs[0].instance_path)
            for(const auto& point:edge.edge_treatment_endpoint_references)
                if(point.semantic_key!=refs[2].semantic_key) {
                    set_reference(2,{point.instance_path,point.owner_id,point.semantic_key},QString::fromStdString(point.semantic_key));return;
                }
    });
    auto* manual=new QPushButton(tr("Ruční reference umístění"),this);manual->setObjectName("flatManualPlacement");form->addRow(manual);
    connect(manual,&QPushButton::clicked,this,[this,pending,changed] {
        pending->sheet_attachment=false;changed(*pending);
        placement_->initialize_from_references(placement_->combined_references(3),[](const auto& key){return QString::fromStdString(key);});
        lock_flat_attachment_fields();refresh_resolved_placement();notify_preview();
    });
    content_layout()->insertLayout(content_layout()->indexOf(sketch_button_),form);
    edit_pending_sketch_=std::move(edit_sketch);
    lock_flat_attachment_fields();
}

void SketchPropertiesDialog::set_bend_mode(zima::document::BendParameters initial,
    const zima::document::SheetMetalDefaults& defaults,std::set<std::string>& locks,
    std::function<void(zima::document::BendParameters)> changed,std::function<void()> edit_sketch,
    std::function<void(std::size_t)> edit_bend_sketch) {
    set_internal_title(tr("Vlastnosti ohybu"));setObjectName("bendPropertiesDialog");
    auto pending=std::make_shared<zima::document::BendParameters>(initial);
    bend_pending_=pending;
    initial_.plane_auto=true;initial_.plane_offset=0;
    offset_->setValue(0);
    if(auto* plane_form=findChild<QFormLayout*>("sketchPlaneForm")) {
        plane_form->setRowVisible(plane_,false);plane_form->setRowVisible(offset_,false);
    }
    auto* form=new QFormLayout;
    auto* mode=new QComboBox(this);mode->setObjectName("bendState");
    mode->addItem(tr("Ohnutý"),false);mode->addItem(tr("Rozvinutý"),true);mode->setCurrentIndex(initial.unbend?1:0);
    form->addRow(tr("Stav"),mode);
    const auto field=[&](const char* name,double value,double minimum,double maximum,const QString& suffix) {
        auto* spin=new QDoubleSpinBox(this);spin->setObjectName(name);spin->setDecimals(6);spin->setRange(minimum,maximum);spin->setSuffix(suffix);spin->setValue(value);return spin;
    };
    bend_radius_=field("bendRadius",initial.radius,0,1000000," mm");
    bend_angle_=field("bendAngle",initial.angle_degrees,0,180," °");
    auto* custom_radius=new QCheckBox(tr("Vlastní poloměr"),this);
    custom_radius->setObjectName("bendRadiusOverride");
    custom_radius->setChecked(!initial.radius_follows_thickness);
    auto* hem=new QCheckBox(tr("Lem"),this);
    hem->setObjectName("bendHem");
    hem->setToolTip(tr("Ohyb 180° s nulovým vnitřním poloměrem."));
    hem->setEnabled(!locks.contains("radius")&&!locks.contains("angle"));
    const auto is_hem=[](const auto& p){return !p.radius_follows_thickness&&p.radius==0.&&p.angle_degrees==180.;};
    hem->setChecked(is_hem(initial));
    auto previous=std::make_shared<zima::document::BendParameters>(initial);
    if(is_hem(*previous)){previous->angle_degrees=90.;previous->radius_follows_thickness=true;}
    form->addRow(hem);
    form->addRow(custom_radius);
    form->addRow(tr("Vnitřní poloměr"),bend_radius_);form->addRow(tr("Úhel ohybu"),bend_angle_);
    zima::ui::bind_numeric_value_lock(bend_radius_,"radius",locks,[this]{notify_preview();});
    zima::ui::bind_numeric_value_lock(bend_angle_,"angle",locks,[this]{notify_preview();});
    const auto refresh_radius=[this,pending,defaults,hem,custom_radius,is_hem] {
        const QSignalBlocker blocker(bend_radius_),hem_blocker(hem),custom_blocker(custom_radius),angle_blocker(bend_angle_);
        const bool closed=is_hem(*pending);
        hem->setChecked(closed);custom_radius->setChecked(!pending->radius_follows_thickness);
        custom_radius->setEnabled(!closed);bend_angle_->setEnabled(!closed);
        bend_angle_->setValue(pending->angle_degrees);
        bend_radius_->setEnabled(!closed&&!pending->radius_follows_thickness);
        bend_radius_->setValue(pending->radius_follows_thickness
            ?(pending->thickness_override?pending->thickness:defaults.thickness_mm.value_or(1))
            :pending->radius);
        if(auto* button=findChild<QPushButton*>("bendPathSketchButton"))button->setEnabled(pending->angle_degrees>0);
    };
    const auto publish=[this,pending,changed,refresh_radius]{refresh_radius();changed(*pending);notify_preview();};
    connect(hem,&QCheckBox::toggled,this,[pending,previous,publish](bool enabled){
        if(enabled){*previous=*pending;pending->angle_degrees=180.;pending->radius=0.;pending->radius_follows_thickness=false;}
        else {pending->angle_degrees=previous->angle_degrees;pending->radius=previous->radius;pending->radius_follows_thickness=previous->radius_follows_thickness;}
        publish();
    });
    connect(custom_radius,&QCheckBox::toggled,this,[this,pending,publish](bool enabled){
        if(enabled)pending->radius=bend_radius_->value();
        pending->radius_follows_thickness=!enabled;publish();
    });
    refresh_radius();
    auto* change_end=new QPushButton(tr("Druhý konec hrany"),this);
    change_end->setObjectName("bendOtherEndpoint");form->addRow(change_end);
    connect(change_end,&QPushButton::clicked,this,[this] {
        if(!bend_pending_->sheet_attachment)return;
        const auto refs=placement_->combined_references(3);
        if(refs.size()<3)return;
        for(const auto& edge:reference_geometry_.edges) {
            if(edge.reference.owner_id!=refs[0].owner_id||edge.reference.semantic_key!=refs[0].semantic_key||
                edge.reference.instance_path!=refs[0].instance_path)continue;
            for(const auto& end:edge.edge_treatment_endpoint_references) {
                if(end.owner_id==refs[2].owner_id&&end.semantic_key==refs[2].semantic_key&&end.instance_path==refs[2].instance_path)continue;
                set_reference(2,{end.instance_path,end.owner_id,end.semantic_key},QString::fromStdString(end.semantic_key));return;
            }
        }
    });
    auto* manual=new QPushButton(tr("Ruční reference umístění"),this);
    manual->setObjectName("bendManualPlacement");form->addRow(manual);
    connect(manual,&QPushButton::clicked,this,[this,pending,changed] {
        if(pending->sheet_attachment) {
            auto detached=current_values().first;
            const auto prefix=detached.id+":attachment:";
            for(auto& dimension:detached.dimensions)if(dimension.first_point_id.starts_with(prefix)) {
                dimension.first_point_id="sketch_origin";
                dimension.value=detached.find_point(dimension.second_point_id)->x;
                dimension.solution_side=std::signbit(dimension.value)?-1:1;
            }
            std::erase_if(detached.external_references,[&](const auto& r){return r.id.starts_with(prefix);});
            initial_=std::move(detached);
        }
        pending->sheet_attachment=false;changed(*pending);
        placement_->initialize_from_references(placement_->combined_references(3),[](const auto& key){return QString::fromStdString(key);});
        refresh_resolved_placement();notify_preview();
    });
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
    set_bend_parameters_=[this,pending,changed,mode,custom_radius,refresh_radius](auto value) {
        *pending=std::move(value);
        const QSignalBlocker angle(bend_angle_),state(mode),linked(custom_radius);
        bend_angle_->setValue(pending->angle_degrees);mode->setCurrentIndex(pending->unbend?1:0);
        custom_radius->setChecked(!pending->radius_follows_thickness);refresh_radius();changed(*pending);
    };
    refresh_radius();
    lock_bend_attachment_fields();
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
    bend_seed_from_edge_=false;
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
    placement_->set_reference_request_callback([this,callback=std::move(callback)](std::size_t index) {
        if(((bend_pending_&&bend_pending_->sheet_attachment)||(flat_pending_&&flat_pending_->sheet_attachment))&&index!=0&&index!=2)return;
        callback(index);
    });
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
    if(next.serialized()!=initial_.serialized())bend_seed_from_edge_=false;
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
    if(bend_pending_) {sketch.plane_auto=true;sketch.plane_offset=0;sketch.plane=zima::sketcher::SketchPlane::XY;}
    sketch.plane_reference_owner_id.clear();
    auto placement = placement_->numeric_placement();
    if(initial_placement_.value_locks.contains("profile_offset"))placement.value_locks.insert("profile_offset");else placement.value_locks.erase("profile_offset");
    placement.references = placement_->combined_references(3);
    zima::document::normalize_container_front_references(placement.references);
    if (sketch.plane_auto && bend_radius_ &&
            zima::document::bend_attachment_profile_direction(placement.references, reference_geometry_))
        sketch.plane = zima::sketcher::SketchPlane::XY;
    else if (sketch.plane_auto && zima::document::sketch_placement_uses_front_plane(placement.references))
        sketch.plane = zima::sketcher::SketchPlane::XZ;
    if((bend_pending_&&bend_pending_->sheet_attachment)||(flat_pending_&&flat_pending_->sheet_attachment)) {
        placement.rotation_offset_x=placement.rotation_offset_y=placement.rotation_offset_z=0;
        placement.orientation_back=false;placement.orientation_quarter_turns=0;
        zima::document::PartDocument carrier;
        carrier.document_id=reference_document_id_.empty()?sketch.id:reference_document_id_;
        auto feature=zima::document::PartDocument::create_sketch_container();
        feature.id=sketch.owner_container_id;feature.feature_kind=zima::document::FeatureKind::Bend;
        if(bend_pending_)feature.bend=*bend_pending_;
        else {feature.feature_kind=zima::document::FeatureKind::Flat;feature.flat=*flat_pending_;}
        feature.placement=placement;
        carrier.history={feature};carrier.sketches={sketch};carrier.resolve_constructions(reference_geometry_);
        sketch=carrier.sketches.front();
        if(flat_pending_) {flat_pending_->thickness=carrier.history.front().flat.thickness;flat_changed_(*flat_pending_);}
    }
    return {std::move(sketch), std::move(placement)};
}

void SketchPropertiesDialog::lock_bend_attachment_fields() {
    if(!bend_pending_)return;
    const bool automatic=bend_pending_->sheet_attachment;
    if(auto* box=findChild<QCheckBox*>("bendThicknessOverride"))box->setEnabled(!automatic);
    if(auto* thickness=findChild<QDoubleSpinBox*>("bendThickness")) {
        thickness->setEnabled(!automatic&&bend_pending_->thickness_override);
        if(automatic) {const QSignalBlocker blocker(thickness);thickness->setValue(bend_pending_->thickness);}
    }
    if(auto* button=findChild<QPushButton*>("bendOtherEndpoint"))button->setEnabled(automatic);
    for(const auto* name:{"containerOrientationFlipButton","containerOrientationRotateButton"})
        if(auto* button=findChild<QPushButton*>(name))button->setEnabled(!automatic);
    if(!automatic)return;
    for(auto* field:placement_->rotation_offset_fields())if(field) {
        const QSignalBlocker blocker(field);field->setValue(0);field->setEnabled(false);
    }
    auto* table=placement_->reference_table();
    for(int row=0;row<table->rowCount();++row) {
        for(int column:{0,2})if(auto* widget=table->cellWidget(row,column))widget->setEnabled(false);
        if(row==1)if(auto* item=table->item(row,1))item->setFlags(item->flags()&~Qt::ItemIsEnabled);
    }
    if(auto* orientation=placement_->orientation_table()) {
        for(int row=0;row<orientation->rowCount();++row) {
            for(int column:{0,2,4})if(auto* widget=orientation->cellWidget(row,column))widget->setEnabled(false);
            if(auto* item=orientation->item(row,1))item->setFlags(item->flags()&~Qt::ItemIsEnabled);
        }
    }
}

void SketchPropertiesDialog::lock_flat_attachment_fields() {
    if(!flat_pending_)return;
    const bool automatic=flat_pending_->sheet_attachment;
    plane_->setEnabled(!automatic);
    if(auto* form=findChild<QFormLayout*>("sketchPlaneForm"))form->setRowVisible(plane_,!automatic);
    if(auto* box=findChild<QCheckBox*>("flatThicknessOverride"))box->setEnabled(!automatic);
    if(auto* field=findChild<QDoubleSpinBox*>("flatThickness")) {
        field->setEnabled(!automatic&&flat_pending_->thickness_override);
        if(automatic){const QSignalBlocker blocker(field);field->setValue(flat_pending_->thickness);}
    }
    if(auto* button=findChild<QPushButton*>("flatOtherEndpoint"))button->setEnabled(automatic);
    for(const auto* name:{"containerOrientationFlipButton","containerOrientationRotateButton"})
        if(auto* button=findChild<QPushButton*>(name))button->setEnabled(!automatic);
    if(!automatic)return;
    for(auto* field:placement_->rotation_offset_fields())if(field) {
        const QSignalBlocker blocker(field);field->setValue(0);field->setEnabled(false);
    }
    auto* table=placement_->reference_table();
    for(int row=0;row<table->rowCount();++row) {
        for(int column:{0,2})if(auto* widget=table->cellWidget(row,column))widget->setEnabled(false);
        if(row==1)if(auto* item=table->item(row,1))item->setFlags(item->flags()&~Qt::ItemIsEnabled);
    }
    if(auto* orientation=placement_->orientation_table())for(int row=0;row<orientation->rowCount();++row) {
        for(int column:{0,2,4})if(auto* widget=orientation->cellWidget(row,column))widget->setEnabled(false);
        if(auto* item=orientation->item(row,1))item->setFlags(item->flags()&~Qt::ItemIsEnabled);
    }
}

bool SketchPropertiesDialog::sheet_reference_allowed(std::size_t index,const zima::document::ConstructionReference& reference) const {
    if(!bend_pending_&&objectName()!="flatPropertiesDialog")return true;
    const auto edge=std::ranges::find_if(reference_geometry_.edges,[&](const auto& e) {
        return e.reference.owner_id==reference.owner_id&&e.reference.semantic_key==reference.semantic_key&&e.reference.instance_path==reference.instance_path;
    });
    if(edge!=reference_geometry_.edges.end()) {
        if(zima::kernel::sheet_edge_role(*edge)==zima::kernel::SheetEdgeRole::Thickness)return false;
        if(flat_pending_&&index==0) {
            const bool sheet=zima::kernel::sheet_edge_role(*edge)!=zima::kernel::SheetEdgeRole::Unknown;
            if(sheet) {
                try{return zima::document::bend_attachment_profile_direction(zima::document::flat_sheet_references(*edge),reference_geometry_).has_value();}
                catch(const std::exception&){return false;}
            }
        }
        if(!bend_pending_&&!(flat_pending_&&flat_pending_->sheet_attachment))return true;
        if(index==0&&zima::kernel::sheet_edge_role(*edge)==zima::kernel::SheetEdgeRole::Boundary) {
            try {return zima::document::bend_attachment_profile_direction(zima::document::bend_sheet_references(*edge),reference_geometry_).has_value();}
            catch(const std::exception&){return false;}
        }
    }
    if(!(bend_pending_&&bend_pending_->sheet_attachment)&&!(flat_pending_&&flat_pending_->sheet_attachment))return true;
    if(index!=2)return false;
    const auto refs=placement_->combined_references(3);
    if(refs.empty())return false;
    for(const auto& e:reference_geometry_.edges)if(e.reference.owner_id==refs[0].owner_id&&e.reference.semantic_key==refs[0].semantic_key&&e.reference.instance_path==refs[0].instance_path)
        return std::ranges::any_of(e.edge_treatment_endpoint_references,[&](const auto& p){return p.owner_id==reference.owner_id&&p.semantic_key==reference.semantic_key&&p.instance_path==reference.instance_path;});
    return false;
}

void SketchPropertiesDialog::notify_preview() {
    if (!preview_) return;
    try {
        auto [sketch, placement] = current_values();
        if((bend_pending_&&bend_pending_->sheet_attachment)||(flat_pending_&&flat_pending_->sheet_attachment))initial_=sketch;
        lock_flat_attachment_fields();
        preview_(sketch, placement);error_->clear();
    } catch(const std::exception& failure) {error_->setText(QString::fromUtf8(failure.what()));}
}

void SketchPropertiesDialog::refresh_resolved_placement() {
    auto value = placement_->numeric_placement();
    value.references = placement_->combined_references(3);
    zima::document::normalize_container_front_references(value.references);
    const auto automatic_plane = bend_radius_ &&
        zima::document::bend_attachment_profile_direction(value.references, reference_geometry_)
        ? zima::sketcher::SketchPlane::XY : zima::sketcher::SketchPlane::XZ;
    if (zima::document::sketch_placement_uses_front_plane(value.references) &&
        plane_->itemData(plane_->findData(QStringLiteral("auto")), Qt::UserRole + 1).toInt() !=
            static_cast<int>(automatic_plane))
        update_automatic_work_plane(plane_, static_cast<int>(automatic_plane));
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
    lock_bend_attachment_fields();
    lock_flat_attachment_fields();
}

std::vector<zima::document::ConstructionReference>
SketchPropertiesDialog::highlighted_reference_entries() const {
    return placement_->highlighted_reference_entries();
}

std::vector<zima::document::ConstructionReference>
SketchPropertiesDialog::references_without(std::size_t index) const {
    if(((bend_pending_&&bend_pending_->sheet_attachment)||(flat_pending_&&flat_pending_->sheet_attachment))&&index==0)return {};
    return placement_->references_without(index);
}

bool SketchPropertiesDialog::owns_reference_owner(
        const std::string& owner_id) const {
    return owner_id == initial_.owner_container_id;
}

bool SketchPropertiesDialog::set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label) {
    if(!sheet_reference_allowed(index,reference))return false;
    if(flat_pending_&&index==0) {
        const auto edge=std::ranges::find_if(reference_geometry_.edges,[&](const auto& e) {
            return e.reference.owner_id==reference.owner_id&&e.reference.semantic_key==reference.semantic_key&&e.reference.instance_path==reference.instance_path;
        });
        if(edge!=reference_geometry_.edges.end()&&zima::kernel::sheet_edge_role(*edge)==zima::kernel::SheetEdgeRole::Boundary) {
            const auto refs=zima::document::flat_sheet_references(*edge);
            if(!flat_pending_->sheet_attachment)flat_pending_->direction=zima::document::ExtrusionDirection::Reverse;
            flat_pending_->sheet_attachment=true;flat_pending_->thickness_override=false;
            flat_pending_->thickness=edge->edge_treatment_side_references.front().sheet_thickness;
            if(auto* field=findChild<QComboBox*>("flatDirection")) {
                const QSignalBlocker blocker(field);field->setCurrentIndex(field->findData(static_cast<int>(flat_pending_->direction)));
            }
            flat_changed_(*flat_pending_);
            placement_->initialize_from_references(refs,[](const auto& key){return QString::fromStdString(key);});
            refresh_resolved_placement();notify_preview();return true;
        }
    }
    if(bend_pending_&&index==0) {
        const auto edge=std::ranges::find_if(reference_geometry_.edges,[&](const auto& e) {
            return e.reference.owner_id==reference.owner_id&&e.reference.semantic_key==reference.semantic_key&&e.reference.instance_path==reference.instance_path;
        });
        if(edge!=reference_geometry_.edges.end()&&zima::kernel::sheet_edge_role(*edge)==zima::kernel::SheetEdgeRole::Boundary) {
            const auto refs=zima::document::bend_sheet_references(*edge);
            auto pending=*bend_pending_;pending.sheet_attachment=true;
            pending.thickness=edge->edge_treatment_side_references.front().sheet_thickness;set_bend_parameters_(pending);
            placement_->initialize_from_references(refs,[](const auto& key){return QString::fromStdString(key);});
            refresh_resolved_placement();notify_preview();return true;
        }
    }
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
    if (accepted) {
        refresh_resolved_placement();
        if(bend_radius_&&bend_seed_from_edge_&&!bend_pending_->sheet_attachment) {
            auto [sketch,placement]=current_values();
            zima::document::orient_bend_start_toward_edge(sketch,placement,reference_geometry_);
            initial_=std::move(sketch);notify_preview();
        }
    }
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
    lock_bend_attachment_fields();
}

void SketchPropertiesDialog::set_remaining_rotation_dof(int dof) {
    placement_->set_remaining_rotation_dof(dof);
}

void SketchPropertiesDialog::set_rotation_constraint_state(
        const zima::document::OrientationConstraintState& state) {
    placement_->set_rotation_constraint_state(state);
    lock_bend_attachment_fields();
}

void SketchPropertiesDialog::set_orientation_base_rotation(
        const zima::kernel::Vec3& rotation, bool constrained) {
    placement_->set_orientation_base_rotation(rotation, constrained);
    lock_bend_attachment_fields();
}

void SketchPropertiesDialog::set_resolved_rotation(
        const zima::kernel::Vec3& rotation, bool valid) {
    placement_->set_resolved_rotation(rotation, valid);
    lock_bend_attachment_fields();
}

bool SketchPropertiesDialog::set_inline_parameter_value(
    std::string_view key, double value) {
    const auto set_field = [value](QDoubleSpinBox* field) {
        if (field == nullptr || !field->isEnabled() || field->isReadOnly() || !field->isVisible())
            return false;
        field->setValue(value);
        return true;
    };
    if (key == "profile_offset") return !bend_pending_&&set_field(offset_);
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
    if((bend_pending_&&bend_pending_->sheet_attachment)||(flat_pending_&&flat_pending_->sheet_attachment))return false;
    return placement_->set_reference_offset(index, value);
}

bool SketchPropertiesDialog::submit() {
    const auto name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    try {
        auto [result, resolved_placement] = current_values();
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

#include "work_plane_selection.hpp"
#include <zima/document/sketch_placement.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/sheet_profile.hpp>
#include <zima/ui/numeric_value_lock.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include "sketch_button_style.hpp"
#include "feature_parameter_panel.hpp"
#include "sketch_properties_dialog.hpp"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace zima::app {

namespace {
// Sentinel userData for the "no Plane container reference" entry of
// plane_reference_ -- distinct from any real owner_id string, matched
// against a userData() of an empty QString.
constexpr const char* kNoPlaneReference = "";

void place_sketch_button_beside_work_plane(QWidget* plane_selector,
        QPushButton* sketch_button) {
    if(!plane_selector||!sketch_button)return;
    auto* plane_row=qobject_cast<QHBoxLayout*>(plane_selector->layout());
    if(!plane_row)return;
    sketch_button->setFixedHeight(30);
    sketch_button->setMinimumWidth(92);
    sketch_button->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    plane_row->addWidget(sketch_button);
}

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
    auto* plane_offset = new zima::ui::UnitDoubleSpinBox(zima::ui::InputQuantity::Length,this);
    offset_ = plane_offset;
    offset_->setObjectName("sketchPlaneOffset");
    offset_->setRange(-1'000'000.0, 1'000'000.0);
    plane_offset->set_display_decimals(zima::ui::numeric_decimal_places(this,3));
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
    plane_selector_=compact_work_plane_selector(plane_,[this] {
        if(reference_request_)reference_request_(0);
    });
    orientation_form->addRow(tr("Výchozí rovina"),plane_selector_);
    orientation_form->addRow(tr("Odsazení roviny"), offset_);
    content_layout()->addLayout(orientation_form);
    placement_->install_dof_label(content_layout());
    sketch_button_ = new QPushButton(tr("Skica"), this);
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
    auto* field = new zima::ui::UnitDoubleSpinBox(zima::ui::InputQuantity::Length,this);
    holes_diameter_ = field;
    field->setObjectName("holesDiameter");
    field->set_display_decimals(zima::ui::numeric_decimal_places(this, 3));
    field->setRange(0.001, 1000000.0);
    field->setValue(diameter);
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
    if(pending->sheet_attachment)
        pending->direction=zima::document::ExtrusionDirection::Reverse;
    flat_pending_=pending;flat_changed_=changed;
    auto* form=new QFormLayout;
    form->setObjectName("flatForm");
    auto* direction=new QComboBox(this);direction->setObjectName("flatDirection");
    direction->addItem(tr("První strana"),static_cast<int>(zima::document::ExtrusionDirection::Forward));
    direction->addItem(tr("Druhá strana"),static_cast<int>(zima::document::ExtrusionDirection::Reverse));
    direction->addItem(tr("Symetricky"),static_cast<int>(zima::document::ExtrusionDirection::Symmetric));
    direction->setCurrentIndex(direction->findData(static_cast<int>(initial.direction)));
    auto* direction_row=new QWidget(this);
    direction_row->setObjectName("flatDirectionRow");
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
    auto* thickness=new zima::ui::UnitDoubleSpinBox(zima::ui::InputQuantity::Length,this);thickness->setObjectName("flatThickness");
    thickness->setRange(.001,1000000);thickness->set_display_decimals(zima::ui::numeric_decimal_places(this,3));
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
    // Keep the Flat-owned Sketch beside the work-plane View field, matching
    // Sheet Profile while preserving the ordinary Sketch dialog layout.
    content_layout()->removeWidget(sketch_button_);
    place_sketch_button_beside_work_plane(plane_selector_,sketch_button_);
    content_layout()->insertLayout(content_layout()->indexOf(error_),form);
    edit_pending_sketch_=std::move(edit_sketch);
    lock_flat_attachment_fields();
}

void SketchPropertiesDialog::set_sheet_profile_mode(
    zima::document::SheetProfileParameters initial,
    const zima::document::SheetMetalDefaults& defaults,
    std::set<std::string>& locks,
    std::function<void(zima::document::SheetProfileParameters)> changed,
    std::function<void()> edit_sketch) {
    set_internal_title(tr("Vlastnosti profilu"));
    setObjectName("sheetProfilePropertiesDialog");
    offset_->setValue(0);
    findChild<QFormLayout*>("sketchPlaneForm")->setRowVisible(offset_,false);
    auto pending=std::make_shared<zima::document::SheetProfileParameters>(initial);
    sheet_profile_pending_=pending;
    sheet_profile_changed_=changed;
    sheet_profile_width_panel_=new FeatureParameterPanel(this,initial.width);
    sheet_profile_width_panel_->setObjectName("sheetProfileWidthPanel");
    sheet_profile_width_panel_->set_linear_extent_only(true);
    sheet_profile_width_panel_->bind_side_locks(locks,[this]{notify_preview();});
    auto* custom=new QCheckBox(tr("Nedržet se tloušťky plechu"),this);
    custom->setObjectName("sheetProfileThicknessOverride");
    custom->setChecked(initial.thickness_override);
    auto* thickness=new zima::ui::UnitDoubleSpinBox(
        zima::ui::InputQuantity::Length,this);
    thickness->setObjectName("sheetProfileThickness");
    thickness->setRange(.001,1000000);
    thickness->set_display_decimals(zima::ui::numeric_decimal_places(this,3));
    const double inherited=defaults.thickness_mm.value_or(1);
    const auto refresh=[=] {
        const QSignalBlocker blocker(thickness);
        thickness->setValue(pending->thickness_override||
            pending->sheet_attachment?pending->thickness:inherited);
        thickness->setEnabled(pending->thickness_override&&
            !pending->sheet_attachment);
    };
    refresh();
    zima::ui::bind_numeric_value_lock(thickness,"thickness",locks,
        [this]{notify_preview();});
    // Keep the profile-owned Sketch beside its work-plane View field. The
    // ordinary Sketch dialog keeps its established full-width button.
    content_layout()->removeWidget(sketch_button_);
    place_sketch_button_beside_work_plane(plane_selector_,sketch_button_);

    auto* thickness_form=new QFormLayout;
    thickness_form->setObjectName("sheetProfileForm");
    thickness_form->addRow(custom);
    thickness_form->addRow(tr("Tloušťka"),thickness);
    auto* note=new QLabel(
        tr("Bez zaškrtnutí se použije tloušťka vybraného plechu nebo nastavení plechu v dílu."),this);
    note->setWordWrap(true);
    thickness_form->addRow(note);

    auto* width_scroll=new QScrollArea(this);
    width_scroll->setObjectName("sheetProfileWidthScroll");
    width_scroll->setFrameShape(QFrame::NoFrame);
    width_scroll->setWidgetResizable(true);
    width_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    width_scroll->setWidget(sheet_profile_width_panel_);
    const int insertion=content_layout()->indexOf(error_);
    content_layout()->insertLayout(insertion,thickness_form);
    content_layout()->insertWidget(insertion+1,width_scroll,1);
    // The same variable Feature area used by ordinary Feature Properties
    // scrolls independently. Placement and thickness rows remain anchored;
    // normal surplus window height stays below the complete form.
    const auto fit_profile_controls=[this,width_scroll] {
        for(auto* group:sheet_profile_width_panel_->findChildren<QGroupBox*>()) {
            group->setMinimumHeight(0);
            if(group->layout()) {
                group->layout()->invalidate();
                group->layout()->activate();
            }
            group->setMinimumHeight(group->sizeHint().height());
            group->updateGeometry();
        }
        sheet_profile_width_panel_->setMinimumHeight(0);
        sheet_profile_width_panel_->layout()->invalidate();
        const int natural=sheet_profile_width_panel_->layout()->sizeHint().height();
        sheet_profile_width_panel_->setMinimumHeight(natural);
        sheet_profile_width_panel_->setMinimumWidth(0);
        const int natural_width=sheet_profile_width_panel_->layout()->minimumSize().width();
        sheet_profile_width_panel_->setMinimumWidth(natural_width);
        const int available_width=parentWidget()?parentWidget()->width()-20:1400;
        width_scroll->setMinimumWidth(std::min(
            natural_width+style()->pixelMetric(QStyle::PM_ScrollBarExtent),
            std::max(180,available_width-60)));
        width_scroll->setMinimumHeight(std::min(180,natural+4));
        width_scroll->setMaximumHeight(
            sheet_profile_width_panel_->active_limit_tables()?QWIDGETSIZE_MAX:natural+4);
        if(layout())layout()->activate();
        if(isVisible()) {
            const int available=parentWidget()?parentWidget()->height()-20:1000;
            width_scroll->setMinimumHeight(natural+4);
            content_layout()->invalidate();content_layout()->activate();
            if(layout()){layout()->invalidate();layout()->activate();}
            const int desired=sizeHint().height();
            if(desired>available) {
                width_scroll->setMinimumHeight(std::max(std::min(180,natural+4),
                    natural+4-(desired-available)));
                content_layout()->invalidate();content_layout()->activate();
                if(layout()){layout()->invalidate();layout()->activate();}
            }
            const auto margins=layout()->contentsMargins();
            const int desired_width=content_layout()->minimumSize().width()+
                margins.left()+margins.right()+
                style()->pixelMetric(QStyle::PM_ScrollBarExtent);
            resize(std::min(available_width,std::max({width(),
                minimumSizeHint().width(),desired_width})),
                std::min(available,sizeHint().height()));
        }
    };
    const auto schedule_fit=[this,fit_profile_controls] {
        QTimer::singleShot(0,this,fit_profile_controls);
    };
    for(std::size_t side=0;side<2;++side) {
        auto* limits=sheet_profile_width_panel_->limit_table(side);
        limits->on_pick([this,side]{request_sheet_profile_target(side);});
        limits->on_inspection([this]{notify_preview();});
        auto* end=sheet_profile_width_panel_->findChild<QComboBox*>(
            QString("featureSideEnd%1").arg(side));
        connect(end,&QComboBox::currentIndexChanged,this,
            [this,side,limits,schedule_fit](int index) {
                if(index==1&&sheet_profile_width_panel_->targets(side).empty())
                    limits->begin_entry(0);
                else if(index!=1&&sheet_profile_target_active_&&
                        sheet_profile_target_side_==side) {
                    finish_sheet_profile_target_entry();
                    if(sheet_profile_target_cancel_)sheet_profile_target_cancel_();
                }
                schedule_fit();
            });
        connect(sheet_profile_width_panel_->findChild<QComboBox*>(
                QString("featureSideMode%1").arg(side)),
            &QComboBox::currentIndexChanged,this,
            [schedule_fit](int){schedule_fit();});
    }
    connect(sheet_profile_width_panel_->findChild<QCheckBox*>("featureSymmetric"),
        &QCheckBox::toggled,this,[schedule_fit](bool){schedule_fit();});
    sheet_profile_width_panel_->on_preview_notice_changed(schedule_fit);
    const auto publish=[this,pending,changed,refresh,schedule_fit] {
        refresh();changed(*pending);notify_preview();schedule_fit();
    };
    sheet_profile_width_panel_->on_swap([this,pending,publish] {
        sheet_profile_width_panel_->swap_sides();
        pending->width=sheet_profile_width_panel_->parameters();publish();
    });
    sheet_profile_width_panel_->on_change([this,pending,publish] {
        pending->width=sheet_profile_width_panel_->parameters();
        refresh_sheet_profile_targets();publish();
    });
    connect(custom,&QCheckBox::toggled,this,[=](bool enabled) {
        if(enabled)pending->thickness=thickness->value();
        pending->thickness_override=enabled;publish();
    });
    connect(thickness,&QDoubleSpinBox::valueChanged,this,[=](double value) {
        pending->thickness=value;changed(*pending);notify_preview();
    });
    edit_pending_sketch_=std::move(edit_sketch);
    refresh_sheet_profile_targets();
    lock_sheet_profile_attachment_fields();
    schedule_fit();
}

void SketchPropertiesDialog::refresh_sheet_profile_targets() {
    if(!sheet_profile_width_panel_)return;
    for(std::size_t side=0;side<2;++side)
        sheet_profile_width_panel_->limit_table(side)->refresh();
}

void SketchPropertiesDialog::request_sheet_profile_target(std::size_t side) {
    if(!sheet_profile_width_panel_||side>=2||
       sheet_profile_pending_->sheet_attachment)return;
    auto* limits=sheet_profile_width_panel_->limit_table(side);
    if(!limits->active_row())
        limits->begin_entry(sheet_profile_width_panel_->targets(side).size(),
            false);
    sheet_profile_target_side_=side;sheet_profile_target_active_=true;
    refresh_sheet_profile_targets();
    if(sheet_profile_target_request_)sheet_profile_target_request_();
}

void SketchPropertiesDialog::finish_sheet_profile_target_entry() {
    if(sheet_profile_width_panel_)
        for(std::size_t side=0;side<2;++side)
            sheet_profile_width_panel_->limit_table(side)->finish_entry();
    sheet_profile_target_active_=false;refresh_sheet_profile_targets();
}

void SketchPropertiesDialog::set_sheet_profile_end_target(
        zima::document::ExtrusionParameters::EndTarget target,
        std::optional<int> target_half_space) {
    if(!sheet_profile_width_panel_||!sheet_profile_target_active_)
        throw std::invalid_argument("Sheet Profile width target entry is not active.");
    auto width=sheet_profile_width_panel_->parameters();
    const auto side=sheet_profile_target_side_;
    if(target_half_space&&*target_half_space!=
            (width.side_direction_sign(side)>0?1:-1)) {
        if(side!=0)
            throw std::invalid_argument(
                "Sheet Profile Side 2 target must be opposite Side 1.");
        if(!sheet_profile_width_panel_->set_direction(
                -zima::document::feature_direction(width)))
            throw std::invalid_argument(
                "Sheet Profile width direction cannot face the selected target.");
        width=sheet_profile_width_panel_->parameters();
    }
    sheet_profile_width_panel_->limit_table(side)->assign(std::move(target),
        false);
    sheet_profile_pending_->width=sheet_profile_width_panel_->parameters();
    sheet_profile_changed_(*sheet_profile_pending_);
    finish_sheet_profile_target_entry();notify_preview();
}

zima::document::FeatureParameters
SketchPropertiesDialog::sheet_profile_width_parameters() const {
    return sheet_profile_width_panel_
        ?sheet_profile_width_panel_->parameters()
        :zima::document::FeatureParameters{};
}

bool SketchPropertiesDialog::set_sheet_profile_signed_length(
        std::size_t side,double value,bool baseline_reversed) {
    if(!sheet_profile_width_panel_||sheet_profile_pending_->sheet_attachment||
       !sheet_profile_width_panel_->set_signed_length(
            side,value,baseline_reversed))return false;
    sheet_profile_pending_->width=sheet_profile_width_panel_->parameters();
    sheet_profile_changed_(*sheet_profile_pending_);notify_preview();return true;
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
        plane_form->setRowVisible(plane_selector_,false);plane_form->setRowVisible(offset_,false);
    }
    auto* form=new QFormLayout;
    using Quantity=zima::ui::InputQuantity;
    const auto field=[&](const char* name,double value,double minimum,double maximum,Quantity quantity) {
        auto* spin=new zima::ui::UnitDoubleSpinBox(quantity,this);spin->setObjectName(name);spin->set_display_decimals(6);spin->setRange(minimum,maximum);spin->setValue(value);return spin;
    };
    bend_radius_=field("bendRadius",initial.radius,0,1000000,Quantity::Length);
    bend_angle_=field("bendAngle",initial.angle_degrees,0,180,Quantity::Angle);
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
    connect(bend_radius_,&QDoubleSpinBox::valueChanged,this,[pending,publish](double v){pending->radius=v;publish();});
    connect(bend_angle_,&QDoubleSpinBox::valueChanged,this,[pending,publish](double v){pending->angle_degrees=v;publish();});
    const auto override_field=[&](bool thickness) {
        auto* row=new QWidget(this);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
        auto* override=new QCheckBox(tr("Vlastní hodnota"),row);override->setObjectName(thickness?"bendThicknessOverride":"bendKFactorOverride");
        const double inherited=thickness?defaults.thickness_mm.value_or(1):defaults.k_factor;
        const bool local=thickness?initial.thickness_override:initial.k_factor_override;
        auto* spin=field(thickness?"bendThickness":"bendKFactor",local?(thickness?initial.thickness:initial.k_factor):inherited,
            thickness?.000001:0,thickness?1000000:1,thickness?Quantity::Length:Quantity::Scalar);
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
    auto* corners=new QWidget(this);auto* corner_layout=new QHBoxLayout(corners);corner_layout->setContentsMargins(0,0,0,0);
    auto* corner_first=new QCheckBox(tr("První konec"),corners);auto* corner_last=new QCheckBox(tr("Druhý konec"),corners);
    corner_first->setObjectName("bendCornerFirst");corner_last->setObjectName("bendCornerLast");
    corner_first->setChecked(initial.corner[0]);corner_last->setChecked(initial.corner[1]);
    corner_layout->addWidget(corner_first);corner_layout->addWidget(corner_last);form->addRow(tr("Uzavření rohu"),corners);
    const auto corner_tip=tr("Zakřivený přechod k prodloužení koncového profilu. Zapněte na obou sousedních profilech; vůle ubírá polovinu hodnoty na každém konci.");
    corners->setToolTip(corner_tip);
    auto* corner_gap=field("bendCornerGap",initial.corner_gap,0,1,Quantity::Length);
    corner_gap->setSingleStep(.01);corner_gap->setEnabled(initial.corner[0]||initial.corner[1]);
    form->addRow(tr("Vůle rohu"),corner_gap);
    connect(corner_first,&QCheckBox::toggled,this,[pending,publish,corner_gap](bool value){pending->corner[0]=value;corner_gap->setEnabled(value||pending->corner[1]);publish();});
    connect(corner_last,&QCheckBox::toggled,this,[pending,publish,corner_gap](bool value){pending->corner[1]=value;corner_gap->setEnabled(value||pending->corner[0]);publish();});
    connect(corner_gap,&QDoubleSpinBox::valueChanged,this,[pending,publish](double value){pending->corner_gap=value;publish();});
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
    set_bend_parameters_=[this,pending,changed,custom_radius,refresh_radius,corner_first,corner_last,corner_gap](auto value) {
        *pending=std::move(value);
        const QSignalBlocker first_block(corner_first),last_block(corner_last),gap_block(corner_gap);
        corner_first->setChecked(pending->corner[0]);corner_last->setChecked(pending->corner[1]);corner_gap->setValue(pending->corner_gap);
        corner_gap->setEnabled(pending->corner[0]||pending->corner[1]);
        const QSignalBlocker angle(bend_angle_),linked(custom_radius);
        bend_angle_->setValue(pending->angle_degrees);
        custom_radius->setChecked(!pending->radius_follows_thickness);refresh_radius();changed(*pending);
    };
    refresh_radius();
    lock_bend_attachment_fields();
}

void SketchPropertiesDialog::set_pending_bend_parameters(zima::document::BendParameters value) {
    if(set_bend_parameters_)set_bend_parameters_(std::move(value));
    notify_preview();
}

void SketchPropertiesDialog::set_profile_bend_correction_only() {
    name_->setEnabled(false);plane_selector_->setEnabled(false);
    offset_->setEnabled(false);sketch_button_->setEnabled(false);
    placement_->reference_table()->setEnabled(false);
    if(auto* table=placement_->orientation_table())table->setEnabled(false);
    for(auto* field:placement_->translation_fields())if(field)field->setEnabled(false);
    for(auto* field:placement_->rotation_fields())if(field)field->setEnabled(false);
    for(auto* field:placement_->rotation_offset_fields())if(field)field->setEnabled(false);
    const std::array<const char*,15> owned{
        "bendHem","bendRadiusOverride","bendRadius","bendAngle",
        "bendOtherEndpoint","bendManualPlacement","bendThicknessOverride",
        "bendThickness","bendKFactorOverride","bendKFactor",
        "bendCornerFirst","bendCornerLast","bendCornerGap",
        "bendPathSketchButton","sketchOpenButton"};
    for(const auto* name:owned)
        if(auto* widget=findChild<QWidget*>(name))widget->setEnabled(false);
    if(auto* end=findChild<QPushButton*>("bendEndSketchButton"))
        end->setEnabled(true);
}

void SketchPropertiesDialog::set_line_bend_mode(
        zima::document::LineBendParameters initial,
        const zima::document::SheetMetalDefaults& defaults,
        std::set<std::string>& locks,
        std::function<void(zima::document::LineBendParameters)> changed,
        std::function<void()> edit_sketch) {
    set_internal_title(tr("Vlastnosti ohybu podle čáry"));
    setObjectName("lineBendPropertiesDialog");
    auto pending=std::make_shared<zima::document::LineBendParameters>(initial);
    line_bend_pending_=pending;
    auto* form=new QFormLayout;
    form->setObjectName("lineBendForm");
    using Quantity=zima::ui::InputQuantity;
    const auto field=[&](const char* name,double value,double minimum,
            double maximum,Quantity quantity) {
        auto* spin=new zima::ui::UnitDoubleSpinBox(quantity,this);
        spin->setObjectName(name);spin->set_display_decimals(6);
        spin->setRange(minimum,maximum);spin->setValue(value);return spin;
    };
    auto* radius=field("lineBendRadius",initial.radius,.000001,
        1000000,Quantity::Length);
    auto* angle=field("lineBendAngle",initial.angle_degrees,.000001,
        180,Quantity::Angle);
    zima::ui::bind_numeric_value_lock(radius,"radius",locks,
        [this]{notify_preview();});
    zima::ui::bind_numeric_value_lock(angle,"angle",locks,
        [this]{notify_preview();});
    form->addRow(tr("Vnitřní poloměr"),radius);
    form->addRow(tr("Úhel ohybu"),angle);
    auto* fixed=new QComboBox(this);fixed->setObjectName("lineBendFixedSide");
    fixed->addItem(tr("Levá strana"),1);
    fixed->addItem(tr("Pravá strana"),-1);
    fixed->setCurrentIndex(fixed->findData(initial.fixed_side));
    fixed->setToolTip(tr(
        "Strana je určena ve směru od prvního ke druhému bodu čáry."));
    form->addRow(tr("Pevná strana"),fixed);
    auto* reverse=new QCheckBox(tr("Obrátit směr ohybu"),this);
    reverse->setObjectName("lineBendReverse");
    reverse->setChecked(initial.reverse);form->addRow(reverse);
    const auto publish=[this,pending,changed]{changed(*pending);notify_preview();};
    connect(radius,&QDoubleSpinBox::valueChanged,this,
        [pending,publish](double value){pending->radius=value;publish();});
    connect(angle,&QDoubleSpinBox::valueChanged,this,
        [pending,publish](double value){pending->angle_degrees=value;publish();});
    connect(fixed,&QComboBox::currentIndexChanged,this,
        [pending,publish,fixed](int){pending->fixed_side=fixed->currentData().toInt();publish();});
    connect(reverse,&QCheckBox::toggled,this,
        [pending,publish](bool value){pending->reverse=value;publish();});
    const auto override_field=[&](bool thickness) {
        auto* row=new QWidget(this);auto* layout=new QHBoxLayout(row);
        layout->setContentsMargins(0,0,0,0);
        auto* override=new QCheckBox(tr("Vlastní hodnota"),row);
        override->setObjectName(thickness?"lineBendThicknessOverride":
            "lineBendKFactorOverride");
        const double inherited=thickness?
            defaults.thickness_mm.value_or(1):defaults.k_factor;
        const bool local=thickness?initial.thickness_override:
            initial.k_factor_override;
        auto* spin=field(thickness?"lineBendThickness":"lineBendKFactor",
            local?(thickness?initial.thickness:initial.k_factor):inherited,
            thickness?.000001:0,thickness?1000000:1,
            thickness?Quantity::Length:Quantity::Scalar);
        override->setChecked(local);spin->setEnabled(local);
        layout->addWidget(override);layout->addWidget(spin,1);
        form->addRow(thickness?tr("Tloušťka materiálu"):tr("K faktor"),row);
        zima::ui::bind_numeric_value_lock(spin,
            thickness?"thickness":"k_factor",locks,[this]{notify_preview();});
        connect(spin,&QDoubleSpinBox::valueChanged,this,
            [pending,publish,thickness](double value) {
                if(thickness)pending->thickness=value;
                else pending->k_factor=value;
                publish();
            });
        connect(override,&QCheckBox::toggled,this,
            [pending,publish,spin,inherited,thickness](bool enabled) {
                if(thickness)pending->thickness_override=enabled;
                else pending->k_factor_override=enabled;
                const QSignalBlocker blocker(spin);
                spin->setValue(enabled?(thickness?pending->thickness:
                    pending->k_factor):inherited);
                spin->setEnabled(enabled);publish();
            });
    };
    override_field(true);override_field(false);
    auto* note=new QLabel(tr(
        "Skica musí obsahovat právě jednu běžnou přímou úsečku. Jedna strana plechu zůstane pevná."),this);
    note->setWordWrap(true);form->addRow(note);
    content_layout()->insertLayout(content_layout()->indexOf(sketch_button_),
        form);
    sketch_button_->setText(tr("Čára ohybu…"));
    edit_pending_sketch_=std::move(edit_sketch);
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
    reference_request_=std::move(callback);
    placement_->set_reference_request_callback([this](std::size_t index) {
        if(((bend_pending_&&bend_pending_->sheet_attachment)||(flat_pending_&&flat_pending_->sheet_attachment))&&index!=0&&index!=2)return;
        if(reference_request_)reference_request_(index);
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
    if(sheet_profile_pending_&&sheet_profile_pending_->sheet_attachment) {
        sketch.plane_auto=true;sketch.plane_offset=0;
        sketch.plane=zima::sketcher::SketchPlane::XZ;
    }
    sketch.plane_reference_owner_id.clear();
    auto placement = placement_->numeric_placement();
    if(initial_placement_.value_locks.contains("profile_offset"))placement.value_locks.insert("profile_offset");else placement.value_locks.erase("profile_offset");
    placement.references = placement_->combined_references(3);
    zima::document::normalize_container_front_references(placement.references);
    if (sketch.plane_auto && bend_radius_ &&
            zima::document::bend_attachment_profile_direction(placement.references, reference_geometry_))
        sketch.plane = zima::sketcher::SketchPlane::XY;
    else zima::document::apply_automatic_sketch_work_plane(sketch,placement.references);
    if((bend_pending_&&bend_pending_->sheet_attachment)||
       (flat_pending_&&flat_pending_->sheet_attachment)||
       (sheet_profile_pending_&&sheet_profile_pending_->sheet_attachment)) {
        placement.rotation_offset_x=placement.rotation_offset_y=placement.rotation_offset_z=0;
        placement.orientation_back=false;placement.orientation_quarter_turns=0;
        zima::document::PartDocument carrier;
        carrier.document_id=reference_document_id_.empty()?sketch.id:reference_document_id_;
        auto feature=zima::document::PartDocument::create_sketch_container();
        feature.id=sketch.owner_container_id;feature.feature_kind=zima::document::FeatureKind::Bend;
        if(bend_pending_)feature.bend=*bend_pending_;
        else if(flat_pending_) {feature.feature_kind=zima::document::FeatureKind::Flat;feature.flat=*flat_pending_;}
        else {feature.feature_kind=zima::document::FeatureKind::SheetProfile;feature.sheet_profile=*sheet_profile_pending_;}
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
    if(automatic)flat_pending_->direction=
        zima::document::ExtrusionDirection::Reverse;
    if(auto* form=findChild<QFormLayout*>("flatForm"))
        if(auto* direction=findChild<QWidget*>("flatDirectionRow"))
            form->setRowVisible(direction,!automatic);
    if(auto* direction=findChild<QComboBox*>("flatDirection")) {
        const QSignalBlocker blocker(direction);
        direction->setCurrentIndex(direction->findData(static_cast<int>(
            flat_pending_->direction)));
        direction->setEnabled(!automatic);
    }
    plane_->setEnabled(!automatic);
    if(auto* form=findChild<QFormLayout*>("sketchPlaneForm"))form->setRowVisible(plane_selector_,!automatic);
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

void SketchPropertiesDialog::lock_sheet_profile_attachment_fields() {
    if(!sheet_profile_pending_)return;
    const bool automatic=sheet_profile_pending_->sheet_attachment;
    if(sheet_profile_width_panel_)
        sheet_profile_width_panel_->setVisible(!automatic);
    if(automatic&&sheet_profile_target_active_)
        finish_sheet_profile_target_entry();
    plane_->setEnabled(!automatic);
    if(auto* form=findChild<QFormLayout*>("sketchPlaneForm"))
        form->setRowVisible(plane_selector_,!automatic);
    if(auto* box=findChild<QCheckBox*>("sheetProfileThicknessOverride"))
        box->setEnabled(!automatic);
    if(auto* thickness=findChild<QDoubleSpinBox*>("sheetProfileThickness")) {
        thickness->setEnabled(!automatic&&
            sheet_profile_pending_->thickness_override);
        if(automatic) {
            const QSignalBlocker blocker(thickness);
            thickness->setValue(sheet_profile_pending_->thickness);
        }
    }
    for(const auto* name:{"containerOrientationFlipButton",
            "containerOrientationRotateButton"})
        if(auto* button=findChild<QPushButton*>(name))
            button->setEnabled(!automatic);
    if(!automatic)return;
    for(auto* field:placement_->rotation_offset_fields())if(field) {
        const QSignalBlocker blocker(field);field->setValue(0);
        field->setEnabled(false);
    }
    auto* table=placement_->reference_table();
    for(int row=0;row<table->rowCount();++row) {
        for(int column:{0,2})if(auto* widget=table->cellWidget(row,column))
            widget->setEnabled(false);
        if(row==1)if(auto* item=table->item(row,1))
            item->setFlags(item->flags()&~Qt::ItemIsEnabled);
    }
    if(auto* orientation=placement_->orientation_table())
        for(int row=0;row<orientation->rowCount();++row) {
            for(int column:{0,2,4})
                if(auto* widget=orientation->cellWidget(row,column))
                    widget->setEnabled(false);
            if(auto* item=orientation->item(row,1))
                item->setFlags(item->flags()&~Qt::ItemIsEnabled);
        }
}

void SketchPropertiesDialog::prepare_flat_default_origin() {
    if(!flat_pending_)return;
    flat_pending_->sheet_attachment=false;flat_changed_(*flat_pending_);
    placement_->clear_reference_highlights();
    placement_->initialize_from_references({},[](const auto& key){return QString::fromStdString(key);});
    zima::document::Placement origin;
    origin.value_locks=placement_->numeric_placement().value_locks;
    placement_->initialize_numeric_values(origin);
    lock_flat_attachment_fields();refresh_resolved_placement();
}

bool SketchPropertiesDialog::sheet_reference_allowed(std::size_t index,const zima::document::ConstructionReference& reference) const {
    if(!is_sheet_edge_feature())return true;
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
        if(sheet_profile_pending_&&index==0&&
           zima::kernel::sheet_edge_role(*edge)==
                zima::kernel::SheetEdgeRole::Boundary) {
            try {
                return zima::document::bend_attachment_profile_direction(
                    zima::document::bend_sheet_references(*edge),
                    reference_geometry_).has_value();
            } catch(const std::exception&) {return false;}
        }
        if(!bend_pending_&&!(flat_pending_&&flat_pending_->sheet_attachment)&&
           !(sheet_profile_pending_&&sheet_profile_pending_->sheet_attachment))return true;
        if(index==0&&zima::kernel::sheet_edge_role(*edge)==zima::kernel::SheetEdgeRole::Boundary) {
            try {return zima::document::bend_attachment_profile_direction(zima::document::bend_sheet_references(*edge),reference_geometry_).has_value();}
            catch(const std::exception&){return false;}
        }
    }
    if(!(bend_pending_&&bend_pending_->sheet_attachment)&&
       !(flat_pending_&&flat_pending_->sheet_attachment)&&
       !(sheet_profile_pending_&&sheet_profile_pending_->sheet_attachment))return true;
    if(index!=2)return false;
    const auto refs=placement_->combined_references(3);
    if(refs.empty())return false;
    for(const auto& e:reference_geometry_.edges)if(e.reference.owner_id==refs[0].owner_id&&e.reference.semantic_key==refs[0].semantic_key&&e.reference.instance_path==refs[0].instance_path)
        return std::ranges::any_of(e.edge_treatment_endpoint_references,[&](const auto& p){return p.owner_id==reference.owner_id&&p.semantic_key==reference.semantic_key&&p.instance_path==reference.instance_path;});
    return false;
}

void SketchPropertiesDialog::notify_preview() {
    if (!preview_) return;
    if(reference_update_){reference_preview_pending_=true;return;}
    try {
        auto [sketch, placement] = current_values();
        if((bend_pending_&&bend_pending_->sheet_attachment)||
           (flat_pending_&&flat_pending_->sheet_attachment)||
           (sheet_profile_pending_&&sheet_profile_pending_->sheet_attachment))
            initial_=sketch;
        lock_flat_attachment_fields();
        lock_sheet_profile_attachment_fields();
        preview_(sketch, placement);error_->clear();
    } catch(const std::exception& failure) {error_->setText(tr(failure.what()));}
}

void SketchPropertiesDialog::refresh_resolved_placement() {
    auto value = placement_->numeric_placement();
    value.references = placement_->combined_references(3);
    zima::document::normalize_container_front_references(value.references);
    const auto automatic_plane = bend_radius_ &&
        zima::document::bend_attachment_profile_direction(value.references, reference_geometry_)
        ? zima::sketcher::SketchPlane::XY : zima::document::automatic_sketch_work_plane(value.references).value_or(initial_.plane);
    if (zima::document::automatic_sketch_work_plane(value.references).has_value() &&
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
    lock_sheet_profile_attachment_fields();
}

std::vector<zima::document::ConstructionReference>
SketchPropertiesDialog::highlighted_reference_entries() const {
    auto result=placement_->highlighted_reference_entries();
    if(sheet_profile_width_panel_)
        for(std::size_t side=0;side<2;++side)
            for(const auto& reference:
                    sheet_profile_width_panel_->limit_table(side)->inspected()) {
                zima::document::ConstructionReference entry;
                entry.instance_path=reference.instance_path;
                entry.owner_id=reference.owner_id;
                entry.semantic_key=reference.semantic_key;
                result.push_back(std::move(entry));
            }
    return result;
}

std::vector<zima::document::ConstructionReference>
SketchPropertiesDialog::references_without(std::size_t index) const {
    if(((bend_pending_&&bend_pending_->sheet_attachment)||
        (flat_pending_&&flat_pending_->sheet_attachment)||
        (sheet_profile_pending_&&sheet_profile_pending_->sheet_attachment))&&
       index==0)return {};
    return placement_->references_without(index);
}

bool SketchPropertiesDialog::owns_reference_owner(
        const std::string& owner_id) const {
    return owner_id == initial_.owner_container_id;
}

zima::document::ConstructionReference SketchPropertiesDialog::prepare_reference_candidate(
        std::size_t index,zima::document::ConstructionReference reference) const {
    if(!entering_whole_origin()&&automatic_work_plane(plane_))
        zima::document::prepare_profile_plane_reference(index,reference,placement_->combined_references(3));
    return reference;
}

bool SketchPropertiesDialog::set_reference(std::size_t index,
        zima::document::ConstructionReference reference,
        const QString& label) {
    // The shared row callback resolves placement before this command finishes
    // choosing its automatic work plane. Keep both updates synchronous, then
    // publish this Sketch's complete final draft only once.
    const bool outer_update=!reference_update_;
    const auto publish=qScopeGuard([this,outer_update] {
        if(outer_update&&std::exchange(reference_preview_pending_,false))notify_preview();
    });
    const QScopedValueRollback update(reference_update_,true);
    if(!sheet_reference_allowed(index,reference))return false;
    if(sheet_profile_pending_&&index==0) {
        const auto edge=std::ranges::find_if(reference_geometry_.edges,
            [&](const auto& value) {
                return value.reference.owner_id==reference.owner_id&&
                    value.reference.semantic_key==reference.semantic_key&&
                    value.reference.instance_path==reference.instance_path;
            });
        if(edge!=reference_geometry_.edges.end()&&
           zima::kernel::sheet_edge_role(*edge)==
                zima::kernel::SheetEdgeRole::Boundary) {
            const auto refs=zima::document::bend_sheet_references(*edge);
            const auto side=zima::document::bend_attachment_profile_direction(
                refs,reference_geometry_);
            if(!side)return false;
            sheet_profile_pending_->sheet_attachment=true;
            sheet_profile_pending_->material_side=1;
            const double thickness=edge->edge_treatment_side_references.front().sheet_thickness;
            sheet_profile_pending_->thickness_override=false;
            sheet_profile_pending_->thickness=thickness;
            if(auto* custom=findChild<QCheckBox*>(
                    "sheetProfileThicknessOverride")) {
                const QSignalBlocker blocker(custom);custom->setChecked(false);
            }
            if(auto* field=findChild<QDoubleSpinBox*>(
                    "sheetProfileThickness")) {
                const QSignalBlocker blocker(field);
                field->setValue(sheet_profile_pending_->thickness);
            }
            if(initial_.points.empty()&&initial_.segments.empty()&&
               initial_.arcs.empty()&&initial_.circles.empty()&&
               initial_.ellipses.empty()&&initial_.elliptical_arcs.empty()&&
               initial_.bsplines.empty()&&initial_.texts.empty())
                sheet_profile_pending_->start_point_id=
                    zima::document::initialize_attached_sheet_profile(
                        initial_,sheet_profile_pending_->thickness);
            sheet_profile_changed_(*sheet_profile_pending_);
            placement_->initialize_from_references(refs,[](const auto& key) {
                return QString::fromStdString(key);
            });
            lock_sheet_profile_attachment_fields();
            refresh_resolved_placement();notify_preview();return true;
        }
    }
    if(flat_pending_&&index==0) {
        const auto edge=std::ranges::find_if(reference_geometry_.edges,[&](const auto& e) {
            return e.reference.owner_id==reference.owner_id&&e.reference.semantic_key==reference.semantic_key&&e.reference.instance_path==reference.instance_path;
        });
        if(edge!=reference_geometry_.edges.end()&&zima::kernel::sheet_edge_role(*edge)==zima::kernel::SheetEdgeRole::Boundary) {
            const auto refs=zima::document::flat_sheet_references(*edge);
            flat_pending_->direction=zima::document::ExtrusionDirection::Reverse;
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
    reference=prepare_reference_candidate(index,std::move(reference));
    const bool first_plane_reference = index == 0 && reference.supports_offset;
    // Shared normalization keeps later positional planes from replacing
    // FRONT. Preserve explicit direction rows: after an anchor point, the
    // curve in row 1 must orient the frame using its tangent at that point.
    QString error;
    const bool accepted = placement_->set_reference(
        index, std::move(reference), label, &error,
        /*derive_orientation=*/index == 0);
    if (accepted && zima::document::placement_references_use_whole_origin(placement_->combined_references(3))) {
        update_automatic_work_plane(plane_, static_cast<int>(zima::sketcher::SketchPlane::XY));
        notify_preview();
    } else if (accepted && first_plane_reference) {
        update_automatic_work_plane(plane_, static_cast<int>(zima::document::automatic_sketch_work_plane(
            placement_->combined_references(3)).value_or(zima::sketcher::SketchPlane::XY)), label);
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
    lock_sheet_profile_attachment_fields();
}

void SketchPropertiesDialog::set_remaining_rotation_dof(int dof) {
    placement_->set_remaining_rotation_dof(dof);
}

void SketchPropertiesDialog::set_rotation_constraint_state(
        const zima::document::OrientationConstraintState& state) {
    placement_->set_rotation_constraint_state(state);
    lock_bend_attachment_fields();
    lock_sheet_profile_attachment_fields();
}

void SketchPropertiesDialog::set_orientation_base_rotation(
        const zima::kernel::Vec3& rotation, bool constrained) {
    placement_->set_orientation_base_rotation(rotation, constrained);
    lock_bend_attachment_fields();
    lock_sheet_profile_attachment_fields();
}

void SketchPropertiesDialog::set_resolved_rotation(
        const zima::kernel::Vec3& rotation, bool valid) {
    placement_->set_resolved_rotation(rotation, valid);
    lock_bend_attachment_fields();
    lock_sheet_profile_attachment_fields();
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
    if ((key == "sheet_profile_side0_length"||key=="side0_length")&&
            sheet_profile_width_panel_)
        return set_field(sheet_profile_width_panel_->side_value(0));
    if ((key == "sheet_profile_side1_length"||key=="side1_length")&&
            sheet_profile_width_panel_)
        return set_field(sheet_profile_width_panel_->side_value(1));
    if(key=="direction"&&sheet_profile_width_panel_)
        return set_field(sheet_profile_width_panel_->direction_control());
    if(key=="side0_up_to_length_correction"&&sheet_profile_width_panel_)
        return set_field(sheet_profile_width_panel_->side_correction(0));
    if(key=="side1_up_to_length_correction"&&sheet_profile_width_panel_)
        return set_field(sheet_profile_width_panel_->side_correction(1));
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

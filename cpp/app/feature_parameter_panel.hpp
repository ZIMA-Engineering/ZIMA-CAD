#pragma once
#include <QWidget>
#include <zima/ui/properties_subwindow.hpp>
#include <functional>
#include "work_plane_selection.hpp"
#include "feature_type_control.hpp"
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/numeric_value_lock.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <zima/document/feature_parameters.hpp>
#include <zima/document/feature_up_to.hpp>
#include <zima/document/feature_direction.hpp>
#include <zima/document/feature_rotation_span.hpp>
#include "numeric_expression_edit.hpp"
#include "feature_limit_table.hpp"
#include <QScopedValueRollback>
#include "sketch_button_style.hpp"
#include "resource_icon.hpp"
#include "tool_button_style.hpp"
#include "profile_operation_button.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QButtonGroup>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <array>
#include <memory>
#include <vector>
#include <QSignalBlocker>
#include <QStandardItemModel>

namespace zima::app {
// Shared parameter editor. History, placement and Sketch lifecycle belong to the host dialog.
class FeatureParameterPanel final : public QWidget {
public:
    explicit FeatureParameterPanel(QWidget* parent, document::FeatureParameters initial = {})
        : QWidget(parent), initial_(std::move(initial)) {
        content_=new QVBoxLayout(this);content_->setContentsMargins(0,0,0,0);
        type_=feature_type_control(this,initial_.type);
        show_point_=new QCheckBox(tr("Bod"),this);show_point_->setObjectName("featureShowPoint");
        show_text_=new QCheckBox(tr("Text"),this);show_text_->setObjectName("featureShowText");
        show_point_->setChecked(initial_.show_point);show_text_->setChecked(initial_.show_text);
        plane_group_=new QGroupBox(tr("Rovina"),this);
        plane_group_->setObjectName("featurePlaneGroup");
        auto* sketch_form=new QFormLayout(plane_group_);
        auto* sketch_group=new QGroupBox(tr("Skica"),this);sketch_group_=sketch_group;
        sketch_group->setObjectName("featureSketchGroup");
        auto* sketch_row=new QHBoxLayout(sketch_group);
        auto* plane=new QComboBox(this);plane_=plane;plane->setObjectName("featureProfilePlane");
        plane->addItem("XY",0);plane->addItem("XZ",1);plane->addItem("YZ",2);
        install_automatic_work_plane(plane,true);
        sketch_form->addRow(tr("Výchozí rovina"),plane);
        auto* offset=number("featureProfileOffset",false);sketch_form->addRow(tr("Odsazení roviny"),offset);
        offset_=offset;offset->setValue(initial_.profile_plane_offset);
        offset_display_=offset->value();
        auto* sketch=new QPushButton(tr("Skica"),sketch_group);style_sketch_button(sketch);sketch->setMinimumHeight(40);
        sketch_=sketch;sketch->setObjectName("featureSketchButton");
        sketch->setMinimumWidth(100);
        sketch->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);
        sketch->setIcon(resource_icon("sketch"));sketch->setIconSize({20,20});
        sketch_row->addWidget(sketch);
        auto* plane_row=new QHBoxLayout;
        plane_row->addWidget(plane_group_,1);
        plane_row->addWidget(sketch_group);
        content_->addLayout(plane_row);
        auto* result_group=new QGroupBox(tr("Výsledek"),this);result_group_=result_group;
        result_group->setObjectName("featureResultGroup");
        auto* profile=new QFormLayout(result_group);
        auto* result=new QComboBox(this);result->setObjectName("featureResult");
        result->addItems({tr("Těleso"),tr("Plocha"),tr("Tenkostěnný prvek")});
        profile->addRow(tr("Výsledek"),result);
        operations_=new QWidget(this);auto* operation_layout=new QHBoxLayout(operations_);operation_layout->setContentsMargins(0,0,0,0);
        auto* operation_group=new QButtonGroup(operations_);operation_group_=operation_group;operation_group->setExclusive(false);
        for(int operation=0;operation<2;++operation){
            auto* button=new ProfileOperationButton(operation==0,operation==0?tr("Přičíst"):tr("Odečíst"),operations_);
            button->setObjectName(operation==0?"featureAdd":"featureSubtract");button->setCheckable(true);
            operation_group->addButton(button,operation);button->setChecked(operation==0);operation_layout->addWidget(button);
        }
        connect(operation_group,&QButtonGroup::idClicked,this,[this](int id){
            changing_operation_=true;
            last_operation_=id;
            if(operation_group_->button(id)->isChecked()) {
                operation_group_->button(1-id)->setChecked(false);
                if(parameters().sketch_only()) {
                    // Re-enable the same side modes during this edit session.
                    for(int i=0;i<2;++i)sides_[i].mode->setCurrentIndex(saved_modes_[i]);
                }
            } else {
                for(int i=0;i<2;++i)saved_modes_[i]=sides_[i].mode->currentIndex();
                // The persisted no-body mode already uses two inactive sides.
                for(auto& side:sides_)side.mode->setCurrentIndex(0);
            }
            changing_operation_=false;
            refresh_operations();
            normalize_rotations(0);
        });
        profile->addRow(tr("Operace"),operations_);
        auto* thickness=number("featureThickness",false);thickness->setMinimum(.001);thickness->setValue(1);
        thickness_=thickness;thickness->setValue(initial_.thin_thickness);
        thickness_display_=thickness->value();
        profile->addRow(tr("Tloušťka"),thickness);thickness->setEnabled(false);
        result_=result;
        auto* thin_side=new QComboBox(this);thin_side->addItems({tr("Dovnitř"),tr("Ven"),tr("Symetricky")});
        thin_side_=thin_side;thin_side->setObjectName("featureThinSide");
        thin_side->setCurrentIndex(static_cast<int>(initial_.thin_mode));
        thin_side->setEnabled(false);profile->addRow(tr("Směr tloušťky"),thin_side);
        connect(result,&QComboBox::currentIndexChanged,this,[=,this](int i){
            thickness->setEnabled(i==2);thin_side->setEnabled(i==2);
            refresh_operations();
        });
        content_->addWidget(result_group);
        auto* symmetric=new QCheckBox(tr("Symetricky"),this);symmetric_control_=symmetric;symmetric->setObjectName("featureSymmetric");
        options_row_=new QWidget(this);
        auto* options=new QHBoxLayout(options_row_);
        options->setContentsMargins(0,0,0,0);
        options->addWidget(symmetric);options->addSpacing(16);
        origin_axis_=new QCheckBox(tr("Osa počátku"),this);origin_axis_->setObjectName("featureOriginAxis");
        centroid_axis_=new QCheckBox(tr("Osa těžiště"),this);centroid_axis_->setObjectName("featureCentroidAxis");
        origin_axis_->setChecked(initial_.origin_centerline);centroid_axis_->setChecked(initial_.centroid_centerline);
        options->addWidget(origin_axis_);options->addWidget(centroid_axis_);
        options->addWidget(new QLabel(tr("Směr"),this));
        direction_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Scalar,this);
        direction_->setObjectName("featureDirection");direction_->setRange(-1,1);
        direction_->setSingleStep(2);direction_->set_display_decimals(0);
        direction_->setValue(document::feature_direction(initial_));options->addWidget(direction_);
        connect(direction_,&QDoubleSpinBox::valueChanged,this,[this](double value){
            if(!document::set_feature_direction(initial_,value)) {
                const QSignalBlocker blocker(direction_);direction_->setValue(document::feature_direction(initial_));
            }
        });
        options->addStretch();
        swap_=new QPushButton(tr("Prohodit strany"),this);
        swap_->setObjectName("featureSwapSides");options->addWidget(swap_);
        connect(swap_,&QPushButton::clicked,this,[this]{if(swap_requested_)swap_requested_();});
        content_->addWidget(options_row_);
        draft_notice_=new QLabel(tr("Exact preview"),this);
        draft_notice_->setObjectName("featureUpToDraftNotice");draft_notice_->setWordWrap(true);draft_notice_->hide();
        content_->addWidget(draft_notice_);
        auto* sides=new QHBoxLayout;
        for(int i=0;i<2;++i) {
            auto& s=sides_[i];s.box=new QGroupBox(i==0?tr("Strana 1"):tr("Strana 2"),this);
            s.pending=initial_.sides[i];
            auto* rows=new QFormLayout(s.box);s.rows=rows;
            s.mode=new QComboBox(s.box);s.mode->setObjectName(QString("featureSideMode%1").arg(i));
            s.mode->addItems({tr("Bez operace"),tr("Vytažení"),tr("Rotace")});rows->addRow(tr("Typ"),s.mode);
            if(i==0)s.mode->addItem(tr("Twist"));
            s.draft=number(i==0?"featureSideDraft0":"featureSideDraft1",true);
            s.draft->setRange(-89.999,89.999);s.draft->setValue(s.pending.draft_angle_degrees);
            s.displayed_draft=s.draft->value();rows->addRow(tr("Úhel úkosu"),s.draft);
            s.end=new QComboBox(s.box);s.end->setObjectName(QString("featureSideEnd%1").arg(i));rows->addRow(tr("Ukončení"),s.end);
            s.value=number(i==0?"featureSideValue0":"featureSideValue1",false,true);s.value->setMinimum(.001);s.value->setValue(50);
            s.label=new QLabel(tr("Délka"),s.box);rows->addRow(s.label,s.value);
            auto* target_row=new QWidget(s.box);s.target_row=target_row;s.target_layout=new QHBoxLayout(target_row);s.target_layout->setContentsMargins(0,0,0,0);
            s.target=new QLineEdit(target_row);s.target->setPlaceholderText(tr("Reference"));s.target->setReadOnly(true);
            s.target_layout->addWidget(s.target,1);rows->addRow(tr("Až k"),target_row);
            s.limits=new FeatureLimitTable(s.pending,s.box);s.limits->setObjectName(QString("featureSideLimits%1").arg(i));
            s.limits->table()->setObjectName(QString("featureSideTargets%1").arg(i));rows->addRow(s.limits);
            s.limits->on_change([this]{if(change_requested_)change_requested_();});
            s.correction=number(i==0?"featureSideCorrection0":"featureSideCorrection1",false,true);
            s.authored_correction=s.pending.up_to.length_correction;s.correction->setValue(s.authored_correction);
            s.displayed_correction=s.correction->value();
            rows->addRow(tr("Korekce"),s.correction);
            s.twist_angle=number(i==0?"featureTwistAngle0":"featureTwistAngle1",true);
            s.twist_angle->setRange(.001,36000);s.twist_angle->setValue(s.pending.angle_degrees);
            s.displayed_twist_angle=s.twist_angle->value();rows->addRow(tr("Úhel kroucení"),s.twist_angle);
            s.twist_direction=new QComboBox(s.box);s.twist_direction->setObjectName(QString("featureTwistDirection%1").arg(i));
            s.twist_direction->addItems({tr("Doprava"),tr("Doleva")});s.twist_direction->setCurrentIndex(s.pending.twist_reverse?1:0);
            rows->addRow(tr("Směr kroucení"),s.twist_direction);
            s.twist_smooth=new QCheckBox(tr("Smooth twist transitions"),s.box);
            s.twist_smooth->setObjectName(QString("featureTwistSmooth%1").arg(i));s.twist_smooth->setChecked(s.pending.twist_smooth);
            rows->addRow(s.twist_smooth);
            connect(s.mode,&QComboBox::currentIndexChanged,this,[this,i]{refresh_side(i);});
            connect(s.end,&QComboBox::currentIndexChanged,this,[this,i]{auto& a=sides_[i];const int mode=a.displayed_axis?1:a.mode->currentIndex();a.value->setEnabled(mode!=0&&a.end->currentIndex()==0);a.target->setEnabled(mode!=0&&a.end->currentIndex()==(mode==2?2:1));
                refresh_limit_controls(i);
                if(!refreshing_side_&&a.displayed_axis){a.pending=capture_side(a,a.previous_mode);a.displayed_end=a.end->currentIndex();}});
            sides->addWidget(s.box);refresh_side(i);
        }
        content_->addLayout(sides,1);
        for(int i=0;i<2;++i)sides_[i].mode->setCurrentIndex(static_cast<int>(initial_.sides[i].operation));
        connect(symmetric,&QCheckBox::toggled,this,[this](bool on){
            sides_[1].box->setEnabled(!on);sides_[1].box->setVisible(!on);
            sides_[0].box->setTitle(on?tr("Obě strany"):tr("Strana 1"));
            symmetric_=on;refresh_operations();refresh_type();
            swap_->setEnabled(!on&&(!locks_||!locks_->contains("direction")));
        });
        symmetric->setChecked(initial_.symmetric);
        result->setCurrentIndex(initial_.result_type==document::ProfileResultType::Surface?1:
            initial_.result_type==document::ProfileResultType::Thin?2:0);
        connect(type_,&QComboBox::currentIndexChanged,this,[this]{
            changing_operation_=true;
            for(int i=0;i<2;++i)refresh_side(i);
            refresh_type();changing_operation_=false;
        });
        refresh_type();
        for(int i=0;i<2;++i) {
            connect(sides_[i].value,&QDoubleSpinBox::valueChanged,this,[this,i](double value){
                auto& side=sides_[i];
                if(refreshing_side_)return;
                if(value>=0){
                    if(value<.001){const QSignalBlocker blocker(side.value);side.value->setValue(.001);}
                    side.last_value=side.value->value();return;
                }
                const QSignalBlocker blocker(side.value);
                if(value<0&&
                    (!locks_||!locks_->contains("direction"))) {
                    set_direction(-document::feature_direction(initial_));
                    side.value->setValue(std::max(.001,-value));side.last_value=side.value->value();
                } else side.value->setValue(side.last_value);
            });
            connect(sides_[i].mode,&QComboBox::currentIndexChanged,this,[this,i]{normalize_rotations(i);});
            connect(sides_[i].end,&QComboBox::currentIndexChanged,this,[this,i]{normalize_rotations(i);});
            connect(sides_[i].value,&QDoubleSpinBox::valueChanged,this,[this,i]{normalize_rotations(i);});
        }
        connect(symmetric,&QCheckBox::toggled,this,[this]{normalize_rotations(0);});
        connect(type_,&QComboBox::currentIndexChanged,this,[this]{normalize_rotations(0);});
        update_draft_notice();
    }
    [[nodiscard]] document::FeatureParameters parameters() const {
        auto value=initial_;value.type=selected_type();
        value.profile_plane_offset=offset_->value()==offset_display_?initial_.profile_plane_offset:offset_->value();
        value.thin_thickness=thickness_->value()==thickness_display_?initial_.thin_thickness:thickness_->value();
        value.thin_mode=static_cast<document::ThinMode>(thin_side_->currentIndex());
        value.result_type=result_->currentIndex()==1?document::ProfileResultType::Surface:
            result_->currentIndex()==2?document::ProfileResultType::Thin:document::ProfileResultType::Solid;
        value.symmetric=symmetric_;value.origin_centerline=origin_axis_->isChecked();
        value.show_point=show_point_->isChecked();value.show_text=show_text_->isChecked();
        value.centroid_centerline=centroid_axis_->isChecked();
        for(int i=0;i<2;++i)value.sides[i]=capture_side(sides_[i],sides_[i].mode->currentIndex());
        return value;
    }
    QComboBox* type_control() const { return type_; }
    QWidget* type_row() {
        auto* row=new QWidget(this);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
        type_->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
        layout->addWidget(type_);layout->addWidget(show_point_);layout->addWidget(show_text_);layout->addStretch();
        return row;
    }
    QComboBox* plane_control() const { return plane_; }
    QDoubleSpinBox* offset_control() const { return offset_; }
    QDoubleSpinBox* thickness_control() const { return thickness_; }
    QPushButton* sketch_button() const { return sketch_; }
    void on_swap(std::function<void()> callback) { swap_requested_=std::move(callback); }
    void swap_sides() {
        set_direction(-document::feature_direction(initial_));
    }
    bool set_direction(double value) {
        if(locks_&&locks_->contains("direction")&&value!=document::feature_direction(initial_))return false;
        if(!document::set_feature_direction(initial_,value))return false;
        const QSignalBlocker blocker(direction_);direction_->setValue(value);return true;
    }
    bool set_signed_length(std::size_t side,double value,std::optional<bool> baseline={}) {
        auto& row=sides_.at(side);
        if((!row.displayed_axis&&row.mode->currentIndex()==2)||!row.value->isEnabled()||row.value->isReadOnly()||!std::isfinite(value))return false;
        const double magnitude=std::clamp(std::abs(value),.001,1000000.);
        const bool reversed=baseline?(*baseline!=(value<0)):(initial_.reversed_sides!=(value<0));
        const bool changed=reversed!=initial_.reversed_sides||row.value->value()!=magnitude;
        if(!set_direction(reversed?-1.:1.))return false;
        {const QSignalBlocker blocker(row.value);row.value->setValue(magnitude);row.last_value=row.value->value();}
        if(changed&&change_requested_)change_requested_();return true;
    }
    bool set_signed_angle(std::size_t side,double value,std::optional<bool> baseline={}) {
        auto& row=sides_.at(side);
        if(row.mode->currentIndex()!=2||row.displayed_axis||!row.value->isEnabled()||
            row.value->isReadOnly()||!std::isfinite(value))return false;
        const double limit=baseline?document::feature_rotation_drag_limit(parameters(),side):360.;
        if(limit<.001)return false;
        const double magnitude=std::clamp(std::abs(value),.001,limit);
        const bool reversed=baseline?(*baseline!=(value<0)):(initial_.reversed_sides!=(value<0));
        const bool changed=reversed!=initial_.reversed_sides||row.value->value()!=magnitude;
        if(!set_direction(reversed?-1.:1.))return false;
        {const QSignalBlocker blocker(row.value);row.value->setValue(magnitude);row.last_value=row.value->value();}
        normalize_rotations(side);
        if(changed&&change_requested_)change_requested_();return true;
    }
    QDoubleSpinBox* direction_control() const { return direction_; }
    void finish_side_expressions() {
        if(selected_type()!=document::FeatureType::Modeling&&selected_type()!=document::FeatureType::Axis)return;
        std::vector<UnitExpressionDoubleSpinBox*> fields;
        for(auto& row:sides_)if(row.value->isVisible()&&row.value->isEnabled()&&!row.value->isReadOnly()) {
            auto* field=static_cast<UnitExpressionDoubleSpinBox*>(row.value);
            static_cast<void>(field->expression_value());fields.push_back(field);
        }
        for(auto& row:sides_)if(row.correction->isVisible()&&row.correction->isEnabled()&&!row.correction->isReadOnly()) {
            auto* field=static_cast<UnitExpressionDoubleSpinBox*>(row.correction);
            static_cast<void>(field->expression_value());fields.push_back(field);
        }
        // Validate every field before interpreting any. Middle-button OK can
        // arrive while the text editor retains focus, without a focus-out event.
        for(auto* field:fields)field->interpretText();
    }
    QLineEdit* target_control(std::size_t side) const { return sides_.at(side).target; }
    QHBoxLayout* target_layout(std::size_t side) const { return sides_.at(side).target_layout; }
    FeatureLimitTable* limit_table(std::size_t side) const {return sides_.at(side).limits;}
    std::vector<document::ExtrusionParameters::EndTarget>& targets(std::size_t side){return sides_.at(side).pending.targets;}
    const std::vector<document::ExtrusionParameters::EndTarget>& targets(std::size_t side)const{return sides_.at(side).pending.targets;}
    void set_single_target(std::size_t side,std::optional<document::ExtrusionParameters::EndTarget> target) {
        auto& value=sides_.at(side).pending;value.targets.clear();
        if(target)value.targets.push_back(std::move(*target));
        document::prune_feature_up_to_patch_choices(value);sides_.at(side).limits->refresh();
    }
    QDoubleSpinBox* side_correction(std::size_t side) const {return sides_.at(side).correction;}
    bool modeling_limits() const {return selected_type()==document::FeatureType::Modeling;}
    void on_preview_notice_changed(std::function<void()> callback){notice_changed_=std::move(callback);}
    void update_draft_notice() {
        const bool visible=document::feature_up_to_requires_exact_preview(parameters());
        if(draft_notice_->isHidden()==!visible)return;
        draft_notice_->setVisible(visible);if(notice_changed_)notice_changed_();
    }
    bool active_limit_tables() const {
        return modeling_limits()&&std::ranges::any_of(sides_,[](const auto& side){return !side.box->isHidden()&&!side.limits->isHidden();});
    }
    QDoubleSpinBox* side_value(std::size_t side) const { return sides_.at(side).value; }
    QDoubleSpinBox* side_angle(std::size_t side) const {
        const auto& value=sides_.at(side);return value.mode->currentIndex()==3?value.twist_angle:value.value;
    }
    QDoubleSpinBox* side_draft(std::size_t side) const { return sides_.at(side).draft; }
    QGroupBox* side_group(std::size_t side) const { return sides_.at(side).box; }
    bool subtract() const { return result_->currentIndex()!=1 && operation_group_->checkedId()==1; }
    void set_subtract(bool value) {
        last_operation_=value?1:0;
        refresh_operations();
    }
    void set_assembly_context() {
        set_subtract(true);
        operation_group_->button(0)->setEnabled(false);
        if(auto* model=qobject_cast<QStandardItemModel*>(result_->model()))model->item(1)->setEnabled(false);
        if(result_->currentIndex()==1)result_->setCurrentIndex(0);
    }
    void bind_side_locks(std::set<std::string>& locks,std::function<void()> changed) {
        locks_=&locks;locks_changed_=std::move(changed);
        ui::bind_numeric_value_lock(direction_,"direction",locks,[this]{
            swap_->setEnabled(!symmetric_&&!locks_->contains("direction")&&sides_[0].mode->currentIndex()!=3);
            locks_changed_();
        });
        swap_->setEnabled(!symmetric_&&!locks_->contains("direction")&&sides_[0].mode->currentIndex()!=3);
        for(int i=0;i<2;++i)refresh_side_lock(i);
    }
    void on_change(std::function<void()> changed) {
        const auto notify=[this,changed]{if(!changing_operation_&&!refreshing_side_&&!normalizing_rotations_)changed();};
        change_requested_=notify;
        for(auto* box:findChildren<QComboBox*>())if(!box->property("featureLimitChoice").toBool())connect(box,&QComboBox::currentIndexChanged,this,notify);
        for(auto* box:findChildren<QDoubleSpinBox*>())connect(box,&QDoubleSpinBox::valueChanged,this,notify);
        for(auto* box:findChildren<QCheckBox*>())connect(box,&QCheckBox::toggled,this,notify);
        connect(operation_group_,&QButtonGroup::idClicked,this,notify);
        if(type_->parentWidget()!=this)connect(type_,&QComboBox::currentIndexChanged,this,notify);
        if(!isAncestorOf(show_point_))connect(show_point_,&QCheckBox::toggled,this,notify);
        if(!isAncestorOf(show_text_))connect(show_text_,&QCheckBox::toggled,this,notify);
    }
protected:
    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if(event->type()==QEvent::LanguageChange&&draft_notice_) {
            draft_notice_->setText(tr("Exact preview"));
            for(auto& side:sides_)if(side.rows&&side.correction)
                if(auto* label=qobject_cast<QLabel*>(side.rows->labelForField(side.correction)))label->setText(tr("Korekce"));
            if(notice_changed_)notice_changed_();
        }
    }
private:
    struct Side {
        QGroupBox* box{};QComboBox* mode{};QComboBox* end{};ui::UnitDoubleSpinBox* value{};ui::UnitDoubleSpinBox* draft{};QLabel* label{};QLineEdit* target{};QHBoxLayout* target_layout{};
        QFormLayout* rows{};QWidget* target_row{};bool displayed_axis{};
        FeatureLimitTable* limits{};ui::UnitDoubleSpinBox* correction{};
        bool correction_angular{};double displayed_correction{},authored_correction{};
        document::FeatureSideParameters pending;
        int previous_mode{},displayed_end{};
        double displayed_value{},authored_value{},displayed_draft{},last_value{};
        ui::UnitDoubleSpinBox* twist_angle{};QComboBox* twist_direction{};QCheckBox* twist_smooth{};
        double displayed_twist_angle{};
    };
    QVBoxLayout* content_{};
    QComboBox* type_{};
    QCheckBox *show_point_{},*show_text_{};
    QGroupBox *plane_group_{},*sketch_group_{},*result_group_{};
    QCheckBox* symmetric_control_{};
    QWidget* options_row_{};
    QLabel* draft_notice_{};std::function<void()> notice_changed_;
    QComboBox* plane_{};
    ui::UnitDoubleSpinBox* direction_{};
    QPushButton* sketch_{};
    QPushButton* swap_{};
    std::function<void()> swap_requested_;
    std::function<void()> change_requested_;
    QButtonGroup* operation_group_{};
    int last_operation_{};
    bool changing_operation_{};
    bool refreshing_side_{},normalizing_rotations_{};
    std::array<int,2> saved_modes_{{1,0}};
    document::FeatureParameters initial_;
    std::array<Side,2> sides_{};
    ui::UnitDoubleSpinBox *offset_{}, *thickness_{};
    QComboBox* thin_side_{};
    QCheckBox *origin_axis_{}, *centroid_axis_{};
    QComboBox* result_{};
    QWidget* operations_{};
    bool symmetric_{};
    double offset_display_{},thickness_display_{};
    std::set<std::string>* locks_{};
    std::function<void()> locks_changed_;
    static document::FeatureSideParameters capture_side(const Side& side,int mode) {
        auto value=side.pending;value.operation=static_cast<document::FeatureSideOperation>(mode);
        if(side.draft&&side.draft->value()!=side.displayed_draft)value.draft_angle_degrees=side.draft->value();
        if(side.correction&&!side.displayed_axis) {
            const double correction=side.correction->value()==side.displayed_correction?side.authored_correction:side.correction->value();
            if(side.correction_angular)value.up_to.angle_correction_degrees=correction;else value.up_to.length_correction=correction;
        }
        const double number=side.value->value()==side.displayed_value?side.authored_value:side.value->value();
        if(side.displayed_axis) {
            value.length=number;
            value.axis_enabled=side.end->currentIndex()!=2;
            if(value.axis_enabled&&side.end->currentIndex()!=side.displayed_end)
                value.extrusion_extent=side.end->currentIndex()==1?document::EndCondition::UpTo:document::EndCondition::Length;
        }
        else {
            if(mode==1){value.length=number;value.extrusion_extent=static_cast<document::EndCondition>(side.end->currentIndex());}
            if(mode==2){value.angle_degrees=number;value.rotation_extent=static_cast<document::FeatureRotationExtent>(side.end->currentIndex());}
            if(mode==3) {
                value.length=number;value.extrusion_extent=document::EndCondition::Length;
                value.draft_angle_degrees=0;value.targets.clear();
                document::prune_feature_up_to_patch_choices(value);
                if(side.twist_angle->value()!=side.displayed_twist_angle)value.angle_degrees=side.twist_angle->value();
                value.twist_reverse=side.twist_direction->currentIndex()==1;value.twist_smooth=side.twist_smooth->isChecked();
            }
        }
        return value;
    }
    document::FeatureType selected_type() const {
        return static_cast<document::FeatureType>(type_->currentIndex());
    }
    void refresh_type() {
        const auto type=selected_type();
        const bool modeling=type==document::FeatureType::Modeling;
        const bool lengths=modeling||type==document::FeatureType::Axis;
        plane_group_->setVisible(type!=document::FeatureType::Point&&type!=document::FeatureType::Axis);
        sketch_group_->setVisible(modeling||type==document::FeatureType::Sketch);
        result_group_->setVisible(modeling);
        options_row_->setVisible(lengths);
        symmetric_control_->setVisible(lengths);swap_->setVisible(lengths);
        origin_axis_->setVisible(modeling);centroid_axis_->setVisible(modeling);
        for(int i=0;i<2;++i) {
            auto& s=sides_[i];if(!s.box)continue;
            s.box->setVisible(lengths&&(i==0||!symmetric_));
            s.rows->setRowVisible(s.mode,modeling);
            s.rows->setRowVisible(s.draft,modeling&&s.mode->currentIndex()==1);
            s.rows->setRowVisible(s.end,lengths&&(type==document::FeatureType::Axis||s.mode->currentIndex()!=3));
            s.rows->setRowVisible(s.target_row,lengths&&(type==document::FeatureType::Axis||s.mode->currentIndex()!=3));
            refresh_limit_controls(i);
        }
    }
    void refresh_operations() {
        const bool active=(sides_[0].mode&&sides_[0].mode->currentIndex()!=0)||
            (!symmetric_&&sides_[1].mode&&sides_[1].mode->currentIndex()!=0);
        operations_->setEnabled(result_->currentIndex()!=1);
        if(changing_operation_)return;
        for(int i=0;i<2;++i)
            operation_group_->button(i)->setChecked(active&&i==last_operation_);
    }
    ui::UnitDoubleSpinBox* number(const char* name,bool angle,bool arithmetic=false) {
        const auto quantity=angle?ui::InputQuantity::Angle:ui::InputQuantity::Length;
        ui::UnitDoubleSpinBox* value=arithmetic?new UnitExpressionDoubleSpinBox(quantity,this):new ui::UnitDoubleSpinBox(quantity,this);
        value->setObjectName(name);
        value->setRange(-1000000,1000000);return value;
    }
    void refresh_side(int index) {
        QScopedValueRollback refreshing(refreshing_side_,true);
        auto& s=sides_[index];const int saved_mode=s.mode->currentIndex();
        s.pending=capture_side(s,s.previous_mode);s.previous_mode=saved_mode;
        {const QSignalBlocker blocked(s.draft);s.draft->setValue(s.pending.draft_angle_degrees);s.displayed_draft=s.draft->value();}
        s.displayed_axis=selected_type()==document::FeatureType::Axis;
        const int mode=s.displayed_axis?1:saved_mode;
        s.draft->setEnabled(selected_type()==document::FeatureType::Modeling&&mode==1);
        s.rows->setRowVisible(s.draft,selected_type()==document::FeatureType::Modeling&&mode==1);
        s.end->clear();
        if(mode==2)s.end->addItems({tr("Úhel"),tr("Plná rotace"),tr("Až k")});
        else s.end->addItems({tr("Délka"),tr("Až k"),s.displayed_axis?tr("Nevytvářet"):tr("Skrz vše")});
        s.end->setCurrentIndex(s.displayed_axis?(!s.pending.axis_enabled?2:s.pending.extrusion_extent==document::EndCondition::UpTo?1:0):mode==2?static_cast<int>(s.pending.rotation_extent):static_cast<int>(s.pending.extrusion_extent));
        s.displayed_end=s.end->currentIndex();
        s.end->setEnabled(mode!=0);s.value->setEnabled(mode!=0&&s.end->currentIndex()==0);
        refresh_operations();
        s.label->setText(mode==2?tr("Úhel"):tr("Délka"));
        s.value->set_quantity(mode==2?ui::InputQuantity::Angle:ui::InputQuantity::Length);
        s.value->setMinimum(mode==2?-360:-1000000);
        s.value->setMaximum(mode==2?360:1000000);
        s.authored_value=mode==2?s.pending.angle_degrees:s.pending.length;
        s.value->setValue(s.authored_value);
        s.displayed_value=s.value->value();
        s.last_value=s.value->value();
        s.target->setEnabled(mode!=0 && s.end->currentIndex()==(mode==2?2:1));
        const bool twisting=mode==3;
        if(twisting) {
            const QSignalBlocker angle_block(s.twist_angle),direction_block(s.twist_direction),smooth_block(s.twist_smooth);
            s.twist_angle->setValue(s.pending.angle_degrees);s.displayed_twist_angle=s.twist_angle->value();
            s.twist_direction->setCurrentIndex(s.pending.twist_reverse?1:0);s.twist_smooth->setChecked(s.pending.twist_smooth);
        }
        s.rows->setRowVisible(s.twist_angle,twisting);s.rows->setRowVisible(s.twist_direction,twisting);
        s.rows->setRowVisible(s.twist_smooth,twisting);s.rows->setRowVisible(s.end,!twisting);
        s.rows->setRowVisible(s.target_row,!twisting);
        refresh_limit_controls(index);s.limits->refresh();
        if(twisting) {
            s.end->setCurrentIndex(0);
            sides_[1].mode->setCurrentIndex(0);symmetric_control_->setChecked(false);
            result_->setCurrentIndex(0);
        }
        const bool any_twist=sides_[0].mode&&sides_[0].mode->currentIndex()==3;
        if(sides_[1].box)sides_[1].box->setEnabled(!any_twist&&!symmetric_);
        symmetric_control_->setEnabled(!any_twist);swap_->setEnabled(!any_twist&&!symmetric_&&(!locks_||!locks_->contains("direction")));
        result_->setEnabled(!any_twist);
        refresh_side_lock(index);
    }
    void refresh_side_lock(int index) {
        if(locks_)ui::bind_numeric_value_lock(sides_[index].draft,
            "side"+std::to_string(index)+"_draft_angle",*locks_,locks_changed_);
        if(locks_)ui::bind_numeric_value_lock(sides_[index].value,
            "side"+std::to_string(index)+(!sides_[index].displayed_axis&&sides_[index].mode->currentIndex()==2?"_angle":"_length"),*locks_,locks_changed_);
        if(locks_&&sides_[index].mode->currentIndex()==3)ui::bind_numeric_value_lock(sides_[index].twist_angle,
            "side"+std::to_string(index)+"_angle",*locks_,locks_changed_);
        if(locks_)ui::bind_numeric_value_lock(sides_[index].correction,
            "side"+std::to_string(index)+(sides_[index].correction_angular?"_up_to_angle_correction":"_up_to_length_correction"),*locks_,locks_changed_);
    }
    void refresh_limit_controls(int index) {
        auto& side=sides_[index];if(!side.limits||!side.correction)return;
        const bool modeling=selected_type()==document::FeatureType::Modeling;
        const int mode=side.mode->currentIndex();const bool up_to=mode==1?side.end->currentIndex()==1:mode==2&&side.end->currentIndex()==2;
        const bool visible=modeling&&up_to&&(!symmetric_||index==0);
        side.rows->setRowVisible(side.target_row,selected_type()==document::FeatureType::Axis);
        side.rows->setRowVisible(side.limits,visible);side.rows->setRowVisible(side.correction,visible);
        side.limits->setEnabled(visible);side.correction->setEnabled(visible);if(!visible)side.limits->finish_entry(true);
        side.box->setSizePolicy(QSizePolicy::Preferred,visible?QSizePolicy::Expanding:QSizePolicy::Preferred);
        const double value=side.correction->value()==side.displayed_correction?side.authored_correction:side.correction->value();
        if(side.correction_angular)side.pending.up_to.angle_correction_degrees=value;else side.pending.up_to.length_correction=value;
        side.correction_angular=mode==2;
        const QSignalBlocker blocked(side.correction);side.correction->set_quantity(mode==2?ui::InputQuantity::Angle:ui::InputQuantity::Length);
        side.correction->setRange(mode==2?-359.999999:-1000000,mode==2?359.999999:1000000);
        side.authored_correction=mode==2?side.pending.up_to.angle_correction_degrees:side.pending.up_to.length_correction;
        side.correction->setValue(side.authored_correction);side.displayed_correction=side.correction->value();
        if(!refreshing_side_)refresh_side_lock(index);
    }
    void normalize_rotations(std::size_t edited) {
        if(refreshing_side_||normalizing_rotations_||changing_operation_)return;
        auto next=parameters();document::normalize_feature_rotations(next,edited);
        if(next==parameters())return;
        QScopedValueRollback normalizing(normalizing_rotations_,true);
        QScopedValueRollback changing(changing_operation_,true);
        std::vector<std::unique_ptr<QSignalBlocker>> blocked;
        for(auto& side:sides_) {
            blocked.push_back(std::make_unique<QSignalBlocker>(side.mode));
            blocked.push_back(std::make_unique<QSignalBlocker>(side.end));
            blocked.push_back(std::make_unique<QSignalBlocker>(side.value));
        }
        for(int i=0;i<2;++i) {
            auto& side=sides_[i];side.pending=next.sides[i];
            side.previous_mode=0;side.displayed_axis=false;
            side.mode->setCurrentIndex(static_cast<int>(side.pending.operation));refresh_side(i);
        }
        // Keep the ordinary symmetry handler responsible for its layout.
        symmetric_control_->setChecked(next.symmetric);
    }
};
}

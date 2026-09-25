#pragma once
#include <QWidget>
#include <zima/ui/properties_subwindow.hpp>
#include <functional>
#include "work_plane_selection.hpp"
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/numeric_value_lock.hpp>
#include <zima/document/feature_parameters.hpp>
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

namespace zima::app {
// Shared parameter editor. History, placement and Sketch lifecycle belong to the host dialog.
class FeatureParameterPanel final : public QWidget {
public:
    explicit FeatureParameterPanel(QWidget* parent, document::FeatureParameters initial = {})
        : QWidget(parent), initial_(std::move(initial)) {
        content_=new QVBoxLayout(this);content_->setContentsMargins(0,0,0,0);
        auto* sketch_group=new QGroupBox(tr("Skica"),this);
        sketch_group->setObjectName("featureSketchGroup");
        auto* sketch_row=new QHBoxLayout(sketch_group);
        auto* sketch_form=new QFormLayout;
        sketch_row->addLayout(sketch_form,1);
        auto* plane=new QComboBox(this);plane_=plane;
        plane->addItem("XY",0);plane->addItem("XZ",1);plane->addItem("YZ",2);
        install_automatic_work_plane(plane,true);
        sketch_form->addRow(tr("Výchozí rovina"),plane);
        auto* offset=number("featureProfileOffset",false);sketch_form->addRow(tr("Odsazení roviny"),offset);
        offset_=offset;offset->setValue(initial_.profile_plane_offset);
        offset_display_=offset->value();
        auto* sketch=new QPushButton(tr("Skica"),sketch_group);style_sketch_button(sketch);sketch->setMinimumHeight(40);
        sketch_=sketch;sketch->setObjectName("featureSketchButton");
        sketch->setFixedWidth(220);
        sketch->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);
        sketch->setIcon(resource_icon("sketch"));sketch->setIconSize({20,20});
        sketch_row->addWidget(sketch);
        content_->addWidget(sketch_group);
        auto* result_group=new QGroupBox(tr("Výsledek"),this);
        result_group->setObjectName("featureResultGroup");
        auto* profile=new QFormLayout(result_group);
        auto* result=new QComboBox(this);result->setObjectName("featureResult");
        result->addItems({tr("Těleso"),tr("Plocha"),tr("Tenkostěnný prvek")});
        profile->addRow(tr("Výsledek"),result);
        operations_=new QWidget(this);auto* operation_layout=new QHBoxLayout(operations_);operation_layout->setContentsMargins(0,0,0,0);
        auto* operation_group=new QButtonGroup(operations_);operation_group_=operation_group;operation_group->setExclusive(true);
        for(int operation=0;operation<2;++operation){
            auto* button=new ProfileOperationButton(operation==0,operation==0?tr("Přičíst"):tr("Odečíst"),operations_);
            button->setObjectName(operation==0?"featureAdd":"featureSubtract");button->setCheckable(true);
            auto style=command_button_style();style.replace("QToolButton","QPushButton");button->setStyleSheet(style);
            operation_group->addButton(button,operation);button->setChecked(operation==0);operation_layout->addWidget(button);
        }
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
        auto* symmetric=new QCheckBox(tr("Symetricky"),this);symmetric->setObjectName("featureSymmetric");
        auto* options=new QHBoxLayout;
        options->addWidget(symmetric);options->addSpacing(16);
        origin_axis_=new QCheckBox(tr("Osa počátku"),this);origin_axis_->setObjectName("featureOriginAxis");
        centroid_axis_=new QCheckBox(tr("Osa těžiště"),this);centroid_axis_->setObjectName("featureCentroidAxis");
        origin_axis_->setChecked(initial_.origin_centerline);centroid_axis_->setChecked(initial_.centroid_centerline);
        options->addWidget(origin_axis_);options->addWidget(centroid_axis_);
        options->addStretch();
        swap_=new QPushButton(tr("Prohodit strany"),this);
        swap_->setObjectName("featureSwapSides");options->addWidget(swap_);
        connect(swap_,&QPushButton::clicked,this,[this]{if(swap_requested_)swap_requested_();});
        content_->addLayout(options);
        auto* sides=new QHBoxLayout;
        for(int i=0;i<2;++i) {
            auto& s=sides_[i];s.box=new QGroupBox(i==0?tr("Strana 1"):tr("Strana 2"),this);
            s.pending=initial_.sides[i];
            auto* rows=new QFormLayout(s.box);
            s.mode=new QComboBox(s.box);s.mode->setObjectName(QString("featureSideMode%1").arg(i));
            s.mode->addItems({tr("Bez operace"),tr("Vytažení"),tr("Rotace")});rows->addRow(tr("Typ"),s.mode);
            s.end=new QComboBox(s.box);s.end->setObjectName(QString("featureSideEnd%1").arg(i));rows->addRow(tr("Ukončení"),s.end);
            s.value=number(i==0?"featureSideValue0":"featureSideValue1",false);s.value->setMinimum(.001);s.value->setValue(50);
            s.label=new QLabel(tr("Délka"),s.box);rows->addRow(s.label,s.value);
            auto* target_row=new QWidget(s.box);s.target_layout=new QHBoxLayout(target_row);s.target_layout->setContentsMargins(0,0,0,0);
            s.target=new QLineEdit(target_row);s.target->setPlaceholderText(tr("Reference"));s.target->setReadOnly(true);
            s.target_layout->addWidget(s.target,1);rows->addRow(tr("Až k"),target_row);
            connect(s.mode,&QComboBox::currentIndexChanged,this,[this,i]{refresh_side(i);});
            connect(s.end,&QComboBox::currentIndexChanged,this,[this,i]{auto& a=sides_[i];a.value->setEnabled(a.mode->currentIndex()!=0&&a.end->currentIndex()==0);a.target->setEnabled(a.mode->currentIndex()!=0&&a.end->currentIndex()==(a.mode->currentIndex()==2?2:1));});
            sides->addWidget(s.box);refresh_side(i);
        }
        content_->addLayout(sides);
        for(int i=0;i<2;++i)sides_[i].mode->setCurrentIndex(static_cast<int>(initial_.sides[i].operation));
        connect(symmetric,&QCheckBox::toggled,this,[this](bool on){
            sides_[1].box->setEnabled(!on);sides_[1].box->setVisible(!on);
            sides_[0].box->setTitle(on?tr("Obě strany"):tr("Strana 1"));
            symmetric_=on;refresh_operations();
            swap_->setEnabled(!on);
        });
        symmetric->setChecked(initial_.symmetric);
        result->setCurrentIndex(initial_.result_type==document::ProfileResultType::Surface?1:
            initial_.result_type==document::ProfileResultType::Thin?2:0);
    }
    [[nodiscard]] document::FeatureParameters parameters() const {
        auto value=initial_;
        value.profile_plane_offset=offset_->value()==offset_display_?initial_.profile_plane_offset:offset_->value();
        value.thin_thickness=thickness_->value()==thickness_display_?initial_.thin_thickness:thickness_->value();
        value.thin_mode=static_cast<document::ThinMode>(thin_side_->currentIndex());
        value.result_type=result_->currentIndex()==1?document::ProfileResultType::Surface:
            result_->currentIndex()==2?document::ProfileResultType::Thin:document::ProfileResultType::Solid;
        value.symmetric=symmetric_;value.origin_centerline=origin_axis_->isChecked();
        value.centroid_centerline=centroid_axis_->isChecked();
        for(int i=0;i<2;++i)value.sides[i]=capture_side(sides_[i],sides_[i].mode->currentIndex());
        return value;
    }
    QComboBox* plane_control() const { return plane_; }
    QDoubleSpinBox* offset_control() const { return offset_; }
    QDoubleSpinBox* thickness_control() const { return thickness_; }
    QPushButton* sketch_button() const { return sketch_; }
    void on_swap(std::function<void()> callback) { swap_requested_=std::move(callback); }
    void swap_sides() {
        const auto values=parameters();
        // No intermediate widget signal may publish a partially swapped definition.
        std::vector<std::unique_ptr<QSignalBlocker>> blocked;
        for(auto& side:sides_) {
            blocked.push_back(std::make_unique<QSignalBlocker>(side.mode));
            blocked.push_back(std::make_unique<QSignalBlocker>(side.end));
            blocked.push_back(std::make_unique<QSignalBlocker>(side.value));
        }
        for(int i=0;i<2;++i) {
            auto& side=sides_[i];side.pending=values.sides[1-i];
            side.previous_mode=0; // Loading must not capture the old displayed number.
            side.mode->setCurrentIndex(static_cast<int>(side.pending.operation));
            refresh_side(i);
        }
    }
    QLineEdit* target_control(std::size_t side) const { return sides_.at(side).target; }
    QHBoxLayout* target_layout(std::size_t side) const { return sides_.at(side).target_layout; }
    QDoubleSpinBox* side_value(std::size_t side) const { return sides_.at(side).value; }
    QGroupBox* side_group(std::size_t side) const { return sides_.at(side).box; }
    bool subtract() const { return result_->currentIndex()!=1 && operation_group_->checkedId()==1; }
    void set_subtract(bool value) { operation_group_->button(value?1:0)->setChecked(true); }
    void bind_side_locks(std::set<std::string>& locks,std::function<void()> changed) {
        locks_=&locks;locks_changed_=std::move(changed);
        for(int i=0;i<2;++i)refresh_side_lock(i);
    }
    void on_change(std::function<void()> changed) {
        for(auto* box:findChildren<QComboBox*>())connect(box,&QComboBox::currentIndexChanged,this,[changed]{changed();});
        for(auto* box:findChildren<QDoubleSpinBox*>())connect(box,&QDoubleSpinBox::valueChanged,this,[changed]{changed();});
        for(auto* box:findChildren<QCheckBox*>())connect(box,&QCheckBox::toggled,this,[changed]{changed();});
        connect(operation_group_,&QButtonGroup::idClicked,this,[changed]{changed();});
    }
private:
    struct Side {
        QGroupBox* box{};QComboBox* mode{};QComboBox* end{};QDoubleSpinBox* value{};QLabel* label{};QLineEdit* target{};QHBoxLayout* target_layout{};
        document::FeatureSideParameters pending;
        int previous_mode{};
        double displayed_value{},authored_value{};
    };
    QVBoxLayout* content_{};
    QComboBox* plane_{};
    QPushButton* sketch_{};
    QPushButton* swap_{};
    std::function<void()> swap_requested_;
    QButtonGroup* operation_group_{};
    document::FeatureParameters initial_;
    std::array<Side,2> sides_{};
    QDoubleSpinBox *offset_{}, *thickness_{};
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
        const double number=side.value->value()==side.displayed_value?side.authored_value:side.value->value();
        if(mode==1){value.length=number;value.extrusion_extent=static_cast<document::EndCondition>(side.end->currentIndex());}
        if(mode==2){value.angle_degrees=number;value.rotation_extent=static_cast<document::FeatureRotationExtent>(side.end->currentIndex());}
        return value;
    }
    void refresh_operations() {
        const bool active=(sides_[0].mode&&sides_[0].mode->currentIndex()!=0)||
            (!symmetric_&&sides_[1].mode&&sides_[1].mode->currentIndex()!=0);
        operations_->setEnabled(active&&result_->currentIndex()!=1);
    }
    QDoubleSpinBox* number(const char* name,bool angle) {
        auto* value=new QDoubleSpinBox(this);value->setObjectName(name);value->setDecimals(ui::numeric_decimal_places(this));
        value->setRange(-1000000,1000000);value->setSuffix(angle?QString::fromUtf8(" °"):QString(" mm"));return value;
    }
    void refresh_side(int index) {
        auto& s=sides_[index];const int mode=s.mode->currentIndex();
        s.pending=capture_side(s,s.previous_mode);s.previous_mode=mode;
        s.end->clear();
        if(mode==2)s.end->addItems({tr("Úhel"),tr("Plná rotace"),tr("Až k")});
        else s.end->addItems({tr("Délka"),tr("Až k"),tr("Skrz vše")});
        s.end->setCurrentIndex(mode==2?static_cast<int>(s.pending.rotation_extent):static_cast<int>(s.pending.extrusion_extent));
        s.end->setEnabled(mode!=0);s.value->setEnabled(mode!=0&&s.end->currentIndex()==0);
        refresh_operations();
        s.label->setText(mode==2?tr("Úhel"):tr("Délka"));
        s.value->setSuffix(mode==2?QString::fromUtf8(" °"):QString(" mm"));
        s.value->setMaximum(mode==2?360:1000000);
        s.authored_value=mode==2?s.pending.angle_degrees:s.pending.length;s.value->setValue(s.authored_value);
        s.displayed_value=s.value->value();
        s.target->setEnabled(mode!=0 && s.end->currentIndex()==(mode==2?2:1));
        refresh_side_lock(index);
    }
    void refresh_side_lock(int index) {
        if(locks_)ui::bind_numeric_value_lock(sides_[index].value,
            "side"+std::to_string(index)+(sides_[index].mode->currentIndex()==2?"_angle":"_length"),*locks_,locks_changed_);
    }
};
}

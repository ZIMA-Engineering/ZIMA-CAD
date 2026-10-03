#include "reference_table_style.hpp"
#pragma once
#include "sweep_station_label.hpp"
#include "table_entry.hpp"
#include "sketch_button_style.hpp"
#include "sweep_placement_dialog.hpp"
#include "sweep_point_order_dialog.hpp"
#include <zima/ui/reference_cell.hpp>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <zima/ui/unit_spin_box.hpp>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>

namespace zima::app {
class Sweep2DDialog final : public SweepPlacementDialog {
public:
    Sweep2DDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        : SweepPlacementDialog(tr("Vlastnosti 2D tažení"),std::move(initial),parent),commit_(std::move(commit)) {
        setObjectName("sweep2dDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(440);
        set_initial_size(QSize(510,900));
        plane_initialized_=pending.sweep2d.path_plane.has_value()||!pending.sweep2d.profiles.empty();
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& value){pending.name=value.toStdString();});
        content_layout()->addLayout(form);install_placement();
        auto* path_row=new QHBoxLayout;
        plane_=new QComboBox(this);plane_->setObjectName("sweep2dPathPlane");
        plane_->addItem("XY","origin:plane:xy");plane_->addItem("XZ","origin:plane:xz");plane_->addItem("YZ","origin:plane:yz");
        connect(plane_,&QComboBox::currentIndexChanged,this,[this](int index){
            if(index<0||index>=3)return;
            pending.sweep2d.path_plane=document::ConstructionReference{{},pending.container_origin.id,plane_->itemData(index).toString().toStdString()};
            plane_initialized_=true;refresh_plane();notify();
        });
        plane_eye_=ui::build_reference_inspection_button(false,false,[this](bool on){plane_inspected_=on;refresh_plane();notify();});
        auto* path_button=new QPushButton(tr("Skica dráhy…"),this);path_button->setObjectName("sweep2dSketch0");style_sketch_button(path_button);
        connect(path_button,&QPushButton::clicked,this,[this]{if(edit_sketch)edit_sketch(0);});
        path_row->addWidget(plane_,1);path_row->addWidget(plane_eye_);path_row->addWidget(path_button);
        content_layout()->addWidget(new QLabel(tr("Rovina a skica dráhy"),this));content_layout()->addLayout(path_row);
        auto* help=new QLabel(tr("Vyberte vlastní rovinu XY, YZ nebo XZ kontejneru pro skicu dráhy."),this);
        help->setWordWrap(true);content_layout()->addWidget(help);
        thin_form_=new QFormLayout;
        auto* result=new QComboBox(this);result->setObjectName("sweep2dResultType");result->addItems({tr("Těleso"),tr("Thin"),tr("Plocha")});
        result->setCurrentIndex(pending.sweep2d.result_type==document::ProfileResultType::Surface?2:pending.sweep2d.result_type==document::ProfileResultType::Thin?1:0);thin_form_->addRow(tr("Typ výsledku"),result);
        thickness_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);thickness_->setObjectName("sweep2dThickness");
        thickness_->setRange(.001,1'000'000);thickness_->setValue(pending.sweep2d.thickness);thin_form_->addRow(tr("Tloušťka"),thickness_);
        ui::bind_numeric_value_lock(thickness_,"thickness",pending.value_locks,[this]{if(changed)changed();});
        side_=new QComboBox(this);side_->setObjectName("sweep2dThinSide");side_->addItems({tr("Dovnitř"),tr("Ven"),tr("Symetricky")});
        side_->setCurrentIndex(pending.sweep2d.thin_mode==document::ThinMode::OneSide?0:pending.sweep2d.thin_mode==document::ThinMode::OtherSide?1:2);
        side_->setToolTip(tr("Symetricky: polovina celkové tloušťky na každou stranu. U otevřené kontury stranu určuje její směr."));
        thin_form_->addRow(tr("Strana tloušťky"),side_);content_layout()->addLayout(thin_form_);
        connect(result,&QComboBox::currentIndexChanged,this,[this](int i){pending.sweep2d.result_type=i==2?document::ProfileResultType::Surface:i==1?document::ProfileResultType::Thin:document::ProfileResultType::Solid;update_thin();notify();});
        connect(thickness_,&QDoubleSpinBox::valueChanged,this,[this](double v){pending.sweep2d.thickness=v;notify();});
        connect(side_,&QComboBox::currentIndexChanged,this,[this](int i){pending.sweep2d.thin_mode=i==0?document::ThinMode::OneSide:i==1?document::ThinMode::OtherSide:document::ThinMode::Symmetric;notify();});
        profiles_=new QTableWidget(0,5,this);profiles_->setObjectName("sweep2dProfiles");profiles_->setMinimumHeight(150);
        profiles_->setHorizontalHeaderLabels({tr("Stanice"),tr("Skica profilu"),tr("Pořadí bodů"),tr("Použitý profil"),QString{}});
        profiles_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);profiles_->verticalHeader()->show();
        profiles_->verticalHeader()->setDefaultSectionSize(34);profiles_->verticalHeader()->setMinimumSectionSize(34);
        profiles_->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Fixed);profiles_->setColumnWidth(4,32);
        profiles_->horizontalHeader()->moveSection(4,0);profiles_->setSelectionMode(QAbstractItemView::NoSelection);
        style_reference_table(profiles_,4,0);
        connect(profiles_,&QTableWidget::cellClicked,this,[this](int row,int column){
            if(column==4)if(auto* button=qobject_cast<QPushButton*>(profiles_->cellWidget(row,1)))button->click();
        });
        profiles_->setEditTriggers(QAbstractItemView::NoEditTriggers);content_layout()->addWidget(profiles_,1);
        setProperty("expandBottomTable",true);
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        install_operation_buttons();update_thin();refresh_plane();refresh_profiles();
    }
    void set_status(const QString& text) override {status_->setText(path_error_.isEmpty()?text:path_error_);}
    void set_sketch(unsigned stage,const sketcher::Sketch& sketch) override {
        pending.sweep2d.sketch_data(stage)=sketch.serialized();
        if(stage==0) {
            std::erase_if(pending.sweep2d.profiles,[&](const auto& p){return !sketch.find_point(p.point_id);});
            try {
                const auto route=document::PartDocument::sweep2d_route(pending);
                std::erase_if(pending.sweep2d.profiles,[&](const auto& p){return std::ranges::none_of(route.stations,
                    [&](const auto& s){return s.point_id==p.point_id&&s.incoming==p.incoming;});});
                const auto& first=route.stations.front();
                document::PartDocument::ensure_sweep2d_profile(pending,first.point_id,first.incoming);
                document::PartDocument::reframe_sweep2d_sketches(pending);
            }catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));}
        }
        refresh_profiles();notify();
    }
    void seed_path_plane(const document::ConstructionReference& ref) {
        if(plane_initialized_)return;plane_initialized_=true;pending.sweep2d.path_plane=ref;
        pending.sweep2d.path_plane->orientation_drives_rotation=false;pending.sweep2d.path_plane->orientation_only=false;
        pending.sweep2d.path_plane->orientation_role.clear();refresh_plane();
    }
    bool point_order_open() const{return point_order_open_;}
    bool path_inspected() const{return plane_inspected_;}
    void end_path_entry(){plane_inspected_=false;refresh_plane();}
    void set_active_reference_index(std::optional<std::size_t> index) override {
        if(index)end_path_entry();SweepPlacementDialog::set_active_reference_index(index);
    }
    void clear_reference_highlights() override {
        plane_inspected_=false;refresh_plane();SweepPlacementDialog::clear_reference_highlights();
    }
    bool set_inline_parameter_value(std::string_view key,double value) override {
        if(key=="thickness"&&thickness_->isVisible()){thickness_->setValue(value);return true;}
        return SweepPlacementDialog::set_inline_parameter_value(key,value);
    }
    void refresh_profiles() {
        const QSignalBlocker blocked(profiles_);profiles_->setRowCount(0);
        document::Curve3DRoute route;
        path_error_.clear();
        try{route=document::PartDocument::sweep2d_route(pending);}catch(const std::exception& error){
            path_error_=tr("Dráha není platná: %1").arg(QObject::tr(error.what()));
            set_status(path_error_);return;
        }
        status_->clear();
        QString inherited;
        for(const auto& station:route.stations) {
            const int row=profiles_->rowCount();profiles_->insertRow(row);
            const auto found=std::ranges::find_if(pending.sweep2d.profiles,[&](const auto& p){return p.point_id==station.point_id&&p.incoming==station.incoming;});
            const auto index=static_cast<std::size_t>(std::distance(pending.sweep2d.profiles.begin(),found));
            const bool populated=found!=pending.sweep2d.profiles.end()&&document::sweep3d_profile_has_geometry(sketcher::Sketch::from_serialized(found->sketch_serialized));
            auto* indicator=ui::build_reference_row_indicator([this,station] {
                std::erase_if(pending.sweep2d.profiles,[&](const auto& p){return p.point_id==station.point_id&&p.incoming==station.incoming;});
                refresh_profiles();notify();
            });
            ui::set_reference_row_populated(indicator,populated);profiles_->setCellWidget(row,4,indicator);
            qobject_cast<QWidget*>(indicator->property("_removeWidget").value<QObject*>())->setToolTip(tr("Odstranit vlastní profil (stanice zůstane)"));
            qobject_cast<QWidget*>(indicator->property("_arrowWidget").value<QObject*>())->setToolTip(tr("Skica"));
            profiles_->setItem(row,0,new ui::ReferenceCellItem(sweep_station_label(station.label)));
            const auto status=populated?tr("Vlastní"):inherited.isEmpty()?tr("Vyplňte první profil"):tr("Z %1").arg(inherited);
            if(populated)inherited=sweep_station_label(station.label);
            profiles_->setItem(row,3,new ui::ReferenceCellItem(status));
            auto* button=new QPushButton(tr("Skica"),profiles_);style_sketch_button(button);
            button->setObjectName(QString("sweep2dStationSketch%1").arg(row));
            connect(button,&QPushButton::clicked,this,[this,station]{
                try{const auto i=document::PartDocument::ensure_sweep2d_profile(pending,station.point_id,station.incoming);
                    if(edit_sketch)edit_sketch(static_cast<unsigned>(i+1));}
                catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));}});
            profiles_->setCellWidget(row,1,button);
            auto* order=new QPushButton(tr("Pořadí bodů"),profiles_);order->setEnabled(populated);
            order->setObjectName(QString("sweep2dPointOrder%1").arg(row));
            connect(order,&QPushButton::clicked,this,[this,index]{
                try {
                    const auto initial=pending.sweep2d.profiles.at(index).correspondence_start_point_id;
                    const QPointer<Sweep2DDialog> self(this);
                    const auto preview=[self,index](std::string id){if(self){self->pending.sweep2d.profiles.at(index).correspondence_start_point_id=std::move(id);self->notify();}};
                    auto* dialog=new SweepPointOrderDialog(sketcher::Sketch::from_serialized(pending.sweep2d.profiles.at(index).sketch_serialized),
                        initial,preview,parentWidget(),pending.sweep2d.result_type!=document::ProfileResultType::Solid);
                    connect(dialog,&QDialog::finished,this,[self,preview,initial](int result){if(!self)return;if(result!=QDialog::Accepted)preview(initial);
                        self->point_order_open_=false;self->show();self->raise();self->notify();});
                    point_order_open_=true;hide();dialog->show();
                }catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));}
            });
            profiles_->setCellWidget(row,2,order);
            profiles_->setRowHeight(row,std::max({34,button->minimumHeight()+1,order->minimumSizeHint().height()+1}));
        }
    }
protected:
    bool submit() override {try{document::PartDocument::reframe_sweep2d_sketches(pending);commit_(pending);return true;}
        catch(const std::exception& e){set_status(tr(e.what()));return false;}}
private:
    std::function<void(document::HistoryContainer)> commit_;
    QComboBox* side_{};QDoubleSpinBox* thickness_{};QLabel* status_{};QFormLayout* thin_form_{};
    QTableWidget* profiles_{};QComboBox* plane_{};
    QToolButton* plane_eye_{};
    bool plane_initialized_{},plane_inspected_{},point_order_open_{};QString path_error_;
    void notify(){if(changed)changed();}
    void update_thin(){const bool enabled=pending.sweep2d.result_type==document::ProfileResultType::Thin;
        thin_form_->setRowVisible(thickness_,enabled);thin_form_->setRowVisible(side_,enabled);
        const bool surface=pending.is_surface_result();
        if(auto* subtract=findChild<QPushButton*>("primitiveSubtractOperation")) {
            if(surface&&subtract->isChecked())findChild<QPushButton*>("primitiveAddOperation")->click();
            subtract->setEnabled(!surface);
        }}
    void refresh_plane(){
        const auto& ref=pending.sweep2d.path_plane;const QSignalBlocker blocked(plane_),eye_blocked(plane_eye_);
        while(plane_->count()>3)plane_->removeItem(3);
        int index=static_cast<int>(sketcher::Sketch::from_serialized(pending.sweep2d.path_sketch).plane);
        if(ref) {
            index=ref->instance_path.empty()&&ref->owner_id==pending.container_origin.id
                ?plane_->findData(QString::fromStdString(ref->semantic_key)):-1;
            // Preserve a current reference assigned through commands until the user changes it.
            if(index<0){plane_->addItem(QString::fromStdString(ref->owner_id+":"+ref->semantic_key));index=3;}
        }
        plane_->setCurrentIndex(index);
        plane_eye_->setEnabled(ref.has_value());plane_eye_->setChecked(plane_inspected_&&ref.has_value());
    }
};
}

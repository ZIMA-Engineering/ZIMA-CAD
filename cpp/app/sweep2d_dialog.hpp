#pragma once
#include "sketch_button_style.hpp"
#include "sweep_placement_dialog.hpp"
#include "sweep_point_order_dialog.hpp"
#include <zima/ui/reference_cell.hpp>
#include <QComboBox>
#include <QDoubleSpinBox>
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
    std::function<void()> request_path_plane;
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
        plane_table_=new QTableWidget(1,3,this);plane_table_->setObjectName("sweep2dPathPlane");
        plane_table_->horizontalHeader()->hide();plane_table_->verticalHeader()->hide();
        plane_table_->setSelectionMode(QAbstractItemView::NoSelection);plane_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        plane_table_->setFixedHeight(36);plane_table_->setColumnWidth(0,24);plane_table_->setColumnWidth(2,28);
        plane_table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        ui::install_reference_cell_delegate(plane_table_);
        plane_indicator_=ui::build_reference_row_indicator([this]{pending.sweep2d.path_plane.reset();plane_initialized_=true;
            plane_label_.clear();plane_inspected_=false;refresh_plane();notify();});
        plane_table_->setCellWidget(0,0,plane_indicator_);
        plane_item_=new ui::ReferenceCellItem;plane_table_->setItem(0,1,plane_item_);
        plane_eye_=ui::build_reference_inspection_button(false,false,[this](bool on){plane_inspected_=on;refresh_plane();notify();});
        plane_table_->setCellWidget(0,2,ui::centered_cell_widget(plane_eye_));
        connect(plane_table_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==1&&request_path_plane)request_path_plane();});
        auto* path_button=new QPushButton(tr("Skica dráhy…"),this);path_button->setObjectName("sweep2dSketch0");style_sketch_button(path_button);
        connect(path_button,&QPushButton::clicked,this,[this]{if(edit_sketch)edit_sketch(0);});
        path_row->addWidget(plane_table_,1);path_row->addWidget(path_button);
        content_layout()->addWidget(new QLabel(tr("Rovina a skica dráhy"),this));content_layout()->addLayout(path_row);
        auto* help=new QLabel(tr("První rovina umístění předvyplní rovinu dráhy. Zde ji můžete změnit. "
            "Nakreslete jednu otevřenou dráhu z počátku skici; průřezy jsou kolmé k její tečně."),this);
        help->setWordWrap(true);content_layout()->addWidget(help);
        thin_form_=new QFormLayout;
        auto* result=new QComboBox(this);result->setObjectName("sweep2dResultType");result->addItems({tr("Těleso"),tr("Thin")});
        result->setCurrentIndex(pending.sweep2d.result_type==document::ProfileResultType::Thin?1:0);thin_form_->addRow(tr("Typ výsledku"),result);
        thickness_=new QDoubleSpinBox(this);thickness_->setObjectName("sweep2dThickness");thickness_->setDecimals(zima::ui::numeric_decimal_places(this,3));
        thickness_->setRange(.001,1'000'000);thickness_->setSuffix(" mm");thickness_->setValue(pending.sweep2d.thickness);thin_form_->addRow(tr("Tloušťka"),thickness_);
        side_=new QComboBox(this);side_->setObjectName("sweep2dThinSide");side_->addItems({tr("Dovnitř"),tr("Ven"),tr("Symetricky")});
        side_->setCurrentIndex(pending.sweep2d.thin_mode==document::ThinMode::OneSide?0:pending.sweep2d.thin_mode==document::ThinMode::OtherSide?1:2);
        side_->setToolTip(tr("Symetricky: polovina celkové tloušťky na každou stranu. U otevřené kontury stranu určuje její směr."));
        thin_form_->addRow(tr("Strana tloušťky"),side_);content_layout()->addLayout(thin_form_);
        connect(result,&QComboBox::currentIndexChanged,this,[this](int i){pending.sweep2d.result_type=i?document::ProfileResultType::Thin:document::ProfileResultType::Solid;update_thin();notify();});
        connect(thickness_,&QDoubleSpinBox::valueChanged,this,[this](double v){pending.sweep2d.thickness=v;notify();});
        connect(side_,&QComboBox::currentIndexChanged,this,[this](int i){pending.sweep2d.thin_mode=i==0?document::ThinMode::OneSide:i==1?document::ThinMode::OtherSide:document::ThinMode::Symmetric;notify();});
        profiles_=new QTableWidget(0,4,this);profiles_->setObjectName("sweep2dProfiles");profiles_->setMinimumHeight(150);
        profiles_->setHorizontalHeaderLabels({tr("Stanice"),tr("Skica profilu"),tr("Pořadí bodů"),tr("Použitý profil")});
        profiles_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);profiles_->verticalHeader()->hide();
        profiles_->setEditTriggers(QAbstractItemView::NoEditTriggers);content_layout()->addWidget(profiles_);
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        install_operation_buttons();update_thin();refresh_plane();refresh_profiles();
    }
    void set_status(const QString& text) override {status_->setText(text);}
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
    void seed_path_plane(const document::ConstructionReference& ref,const QString& label) {
        if(plane_initialized_)return;plane_initialized_=true;pending.sweep2d.path_plane=ref;
        pending.sweep2d.path_plane->orientation_drives_rotation=false;pending.sweep2d.path_plane->orientation_only=false;
        pending.sweep2d.path_plane->orientation_role.clear();plane_label_=label;refresh_plane();
    }
    void set_path_plane(document::ConstructionReference ref,const QString& label) {
        plane_initialized_=true;pending.sweep2d.path_plane=std::move(ref);plane_label_=label;plane_active_=false;refresh_plane();notify();
    }
    void set_path_active(bool active){plane_active_=active;refresh_plane();}
    bool path_active() const{return plane_active_;}
    bool point_order_open() const{return point_order_open_;}
    bool path_inspected() const{return plane_inspected_;}
    void end_path_entry(){plane_active_=false;plane_inspected_=false;refresh_plane();}
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
        try{route=document::PartDocument::sweep2d_route(pending);}catch(const std::exception&){return;}
        QString inherited;
        for(const auto& station:route.stations) {
            const int row=profiles_->rowCount();profiles_->insertRow(row);
            const auto found=std::ranges::find_if(pending.sweep2d.profiles,[&](const auto& p){return p.point_id==station.point_id&&p.incoming==station.incoming;});
            const auto index=static_cast<std::size_t>(std::distance(pending.sweep2d.profiles.begin(),found));
            const bool populated=found!=pending.sweep2d.profiles.end()&&document::sweep3d_profile_has_geometry(sketcher::Sketch::from_serialized(found->sketch_serialized));
            profiles_->setItem(row,0,new QTableWidgetItem(QString::fromStdString(station.label)));
            const auto status=populated?tr("Vlastní"):inherited.isEmpty()?tr("Vyplňte první profil"):tr("Z %1").arg(inherited);
            if(populated)inherited=QString::fromStdString(station.label);
            profiles_->setItem(row,3,new QTableWidgetItem(status));
            auto* button=new QPushButton(tr("Sketch"),profiles_);style_sketch_button(button);
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
                        initial,preview,parentWidget(),pending.sweep2d.result_type==document::ProfileResultType::Thin);
                    connect(dialog,&QDialog::finished,this,[self,preview,initial](int result){if(!self)return;if(result!=QDialog::Accepted)preview(initial);
                        self->point_order_open_=false;self->show();self->raise();self->notify();});
                    point_order_open_=true;hide();dialog->show();
                }catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));}
            });
            profiles_->setCellWidget(row,2,order);
        }
    }
protected:
    bool submit() override {try{document::PartDocument::reframe_sweep2d_sketches(pending);commit_(pending);return true;}
        catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));return false;}}
private:
    std::function<void(document::HistoryContainer)> commit_;
    QComboBox* side_{};QDoubleSpinBox* thickness_{};QLabel* status_{};QFormLayout* thin_form_{};
    QTableWidget* profiles_{};QTableWidget* plane_table_{};ui::ReferenceCellItem* plane_item_{};
    QWidget* plane_indicator_{};QToolButton* plane_eye_{};
    bool plane_initialized_{},plane_active_{},plane_inspected_{},point_order_open_{};QString plane_label_;
    void notify(){if(changed)changed();}
    void update_thin(){const bool enabled=pending.sweep2d.result_type==document::ProfileResultType::Thin;
        thin_form_->setRowVisible(thickness_,enabled);thin_form_->setRowVisible(side_,enabled);}
    void refresh_plane(){
        const auto& ref=pending.sweep2d.path_plane;const QSignalBlocker blocked(plane_eye_);
        if(ref){plane_item_->set_reference(QString::fromStdString(ref->owner_id+":"+ref->semantic_key));
            plane_item_->setText(plane_label_.isEmpty()?QString::fromStdString(ref->semantic_key):plane_label_);}
        else {plane_item_->clear_reference();plane_item_->setText(tr("Rovina kontejneru / vyberte rovinu…"));}
        plane_item_->set_active_input(plane_active_);plane_item_->set_inspected(plane_inspected_&&ref.has_value());
        ui::set_reference_row_populated(plane_indicator_,ref.has_value());plane_eye_->setEnabled(ref.has_value());plane_eye_->setChecked(plane_inspected_&&ref.has_value());
        plane_table_->viewport()->update();
    }
};
}

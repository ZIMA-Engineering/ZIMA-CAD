#pragma once
#include "sweep_placement_dialog.hpp"
#include <zima/document/general_surface.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <set>
namespace zima::app {
class GeneralSurfaceDialog final:public SweepPlacementDialog {
public:
    std::function<void(unsigned)> edit_curve;
    GeneralSurfaceDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        :SweepPlacementDialog(tr("Vlastnosti obecné plochy"),std::move(initial),parent),commit_(std::move(commit)) {
        setObjectName("generalSurfaceDialog");setAttribute(Qt::WA_DeleteOnClose);
        setProperty("expandBottomTable",true);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("generalSurfaceName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        install_placement();
        auto* note=new QLabel(tr("Nakreslete hranice vlastními skicami nebo 3D křivkami. OK vypočítá plochu."),this);
        note->setWordWrap(true);content_layout()->addWidget(note);
        table_=new QTableWidget(0,7,this);table_->setObjectName("generalSurfaceBoundaries");
        table_->setHorizontalHeaderLabels({tr("Č."),{},{},tr("Vlastní hranice"),{},tr("Rovina"),tr("Odsazení")});
        table_->verticalHeader()->hide();table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui::install_reference_cell_delegate(table_);
        for(int column:{0,1,2,4}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,34);}
        table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(5,QHeaderView::Fixed);table_->setColumnWidth(5,70);
        table_->horizontalHeader()->setSectionResizeMode(6,QHeaderView::Fixed);table_->setColumnWidth(6,110);
        content_layout()->addWidget(table_,1);
        auto* actions=new QHBoxLayout;
        for(bool curve:{false,true}) {
            auto* add=new QPushButton(curve?tr("Přidat 3D křivku"):tr("Přidat skicu"),this);
            add->setObjectName(curve?"generalSurfaceAddCurve":"generalSurfaceAddSketch");actions->addWidget(add);
            connect(add,&QPushButton::clicked,this,[this,curve]{
                auto boundary=curve?document::create_general_surface_curve(pending):document::create_general_surface_sketch(pending);
                if(boundary.curve)boundary.curve->name=tr("3D křivka").toStdString();
                else {auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);sketch.name=tr("Skica").toStdString();boundary.sketch_serialized=sketch.serialized();}
                pending.general_surface.boundaries.push_back(std::move(boundary));refresh();notify();
            });
        }
        for(bool down:{false,true}) {
            auto* button=new QPushButton(ui::reference_arrow_icon(down?Qt::DownArrow:Qt::UpArrow),down?tr("Dolů"):tr("Nahoru"),this);
            button->setObjectName(down?"generalSurfaceMoveDown":"generalSurfaceMoveUp");actions->addWidget(button);
            (down?down_:up_)=button;connect(button,&QPushButton::clicked,this,[this,down]{move_selected(down?1:-1);});
        }
        content_layout()->addLayout(actions);status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){
            if(column!=3||row<0||row>=table_->rowCount())return;
            if(pending.general_surface.boundaries[row].curve){if(edit_curve)edit_curve(row);}
            else if(edit_sketch)edit_sketch(row);
        });
        refresh();set_initial_size({700,680});
    }
    void set_status(const QString& text) override {status_->setText(text);}
    void set_sketch(unsigned index,const sketcher::Sketch& sketch) override {
        auto& boundary=pending.general_surface.boundaries.at(index);
        if(boundary.curve||sketch.owner_container_id!=pending.id||sketch.id!=sketcher::Sketch::from_serialized(boundary.sketch_serialized).id)
            throw std::invalid_argument("Invalid owned surface boundary.");
        boundary.sketch_serialized=sketch.serialized();document::reframe_general_surface(pending);refresh();
    }
    bool owns_reference_owner(const std::string& owner) const override {
        if(SweepPlacementDialog::owns_reference_owner(owner))return true;
        for(const auto& boundary:pending.general_surface.boundaries) {
            if(!boundary.curve){if(boundary_id(boundary)==owner)return true;continue;}
            const auto owns=[&](auto&& self,const auto& curve)->bool {
                if(curve.id==owner||curve.entity_id==owner||curve.container_origin.id==owner)return true;
                for(const auto& child:curve.container_origin.children)if(child.id==owner)return true;
                return std::ranges::any_of(curve.curve_points,[&](const auto& point){return self(self,point);});
            };
            if(owns(owns,*boundary.curve))return true;
        }
        return false;
    }
    const std::set<std::string>& inspected_boundaries()const{return inspected_;}
    void end_entry(){set_active_reference_index(std::nullopt);clear_reference_highlights();inspected_.clear();refresh();notify();}
    void refresh() {
        QSignalBlocker blocker(table_);table_->setRowCount(static_cast<int>(pending.general_surface.boundaries.size()));
        for(int row=0;row<table_->rowCount();++row) {
            const auto& boundary=pending.general_surface.boundaries[row];const auto id=boundary_id(boundary);
            table_->setRowHeight(row,34);table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* order=new QCheckBox(this);order->setChecked(id==ordered_);table_->setCellWidget(row,1,ui::centered_cell_widget(order));
            connect(order,&QCheckBox::toggled,this,[this,id,row](bool checked){
                ordered_=checked?id:ordered_==id?std::string{}:ordered_;
                for(int other=0;other<table_->rowCount();++other)if(other!=row)
                    if(auto* box=table_->cellWidget(other,1)->findChild<QCheckBox*>()){QSignalBlocker block(box);box->setChecked(false);}
                update_order_buttons();
            });
            auto* remove=ui::build_reference_row_indicator([this,id]{
                std::erase_if(pending.general_surface.boundaries,[&](const auto& boundary){return boundary_id(boundary)==id;});
                inspected_.erase(id);if(ordered_==id)ordered_.clear();refresh();notify();
            });
            ui::set_reference_row_populated(remove,true);remove->setEnabled(table_->rowCount()>2);
            if(auto* button=remove->findChild<QPushButton*>())button->setToolTip(tr("Odstranit vlastní hranici"));
            table_->setCellWidget(row,2,remove);
            const auto name=boundary.curve?boundary.curve->name:sketcher::Sketch::from_serialized(boundary.sketch_serialized).name;
            auto* item=new ui::ReferenceCellItem(QString::fromStdString(name));item->set_reference(QString::fromStdString(id));item->set_inspected(inspected_.contains(id));
            table_->setItem(row,3,item);
            auto* eye=ui::build_reference_inspection_button(true,inspected_.contains(id),[this,id](bool on){
                if(on)inspected_.insert(id);else inspected_.erase(id);
                for(int row=0;row<table_->rowCount();++row)if(boundary_id(pending.general_surface.boundaries[row])==id)
                    static_cast<ui::ReferenceCellItem*>(table_->item(row,3))->set_inspected(on);
                table_->viewport()->update();notify();
            });table_->setCellWidget(row,4,ui::centered_cell_widget(eye));
            if(boundary.curve) {table_->setCellWidget(row,5,new QWidget);table_->setCellWidget(row,6,new QWidget);continue;}
            const auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);
            auto* plane=new QComboBox(this);plane->addItems({"XY","XZ","YZ"});plane->setCurrentIndex(static_cast<int>(sketch.plane));
            connect(plane,&QComboBox::currentIndexChanged,this,[this,id](int value){edit_plane(id,[&](auto& sketch){sketch.plane=static_cast<sketcher::SketchPlane>(value);sketch.plane_auto=false;});});
            table_->setCellWidget(row,5,plane);
            auto* offset=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);offset->setRange(-1000000,1000000);
            offset->set_display_decimals(ui::numeric_decimal_places(this));offset->setValue(sketch.plane_offset);
            connect(offset,&QDoubleSpinBox::valueChanged,this,[this,id](double value){edit_plane(id,[&](auto& sketch){sketch.plane_offset=value;});});
            table_->setCellWidget(row,6,offset);
        }
        update_order_buttons();
    }
protected:
    bool submit() override {
        try{read_placement();document::validate_general_surface(pending);commit_(pending);return true;}
        catch(const std::exception& error){set_status(tr(error.what()));return false;}
    }
private:
    static std::string boundary_id(const document::GeneralSurfaceBoundary& boundary) {
        return boundary.curve?boundary.curve->id:sketcher::Sketch::from_serialized(boundary.sketch_serialized).id;
    }
    template<class Edit>void edit_plane(const std::string& id,const Edit& edit) {
        for(auto& boundary:pending.general_surface.boundaries)if(!boundary.curve&&boundary_id(boundary)==id) {
            auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);edit(sketch);boundary.sketch_serialized=sketch.serialized();
            document::reframe_general_surface(pending);notify();return;
        }
    }
    int ordered_index()const {
        for(std::size_t i=0;i<pending.general_surface.boundaries.size();++i)if(boundary_id(pending.general_surface.boundaries[i])==ordered_)return static_cast<int>(i);
        return -1;
    }
    void update_order_buttons(){const auto index=ordered_index();up_->setEnabled(index>0);down_->setEnabled(index>=0&&index+1<table_->rowCount());}
    void move_selected(int step){const auto index=ordered_index();if(index<0||index+step<0||index+step>=table_->rowCount())return;
        std::swap(pending.general_surface.boundaries[index],pending.general_surface.boundaries[index+step]);refresh();notify();}
    void notify(){if(changed)changed();}
    QTableWidget* table_{};QLabel* status_{};QPushButton *up_{},*down_{};
    std::string ordered_;std::set<std::string> inspected_;
    std::function<void(document::HistoryContainer)> commit_;
};
} // namespace zima::app

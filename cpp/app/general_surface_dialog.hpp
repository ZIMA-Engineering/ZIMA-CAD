#pragma once
#include "sweep_placement_dialog.hpp"
#include <zima/document/general_surface.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QMouseEvent>
#include <set>
namespace zima::app {
class GeneralSurfaceDialog final:public SweepPlacementDialog {
public:
    std::function<void(unsigned)> edit_curve;
    std::function<void(unsigned)> edit_sketch_properties;
    std::function<void()> inspection_changed;
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
        table_=new QTableWidget(0,5,this);table_->setObjectName("generalSurfaceBoundaries");
        table_->setHorizontalHeaderLabels({{},{},tr("Typ"),tr("Vlastní hranice"),{}});
        table_->verticalHeader()->setDefaultSectionSize(34);table_->verticalHeader()->setMinimumSectionSize(34);
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui::install_reference_cell_delegate(table_);
        for(int column:{0,1,4}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,34);}
        table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
        content_layout()->addWidget(table_,1);
        auto* actions=new QHBoxLayout;
        for(bool down:{false,true}) {
            auto* button=new QPushButton(ui::reference_arrow_icon(down?Qt::DownArrow:Qt::UpArrow),down?tr("Dolů"):tr("Nahoru"),this);
            button->setObjectName(down?"generalSurfaceMoveDown":"generalSurfaceMoveUp");actions->addWidget(button);
            (down?down_:up_)=button;connect(button,&QPushButton::clicked,this,[this,down]{move_selected(down?1:-1);});
        }
        content_layout()->addLayout(actions);status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){
            if(column!=3||row<0||row>=table_->rowCount())return;
            if(row==static_cast<int>(pending.general_surface.boundaries.size()))add_boundary();
            else open_boundary(row);
        });
        refresh();set_initial_size({700,800});
    }
    void set_status(const QString& text) override {status_->setText(text);}
    void set_sketch(unsigned index,const sketcher::Sketch& sketch) override {
        auto& boundary=pending.general_surface.boundaries.at(index);
        if(boundary.curve||!boundary.sketch_feature||sketch.owner_container_id!=boundary.sketch_feature->id||sketch.id!=sketcher::Sketch::from_serialized(boundary.sketch_serialized).id)
            throw std::invalid_argument("Invalid owned surface boundary.");
        boundary.sketch_serialized=sketch.serialized();boundary.sketch_feature->name=sketch.name;
        document::reframe_general_surface(pending);refresh();
    }
    bool owns_reference_owner(const std::string& owner) const override {
        if(SweepPlacementDialog::owns_reference_owner(owner))return true;
        for(const auto& boundary:pending.general_surface.boundaries) {
            if(!boundary.curve){if(boundary_id(boundary)==owner||boundary.sketch_feature->id==owner||
                boundary.sketch_feature->feature_id==owner||boundary.sketch_feature->container_origin.id==owner)return true;continue;}
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
    bool inspects_boundary_owner(const std::string& owner)const {
        if(inspected_.contains(owner))return true;
        // Curve wires use their native entity identity, while the row owns
        // the Curve container identity. Preserve both established identities.
        return std::ranges::any_of(pending.general_surface.boundaries,[&](const auto& boundary){
            return boundary.curve&&boundary.curve->entity_id==owner&&inspected_.contains(boundary.curve->id);
        });
    }
    void end_entry(){set_active_reference_index(std::nullopt);clear_reference_highlights();inspected_.clear();refresh();notify();}
    void refresh() {
        QSignalBlocker blocker(table_);table_->setRowCount(static_cast<int>(pending.general_surface.boundaries.size())+1);
        for(int row=0;row<static_cast<int>(pending.general_surface.boundaries.size());++row) {
            const auto& boundary=pending.general_surface.boundaries[row];const auto id=boundary_id(boundary);
            table_->setRowHeight(row,34);
            auto* order=new QCheckBox(this);order->setChecked(id==ordered_);table_->setCellWidget(row,0,ui::centered_cell_widget(order));
            connect(order,&QCheckBox::toggled,this,[this,id,row](bool checked){
                ordered_=checked?id:ordered_==id?std::string{}:ordered_;
                for(int other=0;other<table_->rowCount();++other)if(other!=row)
                    if(auto* box=table_->cellWidget(other,0)->findChild<QCheckBox*>()){QSignalBlocker block(box);box->setChecked(false);}
                update_order_buttons();
            });
            auto* remove=ui::build_reference_row_indicator([this,id]{
                std::erase_if(pending.general_surface.boundaries,[&](const auto& boundary){return boundary_id(boundary)==id;});
                inspected_.erase(id);if(ordered_==id)ordered_.clear();refresh();notify();
            });
            ui::set_reference_row_populated(remove,true);
            if(auto* button=remove->findChild<QPushButton*>())button->setToolTip(tr("Odstranit vlastní hranici"));
            table_->setCellWidget(row,1,ui::centered_cell_widget(remove));
            auto* type=boundary_type(boundary.curve.has_value());
            type->setEnabled(empty_boundary(boundary));
            if(!type->isEnabled())type->setToolTip(tr("Pro jiný typ odstraňte hranici a přidejte novou."));
            connect(type,&QComboBox::currentIndexChanged,this,[this,id](int value){replace_empty_boundary(id,value==1);});
            table_->setCellWidget(row,2,type);
            const auto name=boundary.curve?boundary.curve->name:sketcher::Sketch::from_serialized(boundary.sketch_serialized).name;
            auto* item=new ui::ReferenceCellItem(QString::fromStdString(name));item->set_reference(QString::fromStdString(id));item->set_inspected(inspected_.contains(id));
            table_->setItem(row,3,item);
            auto* eye=ui::build_reference_inspection_button(true,inspected_.contains(id),[this,id](bool on){
                if(on)inspected_.insert(id);else inspected_.erase(id);
                for(int row=0;row<static_cast<int>(pending.general_surface.boundaries.size());++row)if(boundary_id(pending.general_surface.boundaries[row])==id)
                    static_cast<ui::ReferenceCellItem*>(table_->item(row,3))->set_inspected(on);
                table_->viewport()->update();if(inspection_changed)inspection_changed();
            });table_->setCellWidget(row,4,ui::centered_cell_widget(eye));
        }
        const int row=static_cast<int>(pending.general_surface.boundaries.size());
        table_->setCellWidget(row,0,new QWidget);
        auto* arrow=ui::build_reference_row_indicator({});ui::set_reference_row_populated(arrow,false);
        if(auto* label=arrow->findChild<QLabel*>()) {label->setToolTip(tr("Přidat vlastní hranici…"));label->setProperty("generalSurfaceAddBoundary",true);label->installEventFilter(this);}
        table_->setCellWidget(row,1,ui::centered_cell_widget(arrow));
        auto* type=boundary_type(new_curve_);type->setObjectName("generalSurfaceNewBoundaryType");
        connect(type,&QComboBox::currentIndexChanged,this,[this](int value){new_curve_=value==1;});table_->setCellWidget(row,2,type);
        table_->setItem(row,3,new ui::ReferenceCellItem(tr("Přidat vlastní hranici…")));
        table_->setCellWidget(row,4,ui::centered_cell_widget(ui::build_reference_inspection_button(false,false,{})));
        update_order_buttons();
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if(object->property("generalSurfaceAddBoundary").toBool()&&event->type()==QEvent::MouseButtonRelease&&
            static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){add_boundary();return true;}
        return SweepPlacementDialog::eventFilter(object,event);
    }
    bool submit() override {
        try{read_placement();document::validate_general_surface(pending);commit_(pending);return true;}
        catch(const std::exception& error){set_status(tr(error.what()));return false;}
    }
private:
    static std::string boundary_id(const document::GeneralSurfaceBoundary& boundary) {
        return boundary.curve?boundary.curve->id:sketcher::Sketch::from_serialized(boundary.sketch_serialized).id;
    }
    QComboBox* boundary_type(bool curve) {
        auto* type=new QComboBox(this);type->addItems({tr("Skica"),tr("3D křivka")});type->setCurrentIndex(curve?1:0);return type;
    }
    document::GeneralSurfaceBoundary new_boundary(bool curve) {
        auto boundary=curve?document::create_general_surface_curve(pending):document::create_general_surface_sketch(pending);
        if(boundary.curve)boundary.curve->name=tr("3D křivka").toStdString();
        else {auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);sketch.name=tr("Skica").toStdString();boundary.sketch_serialized=sketch.serialized();
            boundary.sketch_feature->name=sketch.name;boundary.sketch_feature->feature.automatic_name=sketch.name;}
        return boundary;
    }
    static bool empty_boundary(const document::GeneralSurfaceBoundary& boundary) {
        if(boundary.curve)return boundary.curve->curve_points.empty()&&boundary.curve->references.empty();
        const auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);
        return sketch.points.empty()&&sketch.segments.empty()&&sketch.circles.empty()&&sketch.arcs.empty()&&sketch.ellipses.empty()&&
            sketch.elliptical_arcs.empty()&&sketch.bsplines.empty()&&sketch.external_references.empty()&&sketch.texts.empty()&&sketch.symbols.empty()&&sketch.import_blocks.empty();
    }
    void replace_empty_boundary(const std::string& id,bool curve) {
        for(auto& boundary:pending.general_surface.boundaries)if(boundary_id(boundary)==id&&empty_boundary(boundary)&&curve!=boundary.curve.has_value()) {
            boundary=new_boundary(curve);inspected_.erase(id);if(ordered_==id)ordered_=boundary_id(boundary);refresh();notify();return;
        }
    }
    void open_boundary(unsigned row) {
        if(pending.general_surface.boundaries.at(row).curve){if(edit_curve)edit_curve(row);}
        else if(edit_sketch_properties)edit_sketch_properties(row);
    }
    void add_boundary() {
        const auto row=static_cast<unsigned>(pending.general_surface.boundaries.size());
        pending.general_surface.boundaries.push_back(new_boundary(new_curve_));refresh();notify();open_boundary(row);
    }
    int ordered_index()const {
        for(std::size_t i=0;i<pending.general_surface.boundaries.size();++i)if(boundary_id(pending.general_surface.boundaries[i])==ordered_)return static_cast<int>(i);
        return -1;
    }
    void update_order_buttons(){const auto index=ordered_index();up_->setEnabled(index>0);down_->setEnabled(index>=0&&index+1<static_cast<int>(pending.general_surface.boundaries.size()));}
    void move_selected(int step){const auto index=ordered_index();if(index<0||index+step<0||index+step>=static_cast<int>(pending.general_surface.boundaries.size()))return;
        std::swap(pending.general_surface.boundaries[index],pending.general_surface.boundaries[index+step]);refresh();notify();}
    void notify(){if(changed)changed();}
    QTableWidget* table_{};QLabel* status_{};QPushButton *up_{},*down_{};
    std::string ordered_;std::set<std::string> inspected_;
    bool new_curve_{};
    std::function<void(document::HistoryContainer)> commit_;
};
} // namespace zima::app

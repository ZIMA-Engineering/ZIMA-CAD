#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/document/surface_trim.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QFormLayout>
#include <QToolButton>
#include <QPushButton>
#include <QSignalBlocker>
namespace zima::app {
class SurfaceTrimDialog final:public ui::PropertiesSubWindow {
public:
    document::HistoryContainer pending;
    std::function<void()> changed;
    std::function<QString(const kernel::SurfaceTrimTool&)> label;
    SurfaceTrimDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti oříznutí plochy"),parent),pending(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("surfaceTrimDialog");setAttribute(Qt::WA_DeleteOnClose);setProperty("originSelectionBound",true);
        setProperty("expandBottomTable",true);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("surfaceTrimName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        auto* note=new QLabel(tr("Vyberte cílovou plochu, řezné plochy nebo hrany a část, kterou chcete zachovat."),this);note->setWordWrap(true);content_layout()->addWidget(note);
        table_=new QTableWidget(0,4,this);table_->setObjectName("surfaceTrimReferences");
        table_->setHorizontalHeaderLabels({tr("Č."),{},tr("Reference"),{}});table_->verticalHeader()->hide();
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        for(int column:{0,1,3}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,34);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        inspected_.resize(pending.surface_trim.tools.size()+1,false);
        active_=pending.surface_trim.target.valid()?static_cast<int>(pending.surface_trim.tools.size())+1:0;
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2){active_=row;refresh();notify();}});
        content_layout()->addWidget(table_,1);
        auto* region=new QPushButton(tr("Vybrat zachovanou část"),this);region->setObjectName("surfaceTrimRegion");content_layout()->addWidget(region);
        connect(region,&QPushButton::clicked,this,[this]{active_=-2;refresh();notify();});
        region_=new QLabel(this);region_->setObjectName("surfaceTrimRegionStatus");content_layout()->addWidget(region_);
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        set_initial_size({660,410});rebuild();refresh();
    }
    int active_row()const{return active_;}
    bool inspected(int row)const{return row>=0&&row<static_cast<int>(inspected_.size())&&inspected_[row];}
    void set_target(const kernel::FaceReference& reference) {
        pending.surface_trim.target=reference;pending.surface_trim.seed_valid=false;pending.surface_trim.retained_region_key.clear();
        inspected_[0]=true;active_=static_cast<int>(pending.surface_trim.tools.size())+1;refresh();notify();
    }
    void set_tool(kernel::SurfaceTrimTool tool) {
        auto& p=pending.surface_trim;
        if(active_<1||active_>static_cast<int>(p.tools.size())+1)return;
        if(tool.face&&tool.reference.owner_id==p.target.owner_id&&tool.reference.semantic_key==p.target.semantic_key)return;
        for(int row=0;row<static_cast<int>(p.tools.size());++row)if(row+1!=active_&&p.tools[row]==tool)return;
        if(active_==static_cast<int>(p.tools.size())+1){p.tools.push_back(std::move(tool));inspected_.push_back(true);}
        else{p.tools[active_-1]=std::move(tool);inspected_[active_]=true;}
        p.retained_region_key.clear();active_=static_cast<int>(p.tools.size())+1;rebuild();refresh();notify();
    }
    void set_region(kernel::Vec3 seed){pending.surface_trim.seed=seed;pending.surface_trim.seed_valid=true;pending.surface_trim.retained_region_key.clear();active_=-1;refresh();notify();}
    void end_entry(){active_=-1;std::fill(inspected_.begin(),inspected_.end(),false);refresh();notify();}
    void refresh() {
        for(int row=0;row<table_->rowCount();++row) {
            kernel::SurfaceTrimTool tool;
            if(row==0){const auto& r=pending.surface_trim.target;tool={{r.owner_id,r.semantic_key,r.instance_path},true};}
            else if(row<=static_cast<int>(pending.surface_trim.tools.size()))tool=pending.surface_trim.tools[row-1];
            const bool populated=tool.reference.valid();auto* item=static_cast<ui::ReferenceCellItem*>(table_->item(row,2));
            if(populated){item->set_reference(QString::fromStdString(tool.reference.owner_id+":"+tool.reference.semantic_key));item->setText(label?label(tool):QString::fromStdString(tool.reference.owner_id));}
            else{item->clear_reference();item->setText(row==0?tr("Vyberte cílovou plochu…"):tr("Vyberte řeznou plochu nebo hranu…"));}
            item->set_active_input(active_==row);item->set_inspected(inspected(row));
            if(row==0)target_indicator_->setVisible(!populated);else ui::set_reference_row_populated(table_->cellWidget(row,1),populated);
            const QSignalBlocker block(eyes_[row]);eyes_[row]->setEnabled(populated);eyes_[row]->setChecked(inspected(row));
        }
        region_->setText(active_==-2?tr("Klikněte uvnitř části cílové plochy, kterou chcete zachovat."):
            pending.surface_trim.seed_valid?tr("Zachovaná část je vybrána."):tr("Vyberte zachovanou část."));table_->viewport()->update();
    }
private:
    void rebuild() {
        const int count=static_cast<int>(pending.surface_trim.tools.size())+2;table_->setRowCount(0);table_->setRowCount(count);eyes_.resize(count);
        for(int row=0;row<count;++row) {
            table_->setRowHeight(row,34);table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator(row==0?std::function<void()>{}:std::function<void()>{[this,row]{
                auto& p=pending.surface_trim;if(row>static_cast<int>(p.tools.size()))return;
                p.tools.erase(p.tools.begin()+row-1);inspected_.erase(inspected_.begin()+row);p.retained_region_key.clear();
                active_=static_cast<int>(p.tools.size())+1;rebuild();refresh();notify();
            }});
            if(row==0){target_indicator_=indicator;table_->setCellWidget(row,1,ui::centered_cell_widget(indicator));}
            else{indicator->findChild<QPushButton*>()->setToolTip(tr("Odstranit řezný nástroj ze seznamu"));table_->setCellWidget(row,1,indicator);}
            table_->setItem(row,2,new ui::ReferenceCellItem);
            eyes_[row]=ui::build_reference_inspection_button(false,false,[this,row](bool on){if(row<static_cast<int>(inspected_.size())){inspected_[row]=on;refresh();notify();}});
            table_->setCellWidget(row,3,ui::centered_cell_widget(eyes_[row]));
        }
        table_->setMinimumHeight(table_->horizontalHeader()->sizeHint().height()+2*table_->frameWidth()+std::min(count,4)*34);
    }
    bool submit()override{try{commit_(pending);return true;}catch(const std::exception& error){status_->setText(tr(error.what()));return false;}}
    void notify(){if(changed)changed();}
    std::function<void(document::HistoryContainer)> commit_;QTableWidget* table_{};QLabel* status_{};QLabel* region_{};QWidget* target_indicator_{};
    std::vector<QToolButton*> eyes_;std::vector<bool> inspected_;int active_{};
};
}

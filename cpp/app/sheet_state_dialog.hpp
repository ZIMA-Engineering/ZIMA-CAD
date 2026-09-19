#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/document/part_document.hpp>
#include <QCheckBox>
#include <QButtonGroup>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <map>
#include <set>
#include <algorithm>
#include <ranges>
namespace zima::app {
class SheetStateDialog final : public ui::PropertiesSubWindow {
public:
    using Commit=std::function<void(document::HistoryContainer)>;
    SheetStateDialog(document::HistoryContainer initial,std::map<std::string,QString> available,Commit commit,QWidget* parent)
        : PropertiesSubWindow(initial.feature_kind==document::FeatureKind::Unbend?tr("Rozvinout"):tr("Ohnout zpět"),parent),
          initial_(std::move(initial)),available_(std::move(available)),commit_(std::move(commit)) {
        setAttribute(Qt::WA_DeleteOnClose);setObjectName("sheetStateDialog");
        // This history operation has no placement or Origin input.
        setProperty("originSelectionBound",true);
        auto* form=new QFormLayout;
        name_=new QLineEdit(QString::fromStdString(initial_.name),this);name_->setObjectName("sheetStateName");form->addRow(tr("Název"),name_);
        all_=new QCheckBox(initial_.feature_kind==document::FeatureKind::Unbend?
            tr("Rozvinout vše"):tr("Ohnout zpět vše"),this);
        all_->setObjectName("sheetStateAll");all_->setChecked(initial_.sheet_state.all);form->addRow(all_);
        individual_=new QCheckBox(tr("Vybrat jednotlivé prvky"),this);
        individual_->setObjectName("sheetStateIndividual");individual_->setChecked(!initial_.sheet_state.all);form->addRow(individual_);content_layout()->addLayout(form);
        auto* modes=new QButtonGroup(this);modes->setExclusive(true);
        modes->addButton(all_);modes->addButton(individual_);
        table_=new QTableWidget(0,3,this);table_->setObjectName("sheetStateElements");
        table_->setHorizontalHeaderLabels({QString{},tr("Prvek"),QString{}});table_->setColumnWidth(0,32);table_->setColumnWidth(2,32);
        table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui::install_reference_cell_delegate(table_);
        content_layout()->addWidget(table_);set_initial_size({365,310});
        inspected_.insert(initial_.sheet_state.owners.begin(),initial_.sheet_state.owners.end());
        connect(table_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==1){entering_=true;refresh();}});
        const auto mode=[this](bool all) {
            const QSignalBlocker a(all_),b(individual_);
            all_->setChecked(all);individual_->setChecked(!all);
            entering_=!all;highlight_all_=true;refresh();
        };
        connect(all_,&QCheckBox::toggled,this,[mode](bool checked){if(checked)mode(true);});
        connect(individual_,&QCheckBox::toggled,this,[mode](bool checked){if(checked)mode(false);});refresh();
    }
    std::function<void()> selection_changed;
    document::HistoryContainer pending_value()const {
        auto value=initial_;value.name=name_->text().trimmed().toStdString();value.sheet_state.all=!individual_->isChecked();return value;
    }
    bool selecting()const{return individual_->isChecked()&&entering_;}
    void end_entry(){entering_=false;highlight_all_=false;inspected_.clear();refresh();}
    bool available(const std::string& id)const{return available_.contains(id);}
    void toggle(const std::string& id) {
        if(!selecting()||!available(id))return;
        auto& owners=initial_.sheet_state.owners;
        if(std::ranges::find(owners,id)==owners.end()){owners.push_back(id);inspected_.insert(id);}
        else {std::erase(owners,id);inspected_.erase(id);}
        refresh();
    }
    std::vector<std::string> selected()const {
        if(individual_->isChecked())return initial_.sheet_state.owners;
        std::vector<std::string> owners;for(const auto& [id,label]:available_)owners.push_back(id);return owners;
    }
    std::vector<std::string> highlighted()const {
        if(!individual_->isChecked())return highlight_all_?selected():std::vector<std::string>{};
        return {inspected_.begin(),inspected_.end()};
    }
private:
    bool submit()override {commit_(pending_value());return true;}
    void refresh() {
        table_->setEnabled(individual_->isChecked());table_->setRowCount(0);
        for(const auto& id:initial_.sheet_state.owners) {
            const int row=table_->rowCount();table_->insertRow(row);
            auto* indicator=ui::build_reference_row_indicator([this,id]{std::erase(initial_.sheet_state.owners,id);inspected_.erase(id);refresh();});
            ui::set_reference_row_populated(indicator,true);table_->setCellWidget(row,0,ui::centered_cell_widget(indicator));
            auto* field=new ui::ReferenceCellItem(available_.contains(id)?available_.at(id):QString::fromStdString(id));
            field->set_reference(QString::fromStdString(id));field->set_inspected(inspected_.contains(id));table_->setItem(row,1,field);
            auto* eye=ui::build_reference_inspection_button(true,inspected_.contains(id),[this,id](bool checked){
                if(checked)inspected_.insert(id);else inspected_.erase(id);refresh();});
            table_->setCellWidget(row,2,ui::centered_cell_widget(eye));
        }
        const int row=table_->rowCount();table_->insertRow(row);
        auto* indicator=ui::build_reference_row_indicator([]{});ui::set_reference_row_populated(indicator,false);
        table_->setCellWidget(row,0,ui::centered_cell_widget(indicator));
        auto* field=new ui::ReferenceCellItem(tr("Vyberte prvek ve View nebo ve stromu"));field->set_active_input(selecting());
        table_->setItem(row,1,field);
        if(selection_changed)selection_changed();
    }
    document::HistoryContainer initial_;std::map<std::string,QString> available_;Commit commit_;
    QLineEdit* name_{};QCheckBox* all_{};QCheckBox* individual_{};QTableWidget* table_{};
    bool entering_{true},highlight_all_{true};std::set<std::string> inspected_;
};
}

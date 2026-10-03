#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/document/part_document.hpp>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace zima::app {
// Pending editor data only. The calculation transaction owns history identity,
// eligibility and persistence; a dialog must not create a provisional feature.
struct SolidStateEdit {
    std::string name;
    bool restore{};
    bool all{true};
    double coefficient{1};
    std::vector<std::string> owners;
    bool operator==(const SolidStateEdit&)const=default;
};

class SolidStateDialog final:public ui::PropertiesSubWindow {
public:
    using Commit=std::function<void(SolidStateEdit)>;
    SolidStateDialog(SolidStateEdit initial,std::map<std::string,QString> available,
                    Commit commit,QWidget* parent)
        :PropertiesSubWindow(initial.restore?tr("Restore shape"):tr("Straighten"),parent),
         pending_(std::move(initial)),available_(std::move(available)),commit_(std::move(commit)) {
        setObjectName("solidStateDialog");setAttribute(Qt::WA_DeleteOnClose);
        setProperty("originSelectionBound",true);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        name_=new QLineEdit(QString::fromStdString(pending_.name),this);
        name_->setObjectName("solidStateName");form->addRow(tr("Název"),name_);
        factor_=new QDoubleSpinBox(this);factor_->setObjectName("solidStateCoefficient");
        factor_->setDecimals(8);factor_->setRange(.00000001,1e9);factor_->setSingleStep(.01);
        factor_->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
        factor_->setValue(pending_.coefficient);
        factor_->setToolTip(tr("Straight length equals the section centroid path length multiplied by this coefficient. Cross-section dimensions stay unchanged."));
        if(!pending_.restore)form->addRow(tr("Length coefficient"),factor_);else factor_->hide();
        // Keep an unchanged authored value exact, even when display precision
        // rounds it. Restore never reads or overwrites the length coefficient.
        connect(factor_,&QDoubleSpinBox::valueChanged,this,[this](double value){pending_.coefficient=value;});
        all_=new QCheckBox(tr("All eligible elements"),this);all_->setObjectName("solidStateAll");
        individual_=new QCheckBox(tr("Vybrat jednotlivé prvky"),this);individual_->setObjectName("solidStateIndividual");
        auto* group=new QButtonGroup(this);group->setExclusive(true);group->addButton(all_);group->addButton(individual_);
        all_->setChecked(pending_.all);individual_->setChecked(!pending_.all);
        form->addRow(all_);form->addRow(individual_);
        table_=new QTableWidget(0,4,this);table_->setObjectName("solidStateElements");
        table_->setHorizontalHeaderLabels({tr("Č."),{},tr("Prvek"),{}});
        table_->verticalHeader()->hide();table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        for(int column:{0,1,3}) {table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,34);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        table_->verticalHeader()->setDefaultSectionSize(34);
        setProperty("expandBottomTable",true);content_layout()->addWidget(table_,1);
        connect(all_,&QCheckBox::toggled,this,[this](bool checked){if(checked){pending_.all=true;active_=-1;inspected_.clear();refresh();}});
        connect(individual_,&QCheckBox::toggled,this,[this](bool checked){if(checked){pending_.all=false;active_=static_cast<int>(pending_.owners.size());inspected_.clear();refresh();}});
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){
            if(column==2&&!pending_.all){active_=row;refresh();}
        });
        connect(this,&QDialog::finished,this,[this]{end_entry();});
        active_=pending_.all?-1:static_cast<int>(pending_.owners.size());
        set_initial_size({490,360});refresh();
    }
    std::function<void()> selection_changed;
    void bind_container(document::HistoryContainer feature){container_=std::move(feature);}
    std::optional<document::HistoryContainer> pending_container()const {
        if(!container_)return {};
        auto result=*container_;const auto value=pending_value();
        result.name=value.name;result.solid_state={value.all,value.coefficient,value.owners};return result;
    }
    SolidStateEdit pending_value()const {auto value=pending_;value.name=name_->text().trimmed().toStdString();return value;}
    bool selecting()const{return !pending_.all&&active_>=0;}
    bool available(const std::string& id)const{return available_.contains(id);}
    void set_source(const std::string& id) {
        if(!selecting()||!available(id))return;
        if(std::ranges::find(pending_.owners,id)!=pending_.owners.end())return;
        if(active_<static_cast<int>(pending_.owners.size())) {
            inspected_.erase(pending_.owners[active_]);pending_.owners[active_]=id;
        }else pending_.owners.push_back(id);
        active_=static_cast<int>(pending_.owners.size());refresh();
    }
    void end_entry(){active_=-1;inspected_.clear();refresh();}
    std::vector<std::string> selected()const {
        if(!pending_.all)return pending_.owners;
        std::vector<std::string> result;for(const auto& [id,label]:available_)result.push_back(id);return result;
    }
    std::vector<std::string> highlighted()const{return {inspected_.begin(),inspected_.end()};}
protected:
    bool submit()override {
        auto value=pending_value();
        if(value.name.empty())throw std::invalid_argument("Specify a nonempty object name.");
        if(!value.restore&&(!std::isfinite(value.coefficient)||value.coefficient<=0))
            throw std::invalid_argument("Straightening coefficient must be positive and finite.");
        const auto owners=selected();
        if(owners.empty())throw std::invalid_argument("Select at least one eligible solid element.");
        for(const auto& id:owners)if(!available(id))throw std::invalid_argument("A selected solid element is unavailable.");
        commit_(std::move(value));return true;
    }
private:
    void refresh() {
        const auto rows=selected();table_->setRowCount(0);
        const int count=static_cast<int>(rows.size())+(pending_.all?0:1);
        table_->setRowCount(count);
        for(int row=0;row<count;++row) {
            table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            const bool populated=row<static_cast<int>(rows.size());
            const auto id=populated?rows[row]:std::string{};
            if(!pending_.all) {
                auto* indicator=ui::build_reference_row_indicator([this,id]{
                    std::erase(pending_.owners,id);inspected_.erase(id);
                    active_=static_cast<int>(pending_.owners.size());refresh();
                });
                ui::set_reference_row_populated(indicator,populated);
                if(auto* clear=indicator->findChild<QPushButton*>())clear->setToolTip(tr("Remove this element from the selection"));
                table_->setCellWidget(row,1,ui::centered_cell_widget(indicator));
            }
            auto* field=new ui::ReferenceCellItem;
            if(populated){field->set_reference(QString::fromStdString(id));field->setText(available(id)?available_.at(id):QString::fromStdString(id));}
            else field->setText(tr("Vyberte prvek ve View nebo ve stromu"));
            field->set_active_input(selecting()&&active_==row);field->set_inspected(inspected_.contains(id));
            table_->setItem(row,2,field);
            if(populated&&available(id)) {
                auto* eye=ui::build_reference_inspection_button(true,inspected_.contains(id),[this,id](bool checked){
                    if(checked)inspected_.insert(id);else inspected_.erase(id);refresh();
                });table_->setCellWidget(row,3,ui::centered_cell_widget(eye));
            }
        }
        if(selection_changed)selection_changed();
    }
    SolidStateEdit pending_;std::map<std::string,QString> available_;Commit commit_;
    std::optional<document::HistoryContainer> container_;
    QLineEdit* name_{};QDoubleSpinBox* factor_{};QCheckBox* all_{};QCheckBox* individual_{};QTableWidget* table_{};
    int active_{-1};std::set<std::string> inspected_;
};
}

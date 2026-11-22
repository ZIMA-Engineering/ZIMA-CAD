#pragma once
#include <zima/document/fem_definition.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <algorithm>

namespace zima::app {
class FemDistanceDialog final : public ui::PropertiesSubWindow {
public:
    document::FemCondition pending;
    std::function<void()> changed;
    FemDistanceDialog(document::FemCondition value,document::FemDefinition definition,
            std::function<void(document::FemCondition)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Distance measurement"),parent),pending(std::move(value)),
          definition_(std::move(definition)),commit_(std::move(commit)) {
        setObjectName("femDistanceDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;auto* name=new QLineEdit(QString::fromStdString(pending.name),this);
        name->setObjectName("femDistanceName");form->addRow(tr("Název"),name);content_layout()->addLayout(form);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        table_=new QTableWidget(2,5,this);table_->setObjectName("femDistanceReferences");
        table_->horizontalHeader()->hide();table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(32);
        table_->setFixedHeight(70);table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for(int col:{0,1,4}){table_->setColumnWidth(col,32);table_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Fixed);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
        ui::install_reference_cell_delegate(table_);
        for(unsigned i=0;i<2;++i) {
            table_->setItem(i,0,new QTableWidgetItem(QString::number(i+1)));
            table_->setCellWidget(i,1,ui::build_reference_row_indicator([this,i]{arm(i);}));
            table_->setItem(i,2,new QTableWidgetItem(i?tr("Second point"):tr("First point")));
            fields_[i]=new ui::ReferenceCellItem;table_->setItem(i,3,fields_[i]);
            eyes_[i]=ui::build_reference_inspection_button(false,false,[this,i](bool checked){inspected_[i]=checked;refresh();notify();});
            table_->setCellWidget(i,4,ui::centered_cell_widget(eyes_[i]));
        }
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==3)arm(row);});
        content_layout()->addWidget(table_);
        auto* help=new QLabel(tr("Select two measurement points from this FEM Body. Results show the original distance, deformed distance and their difference, independent of display magnification."),this);
        help->setWordWrap(true);content_layout()->addWidget(help);
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        set_initial_size({570,260});refresh();
    }
    const document::FemCondition* point(const std::string& id)const {
        const auto found=std::ranges::find(definition_.conditions,id,&document::FemCondition::id);
        return found!=definition_.conditions.end()&&found->kind==document::FemConditionKind::MeasurementPoint?&*found:nullptr;
    }
    bool active_input()const{return active_.has_value();}
    bool inspected(unsigned i)const{return inspected_[i];}
    void arm(unsigned i){active_=i;refresh();notify();}
    void end_input(){active_.reset();refresh();notify();}
    void accept_point(const std::string& id) {
        if(!active_||!point(id))return;
        const auto row=*active_;pending.distance->point_ids[row]=id;inspected_[row]=false;
        active_=row==0&&pending.distance->point_ids[1].empty()?std::optional<unsigned>{1}:std::nullopt;
        refresh();notify();
    }
protected:
    bool submit()override {
        try{document::validate_fem_condition(pending);commit_(pending);return true;}
        catch(const std::exception& e){status_->setText(tr(e.what()));return false;}
    }
private:
    document::FemDefinition definition_;
    std::function<void(document::FemCondition)> commit_;
    QTableWidget* table_{};std::array<ui::ReferenceCellItem*,2> fields_{};std::array<QToolButton*,2> eyes_{};
    std::array<bool,2> inspected_{};std::optional<unsigned> active_;QLabel* status_{};
    void notify(){if(changed)changed();}
    void refresh() {
        auto definition=definition_;std::erase_if(definition.conditions,[this](const auto& c){return c.id==pending.id;});
        definition.conditions.push_back(pending);document::refresh_fem_distances(definition);pending.distance=definition.conditions.back().distance;
        for(unsigned i=0;i<2;++i) {
            const auto& id=pending.distance->point_ids[i];const auto* source=point(id);
            if(id.empty()){fields_[i]->clear_reference();fields_[i]->setText(tr("Vyberte…"));}
            else{fields_[i]->set_reference(QString::fromStdString(id));fields_[i]->setText(QString::fromStdString(source?source->name:id));}
            fields_[i]->set_active_input(active_==i);fields_[i]->set_inspected(inspected_[i]&&source);
            const QSignalBlocker blocked(eyes_[i]);eyes_[i]->setEnabled(source!=nullptr);eyes_[i]->setChecked(inspected_[i]&&source);
        }
        const bool entered=!pending.distance->point_ids[0].empty()&&!pending.distance->point_ids[1].empty();
        status_->setText(entered&&!pending.distance->reference_valid?tr("A measurement point reference is unavailable."):QString{});table_->viewport()->update();
    }
};
}

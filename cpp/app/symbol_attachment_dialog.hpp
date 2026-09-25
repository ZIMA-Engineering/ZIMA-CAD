#pragma once
#include "symbol_properties_dialog.hpp"
#include <zima/symbols/placement.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QHeaderView>
#include <QTableWidget>
#include <QToolButton>

namespace zima::app {
// The definition/variant editor is shared with Sketch insertion. This panel
// adds annotation attachment only; it never edits container placement.
class SymbolAttachmentDialog final : public SymbolDialog {
public:
    std::function<void()> changed;
    SymbolAttachmentDialog(symbols::Placement value,std::function<void(symbols::Placement)> commit,QWidget* parent,bool sheet=false)
        :SymbolDialog(value.symbol,{},[this,commit=std::move(commit)](auto instance){
            auto value=placement_;value.symbol=std::move(instance);value.validate();commit(std::move(value));
        },parent),placement_(std::move(value)),sheet_(sheet) {
        setObjectName("symbolAttachmentDialog");set_initial_size({420,530});
        auto* panel=new QWidget(this);auto* form=new QFormLayout(panel);form->setContentsMargins(0,0,0,0);
        table_=new QTableWidget(1,3,panel);table_->setObjectName("symbolReference");
        table_->setHorizontalHeaderLabels({QString(),sheet_?tr("Reference symbolu"):tr("Plocha symbolu"),QString()});
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->verticalHeader()->show();table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        for(int column:{0,2}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,32);}
        table_->setFixedHeight(65);ui::install_reference_cell_delegate(table_);
        indicator_=ui::build_reference_row_indicator([this]{placement_.reference.reset();placement_.unresolved=false;active_=true;inspected_=false;refresh();notify();});
        table_->setCellWidget(0,0,indicator_);field_=new ui::ReferenceCellItem;table_->setItem(0,1,field_);
        eye_=ui::build_reference_inspection_button(false,false,[this](bool on){inspected_=on;refresh();notify();});
        table_->setCellWidget(0,2,ui::centered_cell_widget(eye_));form->addRow(table_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==1){active_=true;refresh();notify();}});
        leader_=new QCheckBox(tr("Odkazová čára se šipkou"),panel);leader_->setObjectName("symbolLeader");leader_->setChecked(placement_.leader);
        form->addRow(leader_);connect(leader_,&QCheckBox::toggled,this,[this](bool on){placement_.leader=on;if(on&&placement_.symbol.x==0&&placement_.symbol.y==0&&placement_.leader_bends.empty())set_anchor(15,5);else notify();});
        const std::array labels{tr("Počátek X"),tr("Počátek Y"),tr("Počátek Z")};
        for(std::size_t i=0;i<3;++i){origin_[i]=new QDoubleSpinBox(panel);origin_[i]->setObjectName(QString("symbolOrigin%1").arg(i));
            origin_[i]->setRange(-1000000,1000000);origin_[i]->setDecimals(ui::numeric_decimal_places(parent));form->addRow(labels[i],origin_[i]);
            connect(origin_[i],&QDoubleSpinBox::valueChanged,this,[this]{placement_.frame.origin={origin_[0]->value(),origin_[1]->value(),origin_[2]->value()};notify();});}
        if(sheet_){form->labelForField(origin_[2])->hide();origin_[2]->hide();}
        unresolved_=new QLabel(tr("Reference není dostupná. Symbol zachovává poslední polohu."),panel);unresolved_->setWordWrap(true);form->addRow(unresolved_);
        content_layout()->insertWidget(0,panel);active_=!placement_.reference;refresh();
        set_preview_callback([this](const auto& instance){placement_.symbol=instance;notify();});
    }
    const symbols::Placement& pending_placement()const{return placement_;}
    void begin_entry(){active_=true;refresh();notify();}
    bool active()const{return active_;}
    bool inspected()const{return inspected_;}
    void end_entry(){active_=inspected_=false;refresh();notify();}
    void set_planar_placement(symbols::Placement value){placement_=std::move(value);active_=false;refresh();notify();}
    void set_surface(const kernel::FaceReference& face,const std::string& document,kernel::Vec3 contact) {
        symbols::attach_to_surface(placement_,face,document,contact,placement_.reference&&placement_.reference->reversed);
        active_=false;refresh();notify();
    }
private:
    symbols::Placement placement_;bool active_{},inspected_{},sheet_{};
    QTableWidget* table_{};ui::ReferenceCellItem* field_{};QWidget* indicator_{};QToolButton* eye_{};
    QCheckBox* leader_{};QLabel* unresolved_{};std::array<QDoubleSpinBox*,3> origin_{};
    void notify(){if(changed)changed();}
    void refresh(){
        const auto identity=placement_.reference?QString::fromStdString(placement_.reference->semantic_key):QString{};
        field_->setText(identity.isEmpty()?(sheet_?tr("Vyberte geometrii nebo polohu na listu."):tr("Vyberte plochu pro symbol.")):(sheet_?tr("Reference symbolu"):tr("Plocha symbolu")));field_->setToolTip(identity);
        if(identity.isEmpty())field_->clear_reference();else field_->set_reference(identity);
        field_->set_active_input(active_);field_->set_inspected(inspected_);ui::set_reference_row_populated(indicator_,placement_.reference.has_value());
        eye_->setEnabled(placement_.reference.has_value());eye_->setChecked(inspected_);table_->viewport()->update();
        const auto& p=placement_.frame.origin;const std::array values{p.x,p.y,p.z};
        for(std::size_t i=0;i<3;++i){const QSignalBlocker blocked(origin_[i]);origin_[i]->setValue(values[i]);origin_[i]->setEnabled(!placement_.reference);}
        unresolved_->setVisible(placement_.unresolved);
    }
};
}

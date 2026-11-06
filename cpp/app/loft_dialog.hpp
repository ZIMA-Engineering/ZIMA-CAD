#pragma once
#include "sweep_placement_dialog.hpp"
#include <zima/document/loft.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <set>
namespace zima::app {
class LoftDialog final:public SweepPlacementDialog {
public:
    std::function<void(unsigned)> edit_sketch_properties;
    LoftDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,
               bool allow_subtract,QWidget* parent)
        :SweepPlacementDialog(tr("Vlastnosti přechodu"),std::move(initial),parent),
         commit_(std::move(commit)),allow_subtract_(allow_subtract) {
        setObjectName("loftDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(470);
        setProperty("expandBottomTable",true);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("loftName");
        form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& value){pending.name=value.toStdString();});
        auto* precision=new SweepPrecisionControls(pending.sweep_precision,this,[this]{pending.sweep_precision=precision_->value();notify();});
        content_layout()->addWidget(precision);
        precision_=precision;
        install_placement();
        result_=new QComboBox(this);result_->setObjectName("loftResultType");
        result_->addItems({tr("Těleso"),tr("Tenkostěnný prvek"),tr("Plocha")});
        result_->setCurrentIndex(pending.loft.result_type==document::ProfileResultType::Surface?2:pending.loft.result_type==document::ProfileResultType::Thin?1:0);
        auto* result_form=new QFormLayout;result_form->addRow(tr("Typ výsledku"),result_);
        thin_form_=new QFormLayout;
        thickness_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);thickness_->setObjectName("loftThickness");
        thickness_->setRange(.001,1'000'000);thickness_->setValue(pending.loft.thickness);thin_form_->addRow(tr("Tloušťka"),thickness_);
        ui::bind_numeric_value_lock(thickness_,"thickness",pending.value_locks,[this]{notify();});
        side_=new QComboBox(this);side_->setObjectName("loftThinSide");side_->addItems({tr("Dovnitř"),tr("Ven"),tr("Symetricky")});
        side_->setCurrentIndex(pending.loft.thin_mode==document::ThinMode::OneSide?0:pending.loft.thin_mode==document::ThinMode::OtherSide?1:2);
        thin_form_->addRow(tr("Strana tloušťky"),side_);
        connect(result_,&QComboBox::currentIndexChanged,this,[this](int value){
            pending.loft.result_type=value==2?document::ProfileResultType::Surface:value==1?document::ProfileResultType::Thin:document::ProfileResultType::Solid;
            update_result();notify();
        });
        connect(thickness_,&QDoubleSpinBox::valueChanged,this,[this](double value){pending.loft.thickness=value;notify();});
        connect(side_,&QComboBox::currentIndexChanged,this,[this](int value){
            pending.loft.thin_mode=value==0?document::ThinMode::OneSide:value==1?document::ThinMode::OtherSide:document::ThinMode::Symmetric;notify();
        });
        auto* note=new QLabel(tr("Nakreslete alespoň dvě vlastní 2D skici profilů. Řídicí skici musí protínat všechny profily."),this);
        note->setWordWrap(true);content_layout()->addWidget(note);
        auto* lists=new QSplitter(Qt::Vertical,this);lists->setObjectName("loftLists");
        for(bool guides:{false,true}) {
            auto* group=new QWidget(lists);auto* layout=new QVBoxLayout(group);layout->setContentsMargins(0,0,0,0);
            layout->addWidget(new QLabel(guides?tr("Řídicí skici"):tr("Skici profilů"),group));
            auto* table=new QTableWidget(0,5,group);tables_[guides?1:0]=table;
            table->setObjectName(guides?"loftGuides":"loftProfiles");
            table->setHorizontalHeaderLabels({tr("Č."),{}, {},guides?tr("Řídicí skica"):tr("Skica profilu"),{}});
            table->verticalHeader()->hide();table->verticalHeader()->setDefaultSectionSize(34);
            table->setSelectionMode(QAbstractItemView::NoSelection);table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            ui::install_reference_cell_delegate(table);
            for(int column:{0,1,2,4}){table->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table->setColumnWidth(column,32);}
            table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
            table->setMinimumHeight(115);layout->addWidget(table,1);
            auto* ordering=new QHBoxLayout;
            for(bool down:{false,true}) {
                auto* button=new QPushButton(ui::reference_arrow_icon(down?Qt::DownArrow:Qt::UpArrow),down?tr("Dolů"):tr("Nahoru"),group);
                button->setObjectName(QString(guides?"loftGuides":"loftProfiles")+(down?"Down":"Up"));
                moves_[guides?1:0][down?1:0]=button;ordering->addWidget(button);
                connect(button,&QPushButton::clicked,this,[this,guides,down]{move(guides,down?1:-1);});
            }
            layout->addLayout(ordering);
            connect(table,&QTableWidget::cellClicked,this,[this,guides](int row,int column){if(column==3)open_sketch(guides,row);});
            lists->addWidget(group);
        }
        lists->setStretchFactor(0,1);lists->setStretchFactor(1,1);content_layout()->addWidget(lists,1);
        content_layout()->addLayout(result_form);content_layout()->addLayout(thin_form_);
        operation_=add_feature_operation_buttons(this,content_layout(),pending.combine_mode==document::CombineMode::Subtract,
            [this](bool subtract){pending.combine_mode=subtract?document::CombineMode::Subtract:document::CombineMode::Add;notify();});
        status_=new QLabel(this);status_->setObjectName("loftStatus");status_->setWordWrap(true);content_layout()->addWidget(status_);
        refresh();update_result();set_initial_size({560,950});
    }
    void set_status(const QString& text)override{status_->setText(text);}
    void set_sketch(unsigned stage,const sketcher::Sketch& sketch)override {
        auto& owned=pending.loft.sketches.at(stage);
        if(!owned.sketch_feature||owned.sketch_feature->id!=sketch.owner_container_id)throw std::invalid_argument("Loft source Sketch is unavailable.");
        owned.sketch_serialized=sketch.serialized();owned.sketch_feature->name=sketch.name;refresh();
    }
    bool owns_reference_owner(const std::string& owner)const override {
        if(SweepPlacementDialog::owns_reference_owner(owner))return true;
        for(const auto& value:pending.loft.sketches)if(value.sketch_feature) {
            const auto& f=*value.sketch_feature;
            if(owner==f.id||owner==f.feature_id||owner==f.container_origin.id||owner==f.feature.sketch_id)return true;
        }
        return false;
    }
    bool set_inline_parameter_value(std::string_view key,double value)override {
        if(key=="thickness"&&pending.loft.result_type==document::ProfileResultType::Thin&&thickness_->isEnabled()&&!thickness_->isReadOnly()) {
            thickness_->setValue(value);return true;
        }
        return SweepPlacementDialog::set_inline_parameter_value(key,value);
    }

    const std::set<std::string>& inspected_sources()const{return inspected_;}
    void open_sketch(bool guides,int row) {
        auto& values=family(guides);if(row<0||row>static_cast<int>(values.size()))return;
        if(row==static_cast<int>(values.size())) {
            auto value=document::create_loft_sketch(pending);
            auto sketch=sketcher::Sketch::from_serialized(value.sketch_serialized);sketch.name=tr("Skica").toStdString();
            value.sketch_serialized=sketch.serialized();value.sketch_feature->name=sketch.name;
            values.push_back(value.sketch_feature->id);pending.loft.sketches.push_back(std::move(value));refresh();notify();
        }
        const auto found=std::ranges::find_if(pending.loft.sketches,[&](const auto& value){return value.sketch_feature->id==values[row];});
        if(edit_sketch_properties)edit_sketch_properties(static_cast<unsigned>(found-pending.loft.sketches.begin()));
    }
    void refresh() {
        for(bool guides:{false,true}) {
            auto* table=tables_[guides?1:0];if(!table)continue;QSignalBlocker blocked(table);
            const auto& values=family(guides);table->clearContents();table->setRowCount(static_cast<int>(values.size()+1));
            for(int row=0;row<=static_cast<int>(values.size());++row) {
                const bool exists=row<static_cast<int>(values.size());const auto id=exists?values[row]:std::string{};
                table->setRowHeight(row,34);table->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
                auto* order=new QCheckBox(table);order->setEnabled(!id.empty());order->setChecked(!id.empty()&&ordered_[guides?1:0]==id);
                order->setToolTip(tr("Vybrat položku pro změnu pořadí"));
                table->setCellWidget(row,1,ui::centered_cell_widget(order));
                connect(order,&QCheckBox::toggled,this,[this,guides,id](bool checked){
                    auto& selected=ordered_[guides?1:0];selected=checked?id:selected==id?std::string{}:selected;
                    auto* table=tables_[guides?1:0];
                    for(int row=0;row<table->rowCount();++row)if(auto* cell=table->cellWidget(row,1))
                        if(auto* box=cell->findChild<QCheckBox*>()) {
                            QSignalBlocker block(box);
                            const auto& values=family(guides);
                            box->setChecked(row<static_cast<int>(values.size())&&!selected.empty()&&values[row]==selected);
                        }
                    update_moves(guides);
                });
                auto* indicator=ui::build_reference_row_indicator([this,guides,row]{remove(guides,row);});
                ui::set_reference_row_populated(indicator,!id.empty());
                if(auto* button=indicator->findChild<QPushButton*>())button->setToolTip(tr("Odstranit vlastní skicu"));
                table->setCellWidget(row,2,ui::centered_cell_widget(indicator));
                const auto owned=std::ranges::find_if(pending.loft.sketches,[&](const auto& value){return value.sketch_feature->id==id;});
                const auto text=id.empty()?(guides?tr("Přidat řídicí skicu…"):tr("Přidat skicu profilu…")):
                    QString::fromStdString(owned->sketch_feature->name);
                auto* item=new ui::ReferenceCellItem(text);if(!id.empty())item->set_reference(QString::fromStdString(id));

                item->set_inspected(inspected_.contains(id));
                table->setItem(row,3,item);
                auto* eye=ui::build_reference_inspection_button(!id.empty(),inspected_.contains(id),[this,id](bool on){
                    if(on)inspected_.insert(id);else inspected_.erase(id);
                    for(bool family_guides:{false,true}) {
                        auto* table=tables_[family_guides?1:0];const auto& values=family(family_guides);
                        for(int row=0;row<static_cast<int>(values.size());++row)
                            if(auto* item=dynamic_cast<ui::ReferenceCellItem*>(table->item(row,3)))item->set_inspected(inspected_.contains(values[row]));
                        table->viewport()->update();
                    }
                    notify();
                });
                table->setCellWidget(row,4,ui::centered_cell_widget(eye));
            }
            update_moves(guides);
        }
    }
protected:
    bool submit()override {
        try{read_placement();document::validate_loft(pending);commit_(pending);return true;}
        catch(const std::exception& error){set_status(tr(error.what()));return false;}
    }
private:
    std::vector<std::string>& family(bool guides){return guides?pending.loft.guides:pending.loft.profiles;}
    int selected_index(bool guides) {
        if(ordered_[guides?1:0].empty())return -1;
        const auto& values=family(guides);const auto found=std::ranges::find(values,ordered_[guides?1:0]);
        return found==values.end()?-1:static_cast<int>(found-values.begin());
    }
    void update_moves(bool guides) {
        const auto row=selected_index(guides);moves_[guides?1:0][0]->setEnabled(row>0);
        moves_[guides?1:0][1]->setEnabled(row>=0&&row+1<static_cast<int>(family(guides).size()));
    }
    void move(bool guides,int step) {
        const auto row=selected_index(guides);auto& values=family(guides);
        if(row<0||row+step<0||row+step>=static_cast<int>(values.size()))return;
        std::swap(values[row],values[row+step]);
        refresh();notify();
    }
    void remove(bool guides,int row) {
        auto& values=family(guides);if(row<0||row>=static_cast<int>(values.size()))return;
        const auto id=values[row];inspected_.erase(id);if(ordered_[guides?1:0]==id)ordered_[guides?1:0].clear();
        values.erase(values.begin()+row);
        std::erase_if(pending.loft.sketches,[&](const auto& value){return value.sketch_feature->id==id;});
        set_active_reference_index({});refresh();notify();
    }
    void update_result() {
        const bool surface=pending.loft.result_type==document::ProfileResultType::Surface;
        if(surface){pending.combine_mode=document::CombineMode::Add;operation_.add->setChecked(true);operation_.subtract->setChecked(false);}
        operation_.subtract->setEnabled(allow_subtract_&&!surface);
        const bool thin=pending.loft.result_type==document::ProfileResultType::Thin;
        thin_form_->setRowVisible(thickness_,thin);thin_form_->setRowVisible(side_,thin);
    }
    void notify(){if(changed)changed();}
    std::function<void(document::HistoryContainer)> commit_;
    FeatureOperationButtons operation_{};bool allow_subtract_{};
    QTableWidget* tables_[2]{};QPushButton* moves_[2][2]{};
    SweepPrecisionControls* precision_{};
    QComboBox *result_{},*side_{};ui::UnitDoubleSpinBox* thickness_{};QFormLayout* thin_form_{};QLabel* status_{};
    std::string ordered_[2];std::set<std::string> inspected_;
};
}

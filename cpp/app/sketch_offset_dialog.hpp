#pragma once
#include <zima/sketcher/sketch.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace zima::app {
class SketchOffsetDialog final : public ui::PropertiesSubWindow {
public:
    std::function<void()> changed;
    SketchOffsetDialog(sketcher::SketchOffset initial,std::function<void(sketcher::SketchOffset,bool)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti offsetu"),parent),pending_(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("sketchOffsetDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(360);
        auto* form=new QFormLayout;
        table_=new QTableWidget(1,2,this);table_->setObjectName("sketchOffsetReference");
        table_->horizontalHeader()->hide();table_->verticalHeader()->hide();table_->setFixedHeight(42);
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table_->setColumnWidth(1,32);
        ui::install_reference_cell_delegate(table_);field_=new ui::ReferenceCellItem;table_->setItem(0,0,field_);
        eye_=ui::build_reference_inspection_button(!pending_.source_id.empty(),false,[this](bool value){inspected_=value;notify();});
        table_->setCellWidget(0,1,ui::centered_cell_widget(eye_));
        connect(table_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==0){armed_=true;notify();}});
        form->addRow(tr("Zdrojová křivka"),table_);
        distance_=new QDoubleSpinBox(this);distance_->setObjectName("sketchOffsetDistance");distance_->setDecimals(8);
        distance_->setRange(.00000001,1e6);distance_->setSuffix(tr(" mm"));distance_->setValue(pending_.distance);
        form->addRow(tr("Vzdálenost"),distance_);
        flip_=new QPushButton(tr("Flip"),this);flip_->setObjectName("sketchOffsetFlip");flip_->setCheckable(true);flip_->setChecked(pending_.flipped);
        form->addRow(tr("Strana odsazení"),flip_);
        free_=new QCheckBox(tr("Osvobodit — zachovat tvar bez návaznosti"),this);free_->setObjectName("sketchOffsetFree");free_->setVisible(!pending_.id.empty());form->addRow(free_);
        auto* hint=new QLabel(tr("Vyberte vlastní křivku skici. Fialová šipka ukazuje stranu odsazení."),this);hint->setWordWrap(true);form->addRow(hint);
        error_=new QLabel(this);error_->setWordWrap(true);form->addRow(error_);content_layout()->addLayout(form);
        connect(distance_,&QDoubleSpinBox::valueChanged,this,[this](double d){pending_.distance=d;notify();});
        connect(flip_,&QPushButton::toggled,this,[this](bool f){pending_.flipped=f;notify();});
        connect(free_,&QCheckBox::toggled,this,[this](bool f){armed_=false;distance_->setEnabled(!f);flip_->setEnabled(!f);table_->setEnabled(!f);notify();});
        armed_=pending_.source_id.empty();refresh();
    }
    void set_source_label(const QString& label){source_label_=label;refresh();}
    sketcher::SketchOffset values() const {return pending_;}
    bool entering_reference() const {return armed_&&!free_->isChecked();}
    bool inspecting() const {return inspected_;}
    bool freeing() const {return free_->isChecked();}
    void set_source(const std::string& id){if(!entering_reference())return;pending_.source_id=id;armed_=false;notify();}
    void end_entry(){armed_=false;inspected_=false;eye_->setChecked(false);notify();}
    void set_error(const QString& error){error_->setText(error);buttons()->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty()&&!pending_.source_id.empty());}
protected:
    bool submit() override {try {commit_(pending_,free_->isChecked());return true;}catch(const std::exception& e){set_error(QString::fromUtf8(e.what()));return false;}}
private:
    void refresh(){field_->set_reference(QString::fromStdString(pending_.source_id));field_->setText(pending_.source_id.empty()?tr("Vyberte křivku ve View"):source_label_.isEmpty()?tr("Křivka"):source_label_);field_->set_active_input(armed_);field_->set_inspected(inspected_);eye_->setEnabled(!pending_.source_id.empty());table_->viewport()->update();}
    void notify(){refresh();if(changed)changed();}
    sketcher::SketchOffset pending_;std::function<void(sketcher::SketchOffset,bool)> commit_;
    bool armed_{true},inspected_{};
    QString source_label_;
    QTableWidget* table_{};ui::ReferenceCellItem* field_{};QToolButton* eye_{};
    QDoubleSpinBox* distance_{};QPushButton* flip_{};QCheckBox* free_{};QLabel* error_{};
};
}

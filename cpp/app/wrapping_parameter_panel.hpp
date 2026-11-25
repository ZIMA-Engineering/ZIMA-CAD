#pragma once
#include <zima/document/wrapping_parameters.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QComboBox>
#include <set>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <functional>
#include <cmath>

namespace zima::app {
class WrappingParameterPanel final:public QWidget {
public:
    std::function<void(bool)> pick_requested;
    std::function<void()> changed,inspection_changed,layout_changed;
    std::function<QString(const kernel::FaceReference&)> face_label;
    explicit WrappingParameterPanel(document::WrappingParameters initial,QWidget* parent)
        :QWidget(parent),pending_(std::move(initial)) {
        setObjectName("wrappingParameters");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        form_=new QFormLayout;layout->addLayout(form_);
        output_=new QComboBox(this);output_->setObjectName("wrappingOutput");
        output_->addItem(tr("Skica"),int(document::WrappingOutput::Curves));
        output_->addItem(tr("Rozdělit"),int(document::WrappingOutput::Split));
        output_->addItem(tr("Plocha"),int(document::WrappingOutput::Surface));
        output_->setCurrentIndex(std::max(0,output_->findData(int(pending_.output))));form_->addRow(tr("Výsledek"),output_);
        target_=new QTableWidget(1,4,this);target_->setObjectName("wrappingTarget");target_->verticalHeader()->hide();
        target_->setSelectionMode(QAbstractItemView::NoSelection);target_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui::install_reference_cell_delegate(target_);target_->verticalHeader()->setDefaultSectionSize(34);target_->setMinimumHeight(140);
        for(int col:{0,1,3}){target_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Fixed);target_->setColumnWidth(col,34);}
        target_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);layout->addWidget(target_,1);
        details_=new QFormLayout;layout->addLayout(details_);
        height_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);height_->setObjectName("wrappingHeight");
        height_->setRange(-1000000,1000000);height_->setValue(pending_.height);displayed_height_=height_->value();initial_height_=pending_.height;details_->addRow(tr("Korekce"),height_);
        height_->setToolTip(tr("Positive correction adds material outward; negative correction removes material inward."));
        notice_=new QLabel(tr("Select faces in continuation order. Connected parts of the same surface are included automatically. The Feature Origin anchors the wrap; on a sphere, distances from it are preserved and widths change."),this);
        notice_->setWordWrap(true);layout->addWidget(notice_);
        connect(output_,&QComboBox::currentIndexChanged,this,[this](int index){pending_.output=static_cast<document::WrappingOutput>(output_->itemData(index).toInt());refresh();notify();if(layout_changed)layout_changed();});
        connect(height_,&QDoubleSpinBox::valueChanged,this,[this](double value){pending_.height=std::abs(value)==std::abs(displayed_height_)?initial_height_:std::abs(value);notify();});
        connect(target_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2)begin_entry(row);});refresh();
    }
    const document::WrappingParameters& parameters()const{return pending_;}
    void set_targets(const std::vector<document::ExtrusionParameters::EndTarget>& values) {
        active_=false;row_=0;inspected_.clear();
        pending_.target.reset();pending_.continuations.clear();
        if(!values.empty()) {
            pending_.target=values.front();
            pending_.continuations.assign(values.begin()+1,values.end());
        }
        refresh();
    }
    void set_relief_mode(bool relief) {
        if(relief)pending_.output=document::WrappingOutput::Relief;
        else if(pending_.output==document::WrappingOutput::Relief)pending_.output=document::WrappingOutput::Curves;
        {const QSignalBlocker blocker(output_);output_->setCurrentIndex(std::max(0,output_->findData(int(pending_.output))));}
        refresh();
    }
    bool subtract()const{return pending_.output==document::WrappingOutput::Relief&&height_->value()<0;}
    void set_subtract(bool on){const QSignalBlocker blocker(height_);height_->setValue((on?-1.:1.)*pending_.height);displayed_height_=height_->value();}
    bool active_pick()const{return active_;}
    void begin_entry(int row=0){row_=row;active_=true;refresh();if(pick_requested)pick_requested(true);}
    void finish_entry(){active_=false;refresh();}
    bool set_target(document::ExtrusionParameters::EndTarget value) {
        if(!active_||value.kind!=document::EndTargetKind::Face)return false;
        for(int i=0;i<count();++i)if(i!=row_&&target(i).reference==value.reference)return false;
        if(row_<count()) {inspected_.erase(key(target(row_).reference));target(row_)=std::move(value);}
        else if(!pending_.target)pending_.target=std::move(value);
        else pending_.continuations.push_back(std::move(value));
        active_=false;refresh();notify();if(inspection_changed)inspection_changed();return true;
    }
    std::vector<kernel::FaceReference> inspected_faces()const {
        std::vector<kernel::FaceReference> result;
        for(int i=0;i<count();++i)if(inspected_.contains(key(target(i).reference)))result.push_back(target(i).reference);
        return result;
    }
    void refresh() {
        const bool relief=pending_.output==document::WrappingOutput::Relief;
        form_->setRowVisible(output_,!relief);details_->setRowVisible(height_,relief);
        target_->setHorizontalHeaderLabels({tr("Č."),{},tr("Cílové plochy"),{}});
        target_->setRowCount(count()+1);
        for(int row=0;row<=count();++row) {
            const bool populated=row<count();
            const auto reference=populated?target(row).reference:kernel::FaceReference{};
            const bool inspected=populated&&inspected_.contains(key(reference));
            target_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator([this,row]{
                if(row>=count())return;
                const auto removed=target(row).reference;inspected_.erase(key(removed));
                if(row==0) {
                    if(pending_.continuations.empty())pending_.target.reset();
                    else {pending_.target=std::move(pending_.continuations.front());pending_.continuations.erase(pending_.continuations.begin());}
                }else pending_.continuations.erase(pending_.continuations.begin()+row-1);
                row_=std::min(row,count());active_=true;refresh();notify();if(inspection_changed)inspection_changed();if(pick_requested)pick_requested(true);
            });
            indicator->findChild<QPushButton*>()->setToolTip(tr("Odstranit cílovou plochu ze seznamu"));
            ui::set_reference_row_populated(indicator,populated);target_->setCellWidget(row,1,indicator);
            const auto label=populated?(face_label?face_label(reference):QString::fromStdString(target(row).label)):tr("Select a cylindrical, conical, spherical or planar face to wrap.");
            auto* cell=new ui::ReferenceCellItem(label);if(populated)cell->set_reference(QString::fromStdString(reference.owner_id+":"+reference.semantic_key));
            cell->set_active_input(active_&&row_==row);cell->set_inspected(inspected);target_->setItem(row,2,cell);
            target_->setCellWidget(row,3,ui::centered_cell_widget(ui::build_reference_inspection_button(populated,inspected,[this,reference](bool on){
                if(on)inspected_.insert(key(reference));else inspected_.erase(key(reference));refresh();if(inspection_changed)inspection_changed();
            })));
        }
    }

protected:
    void changeEvent(QEvent* event)override {
        QWidget::changeEvent(event);if(event->type()!=QEvent::LanguageChange)return;
        output_->setItemText(0,tr("Skica"));output_->setItemText(1,tr("Rozdělit"));output_->setItemText(2,tr("Plocha"));
        static_cast<QLabel*>(form_->labelForField(output_))->setText(tr("Výsledek"));
        static_cast<QLabel*>(details_->labelForField(height_))->setText(tr("Korekce"));
        height_->setToolTip(tr("Positive correction adds material outward; negative correction removes material inward."));
        notice_->setText(tr("Select faces in continuation order. Connected parts of the same surface are included automatically. The Feature Origin anchors the wrap; on a sphere, distances from it are preserved and widths change."));refresh();
    }
private:
    using Key=std::tuple<std::string,std::string,std::string>;
    static Key key(const kernel::FaceReference& face){return {face.owner_id,face.semantic_key,face.instance_path};}
    int count()const{return pending_.target?1+static_cast<int>(pending_.continuations.size()):0;}
    document::ExtrusionParameters::EndTarget& target(int row){return row==0?*pending_.target:pending_.continuations.at(row-1);}
    const document::ExtrusionParameters::EndTarget& target(int row)const{return row==0?*pending_.target:pending_.continuations.at(row-1);}
    void notify(){if(changed)changed();}
    document::WrappingParameters pending_;
    QFormLayout *form_{},*details_{};QComboBox* output_{};QTableWidget* target_{};
    ui::UnitDoubleSpinBox* height_{};double displayed_height_{},initial_height_{};
    QLabel* notice_{};bool active_{};int row_{};std::set<Key> inspected_;
};
}
